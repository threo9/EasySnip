#include "scrollcapture.h"
#include <QHash>
#include <QPainter>
#include <QtAlgorithms>
#include <algorithm>
#include <limits>

namespace {
constexpr int sampleColumns = 64;
struct Row {
    QRgb pixels[sampleColumns];
    quint64 changed = 0;
    bool informative = false;
};

int difference(QRgb a, QRgb b) {
    if (a == b)
        return 0;
    return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
}

QVector<int> movingColumns(const QImage &previous, const QImage &current) {
    QVector<int> columns;
    const int height = current.height();
    const int width = current.width();
    QVector<int> counts(width), first(width, -1), last(width, -1);
    int *countData = counts.data(), *firstData = first.data(), *lastData = last.data();
    // Static navigation panes must not vote for zero motion. Require changes
    // over a vertical span so a caret, hovered row or scrollbar thumb cannot
    // determine the displacement of the entire viewport.
    // Scan contiguous rows once, keeping capture latency independent of cache
    // misses from repeatedly walking whole columns of a high-DPI image.
    for (int y = 1; y < height - 1; y += 2) {
        const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
        for (int x = 2; x < width - 2; ++x) {
            if (difference(a[x], b[x]) <= 12)
                continue;
            ++countData[x];
            if (firstData[x] < 0)
                firstData[x] = y;
            lastData[x] = y;
        }
    }
    for (int x = 2; x < width - 2; ++x)
        if (countData[x] >= qMax(4, height / 200) && lastData[x] - firstData[x] >= qMax(40, height / 5))
            columns.append(x);
    return columns;
}

QVector<Row> describe(const QImage &image, const QImage &other, const QVector<int> &columns) {
    QVector<Row> rows(image.height());
    Row *rowData = rows.data();
    int sampledColumns[sampleColumns];
    for (int x = 0; x < sampleColumns; ++x)
        sampledColumns[x] = columns[x * (columns.size() - 1) / (sampleColumns - 1)];
    for (int y = 0; y < image.height(); ++y) {
        auto &row = rowData[y];
        const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const auto *otherPixels = reinterpret_cast<const QRgb *>(other.constScanLine(y));
        int variation = 0;
        for (int x = 0; x < sampleColumns; ++x) {
            const int column = sampledColumns[x];
            row.pixels[x] = pixels[column];
            if (difference(pixels[column], otherPixels[column]) > 12)
                row.changed |= quint64(1) << x;
            if (x > 0)
                variation = qMax(variation, difference(row.pixels[x], row.pixels[x - 1]));
            if (y > 0)
                variation = qMax(variation, difference(row.pixels[x], rowData[y - 1].pixels[x]));
        }
        row.informative = row.changed != 0 && variation >= 36;
    }
    return rows;
}

double rowError(const Row &a, const Row &b) {
    int total = 0, count = 0;
    quint64 changed = a.changed | b.changed;
    if (changed == std::numeric_limits<quint64>::max()) {
        // Colorful regions often change every sampled column. A contiguous
        // scan avoids decoding a separate bit position for each pixel.
        for (int x = 0; x < sampleColumns; ++x)
            total += qMin(180, difference(a.pixels[x], b.pixels[x]));
        return double(total) / (sampleColumns * 3);
    }
    while (changed) {
        const int x = int(qCountTrailingZeroBits(changed));
        total += qMin(180, difference(a.pixels[x], b.pixels[x]));
        ++count;
        // Visit the changed columns in ascending order.
        changed &= changed - 1;
    }
    return count ? double(total) / (count * 3) : 0;
}

struct Match {
    int shift = 0;
    int footer = 0;
    double error = 1000;
};

struct Verification {
    Match match;
    double agreement = 0;
};

quint64 rowFingerprint(const QRgb *pixels, const QVector<int> &columns) {
    quint64 hash = 14695981039346656037ULL;
    for (int x : columns) {
        hash ^= pixels[x];
        hash *= 1099511628211ULL;
    }
    return hash;
}

QHash<quint64, int> uniqueRows(const QImage &image, const QVector<Row> &rows, const QVector<int> &columns) {
    QHash<quint64, int> positions;
    positions.reserve(image.height());
    for (int y = 2; y < image.height() - 2; ++y) {
        if (!rows[y].informative)
            continue;
        const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const quint64 hash = rowFingerprint(pixels, columns);
        auto found = positions.find(hash);
        if (found == positions.end())
            positions.insert(hash, y);
        else
            found.value() = -1;
    }
    return positions;
}

QPair<int, int> contentRows(const QVector<Row> &rows) {
    const int height = rows.size();
    int begin = 2, end = height - 2;
    const auto movingRow = [](const Row &row) {
        return row.informative && qPopulationCount(row.changed) >= 4;
    };
    while (begin < height / 3 && !movingRow(rows[begin]))
        ++begin;
    while (end > height * 2 / 3 && !movingRow(rows[end - 1]))
        --end;
    return {begin, end};
}

bool stationaryPatch(const QImage &previous, const QImage &current, int x, int y) {
    // Equal background pixels alone are common in scrolling text. Check a
    // neighbourhood before treating a pixel as a stationary occlusion.
    int changed = 0;
    for (int row = y - 1; row <= y + 1; ++row) {
        const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(row));
        const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(row));
        for (int column = x - 1; column <= x + 1; ++column)
            if (difference(a[column], b[column]) > 12 && ++changed > 3)
                return false;
    }
    return true;
}

struct StationaryAreas {
    QImage mask;
    int bottom = 0;
    int seam = 0;
};

StationaryAreas stationaryAreas(const QImage &previous, const QImage &current,
                                const QVector<int> &columns, int shift, bool restrictColumns = false,
                                const QRgb *const *aligned = nullptr) {
    constexpr int cellSize = 4;
    const int width = current.width(), height = current.height();
    const int gridWidth = (width + cellSize - 1) / cellSize;
    const int gridHeight = (height + cellSize - 1) / cellSize;
    QVector<uchar> motionErrors(gridWidth * gridHeight), connected(motionErrors.size());
    uchar *errorData = motionErrors.data(), *connectedData = connected.data();
    bool hasCandidate = false;
    const int firstColumn = restrictColumns ? columns.first() : 2;
    const int lastColumn = restrictColumns ? columns.last() : width - 3;
    // Stable textured patches identify fixed controls. Keep flat background in
    // displacement verification so incorrect overlaps remain detectable.
    for (int y = 1; y < height - 1; ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
        const auto *aboveA = reinterpret_cast<const QRgb *>(previous.constScanLine(y - 1));
        const auto *aboveB = reinterpret_cast<const QRgb *>(current.constScanLine(y - 1));
        const auto *shiftedA = y + shift >= 0 && y + shift < height
            ? reinterpret_cast<const QRgb *>(previous.constScanLine(y + shift)) : nullptr;
        const auto *shiftedB = y - shift >= 0 && y - shift < height
            ? reinterpret_cast<const QRgb *>(current.constScanLine(y - shift)) : nullptr;
        for (int x = firstColumn; x <= lastColumn; ++x) {
            // During historical recovery, matching retained pixels are direct
            // evidence of document content, including repeated digit shapes.
            if (aligned && difference(aligned[y][x], b[x]) <= 24)
                continue;
            // Most pixels already agree with the scrolling displacement.
            // Eliminate them before testing gradients and neighbourhoods.
            if ((shiftedA && difference(shiftedA[x], b[x]) <= 24) ||
                (shiftedB && difference(a[x], shiftedB[x]) <= 24))
                continue;
            if (difference(a[x], b[x]) > 6 ||
                qMax(difference(a[x], a[x - 1]), difference(a[x], aboveA[x])) < 18 ||
                qMax(difference(b[x], b[x - 1]), difference(b[x], aboveB[x])) < 18 ||
                !stationaryPatch(previous, current, x, y))
                continue;
            const int cell = (y / cellSize) * gridWidth + x / cellSize;
            // Repeated list labels can look stationary after a whole-row
            // scroll. A fixed control must also disagree with the displacement
            // established by distinct moving features, in both directions.
            if (++errorData[cell] >= 2)
                hasCandidate = true;
        }
    }
    if (!hasCandidate)
        return {{}, height, height};
    // Join adjacent parts of outlines and glyphs, then mask each component's
    // rectangle. This includes the opaque interior of a rounded input box.
    for (int y = 0; y < gridHeight; ++y)
        for (int x = 0; x < gridWidth; ++x)
            if (errorData[y * gridWidth + x] >= 2)
                for (int row = qMax(0, y - 1); row <= qMin(gridHeight - 1, y + 1); ++row)
                    for (int column = qMax(0, x - 1); column <= qMin(gridWidth - 1, x + 1); ++column)
                        connectedData[row * gridWidth + column] = 1;
    StationaryAreas areas{{}, height, height};
    QVector<QRect> regions;
    QVector<int> pending;
    for (int index = 0; index < connected.size(); ++index) {
        if (!connectedData[index])
            continue;
        pending.clear();
        pending.append(index);
        connectedData[index] = 0;
        QRect bounds;
        int inconsistent = 0;
        while (!pending.isEmpty()) {
            const int cell = pending.takeLast();
            const int x = cell % gridWidth, y = cell / gridWidth;
            bounds = bounds.united(QRect(x * cellSize, y * cellSize, cellSize, cellSize));
            inconsistent += errorData[cell];
            for (const QPoint &next : {QPoint(x - 1, y), QPoint(x + 1, y), QPoint(x, y - 1), QPoint(x, y + 1)}) {
                if (next.x() < 0 || next.x() >= gridWidth || next.y() < 0 || next.y() >= gridHeight)
                    continue;
                const int neighbour = next.y() * gridWidth + next.x();
                if (connectedData[neighbour]) {
                    connectedData[neighbour] = 0;
                    pending.append(neighbour);
                }
            }
        }
        if (inconsistent < 8)
            continue;
        bounds = bounds.intersected(current.rect());
        regions.append(bounds);
    }
    // A thin outline may be interrupted by antialiasing over moving text.
    // Rejoin its pieces only when the enclosed rectangle is also stationary.
    for (int index = 0; index < regions.size(); ++index) {
        for (int other = index + 1; other < regions.size(); ++other) {
            const QRect joined = regions[index].united(regions[other]);
            const bool sharesColumns = qMin(regions[index].right(), regions[other].right()) >=
                qMax(regions[index].left(), regions[other].left());
            const bool sharesRows = qMin(regions[index].bottom(), regions[other].bottom()) >=
                qMax(regions[index].top(), regions[other].top());
            if ((!sharesColumns && !sharesRows) || joined.height() > height / 3)
                continue;
            const QRect interior = joined.adjusted(cellSize * 3, cellSize * 3, -cellSize * 3, -cellSize * 3);
            if (interior.isEmpty())
                continue;
            const int allowedChanges = qMax(1, ((interior.width() + 1) / 2) * ((interior.height() + 1) / 2) / 100);
            int changed = 0;
            for (int y = interior.top(); y <= interior.bottom() && changed <= allowedChanges; y += 2) {
                const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(y));
                const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
                for (int x = interior.left(); x <= interior.right(); x += 2)
                    changed += difference(a[x], b[x]) > 12;
            }
            if (changed <= allowedChanges) {
                regions[index] = joined;
                regions.removeAt(other);
                other = index;
            }
        }
    }
    for (QRect bounds : regions) {
        // Coincident strokes in repeated text can seed a stationary component.
        // Check compact components' interiors before they can move the seam.
        // Larger components can contain several joined controls with moving
        // gaps between them. Inset rounded controls to exclude their corners.
        const int inset = qMin(cellSize * 3, qMin(bounds.width(), bounds.height()) / 4);
        const QRect interior = bounds.adjusted(inset, inset, -inset, -inset);
        const int allowedChanges = qMax(1, ((interior.width() + 1) / 2) * ((interior.height() + 1) / 2) / 100);
        int changed = 0;
        for (int y = interior.top(); y <= interior.bottom() && changed <= allowedChanges; y += 2) {
            const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(y));
            const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
            for (int x = interior.left(); x <= interior.right(); x += 2)
                changed += difference(a[x], b[x]) > 12;
        }
        if ((bounds.width() < cellSize * 12 || bounds.height() < cellSize * 12) && changed > allowedChanges)
            continue;
        const bool wide = bounds.width() > (columns.last() - columns.first() + 1) / 2;
        if (wide && bounds.top() >= height / 2 && bounds.bottom() >= height * 3 / 4 && bounds.width() > 24) {
            int top = bounds.top();
            const int allowedChanges = qMax(1, (bounds.width() - 24) / 200);
            // Recover the opaque interior above a footer's visible bottom
            // border. Stop at moving text, then include its antialiased edge.
            while (top > height / 2) {
                const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(top - 1));
                const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(top - 1));
                int changed = 0;
                for (int x = bounds.left() + 12; x <= bounds.right() - 12; x += 2)
                    changed += difference(a[x], b[x]) > 12;
                if (changed > allowedChanges)
                    break;
                --top;
            }
            bounds.setTop(qMax(height / 2, top - cellSize));
        }
        if (areas.mask.isNull()) {
            areas.mask = QImage(current.size(), QImage::Format_Grayscale8);
            if (areas.mask.isNull())
                return {{}, height, height};
            areas.mask.fill(0);
        }
        for (int y = bounds.top(); y <= bounds.bottom(); ++y)
            std::fill_n(areas.mask.scanLine(y) + bounds.left(), bounds.width(), uchar(1));
        if (bounds.top() >= height / 2) {
            areas.seam = qMin(areas.seam, bounds.top());
            if (wide)
                areas.bottom = qMin(areas.bottom, bounds.top());
        }
    }
    return areas;
}

Verification verifyMatch(const QImage &previous, const QImage &current,
                         const QVector<int> &columns, const QPair<int, int> &oldRows,
                         const QPair<int, int> &newRows, const StationaryAreas &fixed, Match match) {
    int matching = 0, compared = 0, rows = 0, firstRow = -1, lastRow = -1, lastReferenceRow = -1;
    int badRows = 0, firstBadRow = -1;
    double total = 0;
    const int begin = qMax(newRows.first, oldRows.first - match.shift);
    const int end = qMin(qMin(newRows.second, fixed.bottom), qMin(oldRows.second, fixed.bottom) - match.shift);
    for (int y = begin; y < end; y += 2) {
        const auto *a = reinterpret_cast<const QRgb *>(previous.constScanLine(y + match.shift));
        const auto *old = reinterpret_cast<const QRgb *>(previous.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
        const auto *above = reinterpret_cast<const QRgb *>(current.constScanLine(y - 1));
        const auto *oldAbove = reinterpret_cast<const QRgb *>(previous.constScanLine(y + match.shift - 1));
        const auto *shifted = reinterpret_cast<const QRgb *>(current.constScanLine(y + match.shift));
        const auto *oldMask = fixed.mask.isNull() ? nullptr : fixed.mask.constScanLine(y + match.shift);
        const auto *newMask = fixed.mask.isNull() ? nullptr : fixed.mask.constScanLine(y);
        int rowCount = 0, rowMatches = 0;
        int block = -1, blockCount = 0, blockErrors = 0;
        bool badRow = false;
        for (int x : columns) {
            if (x / 32 != block) {
                badRow = badRow || (blockErrors >= 3 && blockErrors * 5 > blockCount);
                block = x / 32;
                blockCount = blockErrors = 0;
            }
            if ((oldMask && (oldMask[x] || newMask[x])) ||
                (difference(old[x], b[x]) <= 12 && difference(a[x], shifted[x]) <= 12) ||
                qMax(qMax(difference(b[x], b[x - 1]), difference(b[x], above[x])),
                     qMax(difference(a[x], a[x - 1]), difference(a[x], oldAbove[x]))) < 18)
                continue;
            const int error = difference(a[x], b[x]);
            ++blockCount;
            blockErrors += error > 24;
            rowMatches += error <= 24;
            total += qMin(180, error);
            ++rowCount;
        }
        matching += rowMatches;
        compared += rowCount;
        badRow = badRow || (blockErrors >= 3 && blockErrors * 5 > blockCount);
        if (badRow) {
            ++badRows;
            if (firstBadRow < 0)
                firstBadRow = y;
            // A small live badge may change one patch. Mismatches repeated in
            // separated text lines indicate a wrong row, even if their common
            // filename prefix makes the overall pixel agreement look high.
            if (badRows >= 4 && y - firstBadRow > 32)
                return {{match.shift, match.footer, 1000}, 0};
        }
        if (!badRow && rowCount >= 2 && double(rowMatches) / rowCount >= 0.90) {
            ++rows;
            if (firstRow < 0)
                firstRow = y;
            lastRow = y;
            if (y + qMax(0, match.shift) < fixed.seam)
                lastReferenceRow = y + qMax(0, match.shift);
        }
    }
    if (rows < 8 || compared < 64 || lastRow - firstRow < qMin(40, (end - begin) / 3) || lastReferenceRow < 0)
        return {{match.shift, match.footer, 1000}, 0};
    match.error = total / (compared * 3);
    // Any seam inside the verified moving overlap preserves the content. Using
    // its last supported row also excludes status bars that change on scroll.
    match.footer = current.height() - lastReferenceRow - 1;
    return {match, double(matching) / compared};
}

Match findMatch(const QImage &previous, const QImage &current) {
    const auto columns = movingColumns(previous, current);
    if (columns.size() < 4)
        return {};
    const auto a = describe(previous, current, columns), b = describe(current, previous, columns);
    const int height = previous.height();
    // Whitespace moves with the document. Estimate each frame's content bounds
    // independently, and search against the viewport height so a blank margin
    // in the new frame cannot hide valid overlap at the old frame's bottom.
    const auto oldRows = contentRows(a), newRows = contentRows(b);
    const int minimumOverlap = 32;
    const int maxShift = height - minimumOverlap;
    // Exact, unique row fingerprints retain narrow identifiers that the coarse
    // column grid can miss. Several separated anchors must vote for one shift,
    // followed by dense pixel verification. Repeated rows cannot cast a vote.
    const auto oldAnchors = uniqueRows(previous, a, columns);
    const auto newAnchors = uniqueRows(current, b, columns);
    struct Votes {
        int count = 0;
        int first = -1;
        int last = -1;
    };
    QVector<Votes> votes(height * 2 - 1);
    for (auto it = newAnchors.cbegin(); it != newAnchors.cend(); ++it) {
        const int oldY = oldAnchors.value(it.key(), -1), newY = it.value();
        if (oldY < 0 || newY < 0 || oldY == newY || qAbs(oldY - newY) > maxShift)
            continue;
        auto &vote = votes[oldY - newY + height - 1];
        ++vote.count;
        vote.first = vote.first < 0 ? newY : qMin(vote.first, newY);
        vote.last = qMax(vote.last, newY);
    }
    int anchorShift = 0, mostVotes = 7;
    for (int index = 0; index < votes.size(); ++index)
        if (votes[index].count > mostVotes && votes[index].last - votes[index].first >= 40) {
            mostVotes = votes[index].count;
            anchorShift = index - height + 1;
        }
    StationaryAreas fixed{{}, height, height};
    QVector<Verification> anchoredMatches;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) {
            // Preserve the ordinary match first. Masking requires additional
            // distinct anchors to keep near-identical text rows unambiguous.
            if (!anchorShift || mostVotes < 16)
                break;
            fixed = stationaryAreas(previous, current, columns, anchorShift);
            if (fixed.mask.isNull())
                break;
            anchoredMatches.clear();
        }
        QVector<Verification> exactMatches;
        for (int index = 0; index < votes.size(); ++index) {
            if (votes[index].count < 8 || votes[index].last - votes[index].first < 40)
                continue;
            const auto verified = verifyMatch(previous, current, columns, oldRows, newRows, fixed,
                                              {index - height + 1, 0, 0});
            if (verified.agreement >= 0.94 && verified.match.error <= 5.0)
                anchoredMatches.append(verified);
            if (verified.agreement >= 0.995 && verified.match.error <= 0.25)
                exactMatches.append(verified);
        }
        if (exactMatches.size() == 1) {
            const auto best = exactMatches.first().match;
            if (!attempt) {
                // Even a one-pixel move can match around a floating box. Use
                // its detected boundary for the seam on those small moves too.
                const auto overlays = stationaryAreas(previous, current, columns, best.shift);
                if (!overlays.mask.isNull()) {
                    const auto cleanSeam = verifyMatch(previous, current, columns, oldRows, newRows, overlays, best);
                    if (cleanSeam.agreement >= 0.94 && cleanSeam.match.error <= 5.0)
                        return cleanSeam.match;
                }
            }
            return best;
        }
        if (exactMatches.size() > 1)
            return {};
    }
    QVector<Match> candidates;
    candidates.reserve(qMax(0, maxShift * 2));
    QVector<int> informativeRows;
    for (int y = newRows.first; y < newRows.second; ++y)
        if (b[y].informative)
            informativeRows.append(y);
    for (int shift = -maxShift; shift <= maxShift; ++shift) {
        if (!shift)
            continue;
        const int begin = qMax(newRows.first, oldRows.first - shift);
        const int end = qMin(newRows.second, oldRows.second - shift);
        // Content bounds exclude blank margins. Their intersection may be much
        // shorter than the physical overlap while still containing enough text
        // for the dense verifier's eight independently checked scan lines.
        if (end - begin < 16)
            continue;
        const auto firstInformative = std::lower_bound(informativeRows.cbegin(), informativeRows.cend(), begin);
        const auto lastInformative = std::lower_bound(firstInformative, informativeRows.cend(), end);
        const int informativeCount = int(lastInformative - firstInformative);
        const int sampledCount = qMin(sampleColumns, informativeCount);
        if (sampledCount < 8)
            continue;
        const int first = firstInformative[0];
        const int last = firstInformative[(sampledCount - 1) * informativeCount / sampledCount];
        if (last - first < qMin(minimumOverlap / 2, (end - begin) / 3))
            continue;
        // Sample actual features instead of a regular vertical grid, which can
        // repeatedly land in blank line spacing throughout a long document.
        double errors[sampleColumns];
        for (int index = 0; index < sampledCount; ++index) {
            const int y = firstInformative[index * informativeCount / sampledCount];
            errors[index] = rowError(a[y + shift], b[y]);
        }
        std::sort(errors, errors + sampledCount);
        const int count = sampledCount - sampledCount / 10;
        double total = 0;
        for (int i = 0; i < count; ++i)
            total += errors[i];
        candidates.append({shift, height - oldRows.second, total / count});
    }
    if (candidates.isEmpty() && anchoredMatches.isEmpty())
        return {};
    std::sort(candidates.begin(), candidates.end(), [](const Match &a, const Match &b) {
        return a.error < b.error;
    });
    // Occluded rows can have a poor coarse score even when their unique
    // visible features agree. Keep those verified candidates in the ranking.
    QVector<Verification> verified = anchoredMatches;
    for (const auto &candidate : candidates) {
        if (candidate.error > candidates.first().error * 1.6 + 3.0 || verified.size() >= 12)
            break;
        const bool adjacent = std::any_of(verified.cbegin(), verified.cend(), [&](const Verification &item) {
            return qAbs(item.match.shift - candidate.shift) <= 2;
        });
        if (!adjacent)
            verified.append(verifyMatch(previous, current, columns, oldRows, newRows, fixed, candidate));
    }
    std::sort(verified.begin(), verified.end(), [](const Verification &a, const Verification &b) {
        return a.match.error < b.match.error;
    });
    const auto best = verified.first();
    if (best.agreement < 0.94 || best.match.error > 5.0)
        return {};
    if (verified.size() > 1 && verified[1].match.error < best.match.error * 1.6 + 0.75)
        return {};
    return best.match;
}

QImage sidebarBackground(const QImage &image, const QRect &area) {
    if (area.width() <= 0)
        return {};
    qint64 leastVariation = std::numeric_limits<qint64>::max();
    int selected = area.bottom();
    for (int y = qMin(image.height() - 2, area.bottom() - 1); y > area.top(); --y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const auto *above = reinterpret_cast<const QRgb *>(image.constScanLine(y - 1));
        const auto *below = reinterpret_cast<const QRgb *>(image.constScanLine(y + 1));
        qint64 variation = 0;
        for (int x = area.left(); x <= area.right(); ++x) {
            variation += difference(row[x], above[x]) + difference(row[x], below[x]);
            if (x > area.left())
                variation += difference(row[x], row[x - 1]);
        }
        if (variation < leastVariation) {
            leastVariation = variation;
            selected = y;
        }
    }
    return image.copy(area.left(), selected, area.width(), 1);
}
} // namespace

ScrollStitcher::ScrollStitcher(int maxHeight, qint64 maxPixels)
    : maxHeight_(maxHeight), maxPixels_(maxPixels) {}

void ScrollStitcher::setViewport(const QRect &viewport) {
    if (!frames_)
        viewport_ = viewport;
}

bool ScrollStitcher::sameFrame(const QImage &first, const QImage &second, const QRect &viewport) {
    if (first.isNull() || first.size() != second.size())
        return false;
    const QImage a = first.convertToFormat(QImage::Format_RGB32);
    const QImage b = second.convertToFormat(QImage::Format_RGB32);
    const QRect area = viewport.isEmpty() ? a.rect() : viewport.intersected(a.rect());
    int matching = 0, compared = 0;
    int changedRows = 0, firstChanged = -1, lastChanged = -1;
    for (int y = area.top(); y <= area.bottom(); y += qMax(1, area.height() / 180)) {
        const auto *oldPixels = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *newPixels = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        bool changed = false;
        for (int x = area.left() + 2; x < area.right() - 1; x += qMax(1, area.width() / 128)) {
            const bool equal = difference(oldPixels[x], newPixels[x]) <= 6;
            matching += equal;
            changed = changed || !equal;
            ++compared;
        }
        if (changed) {
            ++changedRows;
            if (firstChanged < 0)
                firstChanged = y;
            lastChanged = y;
        }
    }
    // Sparse identifier changes spread through a page are evidence of motion,
    // even when blank background dominates the percentage of equal pixels.
    return compared > 0 && double(matching) / compared >= 0.998 &&
        (changedRows < 3 || lastChanged - firstChanged < qMax(40, area.height() / 5));
}

bool ScrollStitcher::recoverPosition(const QImage &previous, const QImage &current,
                                    int &shift, int &tailStart) const {
    if (position_ <= 0)
        return false;
    const auto columns = movingColumns(previous, current);
    if (columns.size() < 4)
        return false;
    const auto features = describe(current, previous, columns);
    // A glyph's individual scan lines recur across thousands of similar log
    // entries. Five adjacent fingerprints distinguish their complete shapes.
    auto contextHash = [](const QVector<quint64> &hashes, int y) {
        quint64 hash = 14695981039346656037ULL;
        for (int row = y - 2; row <= y + 2; ++row) {
            hash ^= hashes[row];
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    QVector<quint64> currentHashes(current.height());
    for (int y = 0; y < current.height(); ++y)
        currentHashes[y] = rowFingerprint(reinterpret_cast<const QRgb *>(current.constScanLine(y)), columns);
    QHash<quint64, int> anchors;
    for (int y = 2; y < current.height() - 2; ++y) {
        if (!features[y].informative)
            continue;
        const quint64 hash = contextHash(currentHashes, y);
        auto found = anchors.find(hash);
        if (found == anchors.end())
            anchors.insert(hash, y);
        else
            found.value() = -1;
    }
    if (anchors.size() < 8)
        return false;
    const QRect area = viewport_.isEmpty() ? last_.rect() : viewport_;
    // Index the original strips in place. Recovery allocates row pointers and
    // fingerprints, never another full-length image or an unbounded frame log.
    QVector<const QRgb *> stored;
    stored.reserve(height_);
    for (const auto &strip : strips_)
        for (int y = 0; y < strip.height(); ++y)
            stored.append(reinterpret_cast<const QRgb *>(strip.constScanLine(y)) + area.left());
    for (int y = tailStart_; y < last_.height(); ++y)
        stored.append(reinterpret_cast<const QRgb *>(last_.constScanLine(y)) + area.left());
    QVector<quint64> storedHashes(stored.size());
    for (int y = 0; y < stored.size(); ++y)
        storedHashes[y] = rowFingerprint(stored[y], columns);
    QHash<quint64, int> locations;
    for (int y = area.top() + 2; y < stored.size() - 2; ++y) {
        const quint64 hash = contextHash(storedHashes, y);
        if (!anchors.contains(hash))
            continue;
        auto found = locations.find(hash);
        if (found == locations.end())
            locations.insert(hash, y);
        else
            found.value() = -1;
    }
    struct Support {
        int count = 0;
        int first = std::numeric_limits<int>::max();
        int last = -1;
    };
    QHash<int, Support> positions;
    for (auto it = locations.cbegin(); it != locations.cend(); ++it) {
        const int y = anchors.value(it.key());
        const int position = it.value() - area.top() - y;
        // Only retained content can reconnect a lost capture. A forward jump
        // with unseen content must keep waiting for the user to return.
        if (it.value() < 0 || y < 0 || position < 0 || position >= position_)
            continue;
        auto &support = positions[position];
        ++support.count;
        support.first = qMin(support.first, y);
        support.last = qMax(support.last, y);
    }
    const auto bounds = contentRows(features);
    int recoveredPosition = -1, recoveredTail = -1;
    for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
        if (it->count < 8 || it->last - it->first < 40)
            continue;
        // With no adjacent overlap, stationary sidebars may connect the top
        // and bottom chrome into a full-window rectangle. Limit recovery's
        // occlusion detection to the horizontal span containing moving content.
        const auto fixed = stationaryAreas(previous, current, columns, int(it.key() - position_), true,
                                           stored.constData() + it.key() + area.top());
        int compared = 0, matching = 0, goodRows = 0, firstGood = -1, lastGood = -1;
        int badRows = 0, firstBad = -1;
        bool inconsistent = false;
        qint64 total = 0;
        for (int y = bounds.first; y < qMin(bounds.second, fixed.bottom); y += 2) {
            const int sourceY = it.key() + area.top() + y;
            const auto *a = stored[sourceY];
            const auto *aboveA = stored[sourceY - 1];
            const auto *b = reinterpret_cast<const QRgb *>(current.constScanLine(y));
            const auto *aboveB = reinterpret_cast<const QRgb *>(current.constScanLine(y - 1));
            const auto *mask = fixed.mask.isNull() ? nullptr : fixed.mask.constScanLine(y);
            const int referenceY = sourceY - int(position_) - area.top();
            const auto *oldMask = fixed.mask.isNull() || referenceY < 0 || referenceY >= previous.height()
                ? nullptr : fixed.mask.constScanLine(referenceY);
            int count = 0, matches = 0, block = -1, blockCount = 0, blockErrors = 0;
            bool bad = false;
            for (int x : columns) {
                if (x / 32 != block) {
                    bad = bad || (blockErrors >= 3 && blockErrors * 5 > blockCount);
                    block = x / 32;
                    blockCount = blockErrors = 0;
                }
                if ((mask && mask[x]) || (oldMask && oldMask[x]) ||
                    qMax(qMax(difference(a[x], a[x - 1]), difference(a[x], aboveA[x])),
                         qMax(difference(b[x], b[x - 1]), difference(b[x], aboveB[x]))) < 18)
                    continue;
                const int error = difference(a[x], b[x]);
                ++blockCount;
                blockErrors += error > 24;
                matches += error <= 24;
                total += qMin(180, error);
                ++count;
            }
            compared += count;
            matching += matches;
            bad = bad || (blockErrors >= 3 && blockErrors * 5 > blockCount);
            if (bad) {
                ++badRows;
                if (firstBad < 0)
                    firstBad = y;
                if (badRows >= 4 && y - firstBad > 32) {
                    inconsistent = true;
                    break;
                }
            }
            if (!bad && count >= 2 && double(matches) / count >= 0.90 && y < fixed.seam) {
                ++goodRows;
                if (firstGood < 0)
                    firstGood = y;
                lastGood = y;
            }
        }
        if (inconsistent || goodRows < 8 ||
            lastGood - firstGood < 40 || compared < 64 || double(matching) / compared < 0.94 ||
            double(total) / (compared * 3) > 5.0)
            continue;
        if (recoveredPosition >= 0)
            return false;
        recoveredPosition = it.key();
        recoveredTail = area.top() + lastGood + 1;
    }
    if (recoveredPosition < 0)
        return false;
    shift = recoveredPosition - int(position_);
    tailStart = recoveredTail;
    return true;
}

ScrollStitcher::Result ScrollStitcher::append(const QImage &source) {
    if (source.width() < 80 || source.height() < 100 ||
        (!last_.isNull() && source.size() != last_.size()))
        return {Status::Invalid, 0};
    if (source.height() > maxHeight_ || qint64(source.width()) * source.height() > maxPixels_)
        return {Status::LimitReached, 0};
    const QImage frame = source.convertToFormat(QImage::Format_RGB32);
    if (frame.isNull())
        return {Status::Invalid, 0};
    if (!checkedFrame_.isNull() && frame == checkedFrame_)
        return checkedResult_;
    if (last_.isNull()) {
        last_ = frame;
        reference_ = frame;
        first_ = frame;
        viewport_ = viewport_.intersected(frame.rect());
        if (viewport_.width() < 80 || viewport_.height() < 100)
            viewport_ = {};
        if (!viewport_.isEmpty()) {
            leftFill_ = sidebarBackground(first_, QRect(0, viewport_.top(), viewport_.left(), viewport_.height()));
            rightFill_ = sidebarBackground(first_, QRect(viewport_.right() + 1, viewport_.top(),
                first_.width() - viewport_.right() - 1, viewport_.height()));
        }
        height_ = frame.height();
        frames_ = 1;
        return {Status::FirstFrame, 0};
    }
    const QImage previous = viewport_.isEmpty() ? reference_ : reference_.copy(viewport_);
    const QImage current = viewport_.isEmpty() ? frame : frame.copy(viewport_);
    if (previous == current)
        return {Status::Unchanged, 0};
    // A long list may differ only in a few filename digits after a whole-row
    // scroll. Try geometric matching before treating sparse changes as idle.
    Match match = findMatch(previous, current);
    auto rememberResult = [&](Status status) {
        checkedFrame_ = frame;
        checkedResult_ = {status, 0};
        return checkedResult_;
    };
    if (!match.shift) {
        int recoveredTail = 0;
        if (!recoverPosition(previous, current, match.shift, recoveredTail))
            return rememberResult(sameFrame(previous, current) ? Status::Unchanged : Status::Uncertain);
        match.footer = current.height() + (viewport_.isEmpty() ? 0 : viewport_.top()) - recoveredTail;
    }
    const qint64 nextPosition = position_ + match.shift;
    if (nextPosition <= 0) {
        // Keep the original capture as the minimum range, while tracking the
        // actual viewport above the starting position so a later return does
        // not append that content a second time.
        strips_.clear();
        last_ = first_;
        reference_ = frame;
        checkedFrame_ = {};
        position_ = nextPosition;
        tailStart_ = 0;
        height_ = first_.height();
        frames_ = 1;
        return {Status::Retracted, match.shift};
    }
    if (!viewport_.isEmpty())
        match.footer += frame.height() - viewport_.bottom() - 1;
    const qint64 nextHeight = frame.height() + nextPosition;
    if (nextHeight > maxHeight_ || qint64(frame.width()) * nextHeight > maxPixels_)
        return rememberResult(Status::LimitReached);
    // findMatch supplies an overlap seam in the earlier viewport for downward
    // motion and in the new viewport for upward motion. Convert it to a fixed
    // coordinate relative to the start of this capture before trimming tiles.
    const int nextTailStart = frame.height() - match.footer - qMax(0, match.shift);
    const qint64 seam = nextPosition + nextTailStart;
    if (nextTailStart < 0 || nextTailStart >= frame.height() || seam < 0 || seam > height_)
        return rememberResult(Status::Uncertain);
    QVector<QImage> nextStrips = strips_;
    int prefixHeight = height_ - last_.height() + tailStart_;
    if (seam > prefixHeight) {
        const QImage strip = last_.copy(0, tailStart_, last_.width(), int(seam) - prefixHeight);
        if (strip.isNull())
            return {Status::LimitReached, 0};
        // Coalesce short strips so thousands of tiny scroll steps do not grow
        // the tile list or hit a frame-count ceiling. Copies stay bounded to a
        // small tile and retain the original pixels, including after trimming.
        if (!nextStrips.isEmpty() && nextStrips.last().height() + strip.height() <= 256) {
            const QImage &previousStrip = nextStrips.last();
            QImage merged(frame.width(), previousStrip.height() + strip.height(), QImage::Format_RGB32);
            if (merged.isNull())
                return {Status::LimitReached, 0};
            std::copy_n(previousStrip.constBits(), previousStrip.bytesPerLine() * previousStrip.height(), merged.bits());
            std::copy_n(strip.constBits(), strip.bytesPerLine() * strip.height(), merged.scanLine(previousStrip.height()));
            nextStrips.last() = merged;
        } else {
            nextStrips.append(strip);
        }
    } else {
        while (prefixHeight > seam && !nextStrips.isEmpty()) {
            const int keep = nextStrips.last().height() - (prefixHeight - int(seam));
            if (keep <= 0) {
                prefixHeight -= nextStrips.last().height();
                nextStrips.removeLast();
            } else {
                const QImage strip = nextStrips.last().copy(0, 0, frame.width(), keep);
                if (strip.isNull())
                    return {Status::LimitReached, 0};
                nextStrips.last() = strip;
                prefixHeight = int(seam);
            }
        }
    }
    strips_ = std::move(nextStrips);
    tailStart_ = nextTailStart;
    last_ = frame;
    reference_ = frame;
    checkedFrame_ = {};
    position_ = nextPosition;
    const bool retracted = nextHeight < height_;
    height_ = int(nextHeight);
    frames_ = strips_.size() + 1;
    return {retracted ? Status::Retracted : Status::Appended, match.shift};
}

QSize ScrollStitcher::size() const { return QSize(last_.width(), height_); }

QImage ScrollStitcher::image() const {
    if (last_.isNull())
        return {};
    QImage output(size(), QImage::Format_RGB32);
    if (output.isNull())
        return {};
    // Scan-line copies also work beyond QPainter's raster coordinate limits.
    int row = 0;
    auto copy = [&](const QImage &image, int start) {
        for (int y = start; y < image.height(); ++y)
            std::copy_n(image.constScanLine(y), image.width() * 4, output.scanLine(row++));
    };
    for (const auto &strip : strips_)
        copy(strip, 0);
    copy(last_, tailStart_);
    QPainter painter(&output);
    paintFixedAreas(painter);
    return output;
}

void ScrollStitcher::paintFixedAreas(QPainter &painter) const {
    if (viewport_.isEmpty() || first_.isNull() || height_ == first_.height())
        return;
    const int preservedHeight = viewport_.bottom() + 1;
    const int addedHeight = height_ - first_.height();
    // Keep each stationary side pane once, extending only its background next
    // to the extra scrolling content. The window header/footer retain their
    // original positions at the top and bottom of the resulting long image.
    for (const auto &side : {QPair<int, QImage>(0, leftFill_),
                             QPair<int, QImage>(viewport_.right() + 1, rightFill_)}) {
        if (side.second.isNull())
            continue;
        const QRect top(side.first, 0, side.second.width(), preservedHeight);
        painter.drawImage(top, first_, top);
        painter.drawImage(QRect(side.first, preservedHeight, side.second.width(), addedHeight), side.second);
    }
}

QImage ScrollStitcher::preview(const QSize &bounds) const {
    if (last_.isNull() || bounds.isEmpty())
        return {};
    const QSize target = size().scaled(bounds, Qt::KeepAspectRatio);
    QImage output(target, QImage::Format_RGB32);
    if (output.isNull())
        return {};
    output.fill(Qt::white);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double scale = double(target.height()) / height_;
    int row = 0;
    for (const auto &strip : strips_) {
        painter.drawImage(QRectF(0, row * scale, target.width(), strip.height() * scale), strip);
        row += strip.height();
    }
    painter.drawImage(QRectF(0, row * scale, target.width(), (last_.height() - tailStart_) * scale),
                      last_, QRectF(0, tailStart_, last_.width(), last_.height() - tailStart_));
    painter.scale(double(target.width()) / last_.width(), scale);
    paintFixedAreas(painter);
    return output;
}
