#include "document.h"
#include <QJsonArray>
#include <QPainter>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <ReadBarcode.h>
#include <DecodeHints.h>
#include <Result.h>
#include <TextUtfEncoding.h>

QString toolName(Tool tool) {
    static const QStringList names{QStringLiteral("选择"),   QStringLiteral("矩形"), QStringLiteral("椭圆"),
                                   QStringLiteral("圆角"),   QStringLiteral("直线"), QStringLiteral("箭头"),
                                   QStringLiteral("折线"),   QStringLiteral("画笔"), QString(),
                                   QStringLiteral("马赛克"), QStringLiteral("模糊"), QStringLiteral("橡皮"),
                                   QStringLiteral("文字"),   QStringLiteral("序号"), QStringLiteral("放大")};
    return names.value(int(tool));
}
QString toolKey(Tool tool) {
    return QString::number(int(tool));
}
bool isLineTool(Tool tool) {
    return tool == Tool::Line || tool == Tool::Arrow || tool == Tool::Polyline;
}
bool isEffectTool(Tool tool) {
    return tool == Tool::Mosaic || tool == Tool::Blur || tool == Tool::Eraser;
}

QRectF Annotation::bounds() const {
    QRectF result = rect.normalized();
    if (!points.isEmpty()) {
        qreal left = points.first().x(), top = points.first().y(), right = left, bottom = top;
        for (const auto &point : points) {
            left = qMin(left, point.x());
            right = qMax(right, point.x());
            top = qMin(top, point.y());
            bottom = qMax(bottom, point.y());
        }
        result = QRectF(QPointF(left, top), QPointF(right, bottom));
    }
    return result.adjusted(-width / 2 - 2, -width / 2 - 2, width / 2 + 2, width / 2 + 2);
}
QJsonObject Annotation::toJson() const {
    QJsonArray coordinates;
    for (const auto &point : points)
        coordinates.append(QJsonArray{point.x(), point.y()});
    return {{"tool", int(tool)},
            {"rect", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()}},
            {"points", coordinates},
            {"color", color.name(QColor::HexArgb)},
            {"fill", fill.name(QColor::HexArgb)},
            {"outline", outline.name(QColor::HexArgb)},
            {"width", width},
            {"rotation", rotation},
            {"fontSize", fontSize},
            {"outlineWidth", outlineWidth},
            {"lineStyle", lineStyle},
            {"shape", shape},
            {"arrowStyle", arrowStyle},
            {"number", number},
            {"text", text}};
}
Annotation Annotation::fromJson(const QJsonObject &object) {
    Annotation item;
    const int storedTool = qBound(0, object["tool"].toInt(1), int(Tool::Magnify));
    item.tool = storedTool == 8 ? Tool::Pen : Tool(storedTool);
    const auto rect = object["rect"].toArray();
    item.rect = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble());
    for (const auto &point : object["points"].toArray()) {
        const auto pair = point.toArray();
        item.points.append(QPointF(pair[0].toDouble(), pair[1].toDouble()));
    }
    item.color = QColor(object["color"].toString("#ffff536b"));
    item.fill = QColor(object["fill"].toString("#00000000"));
    item.outline = QColor(object["outline"].toString("#ff000000"));
    item.width = qBound(1.0, object["width"].toDouble(3), 100.0);
    item.rotation = object["rotation"].toDouble();
    item.fontSize = qBound(6.0, object["fontSize"].toDouble(24), 300.0);
    item.outlineWidth = qBound(0.0, object["outlineWidth"].toDouble(), 20.0);
    item.lineStyle = qBound(1, object["lineStyle"].toInt(1), 3);
    item.shape = qBound(0, object["shape"].toInt(), 3);
    item.arrowStyle = qBound(0, object["arrowStyle"].toInt(), 3);
    item.number = object["number"].toInt(1);
    item.text = object["text"].toString();
    if (storedTool == 8) {
        // Convert retired highlighter records to ordinary strokes or filled shapes.
        item.lineStyle = int(Qt::SolidLine);
        if (item.shape == 3 && !item.points.isEmpty()) {
            if (item.points.size() == 1)
                item.points.append(item.points.first());
        } else {
            const QRectF original = item.rect.normalized();
            item.tool = item.shape == 1 || item.shape == 2 ? Tool::Ellipse : Tool::Rectangle;
            item.fill = item.color;
            item.color = Qt::transparent;
            item.rect = original;
            item.points.clear();
            if (item.shape == 2) {
                const double size = qMin(original.width(), original.height());
                item.rect.setSize(QSizeF(size, size));
                QTransform transform;
                transform.translate(original.center().x(), original.center().y());
                transform.rotate(item.rotation);
                transform.translate(-original.center().x(), -original.center().y());
                item.rect.translate(transform.map(item.rect.center()) - item.rect.center());
            }
        }
        item.shape = 0;
    }
    return item;
}

QImage gaussianBlur(const QImage &source, int radius) {
    if (source.isNull())
        return {};
    radius = qBound(1, radius, 32);
    const QImage input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QVector<double> weights(2 * radius + 1);
    const double sigma = qMax(1.0, radius / 2.5);
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        weights[i + radius] = qExp(-(i * i) / (2 * sigma * sigma));
        sum += weights[i + radius];
    }
    for (auto &weight : weights)
        weight /= sum;
    QImage horizontal(input.size(), input.format()), output(input.size(), input.format());
    auto pass = [&](const QImage &from, QImage &to, bool vertical) {
        for (int y = 0; y < from.height(); ++y) {
            auto *destination = reinterpret_cast<QRgb *>(to.scanLine(y));
            for (int x = 0; x < from.width(); ++x) {
                double red = 0, green = 0, blue = 0, alpha = 0;
                for (int k = -radius; k <= radius; ++k) {
                    const int xx = vertical ? x : qBound(0, x + k, from.width() - 1);
                    const int yy = vertical ? qBound(0, y + k, from.height() - 1) : y;
                    const QRgb pixel = reinterpret_cast<const QRgb *>(from.constScanLine(yy))[xx];
                    const double weight = weights[k + radius];
                    red += qRed(pixel) * weight;
                    green += qGreen(pixel) * weight;
                    blue += qBlue(pixel) * weight;
                    alpha += qAlpha(pixel) * weight;
                }
                destination[x] = qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
            }
        }
    };
    pass(input, horizontal, false);
    pass(horizontal, output, true);
    return output;
}

namespace {
QPainterPath effectPath(const Annotation &item) {
    QPainterPath path;
    if (item.shape == 3 && !item.points.isEmpty()) {
        path.moveTo(item.points.first());
        for (int i = 1; i < item.points.size(); ++i)
            path.lineTo(item.points[i]);
        QPainterPathStroker stroke;
        stroke.setWidth(item.width);
        stroke.setCapStyle(Qt::RoundCap);
        stroke.setJoinStyle(Qt::RoundJoin);
        QPainterPath result = stroke.createStroke(path);
        result.setFillRule(Qt::WindingFill);
        result.addEllipse(item.points.first(), item.width / 2, item.width / 2);
        return result;
    }
    QRectF rect = item.rect.normalized();
    if (item.shape == 2) {
        const qreal size = qMin(rect.width(), rect.height());
        rect.setSize(QSizeF(size, size));
    }
    if (item.shape == 1 || item.shape == 2)
        path.addEllipse(rect);
    else
        path.addRect(rect);
    return path;
}
void arrowHead(QPainter &painter, const QPointF &from, const QPointF &to, const Annotation &item) {
    const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    const double length = qMax(12.0, item.width * 4);
    const QPointF a = to - QPointF(std::cos(angle - M_PI / 6) * length, std::sin(angle - M_PI / 6) * length);
    const QPointF b = to - QPointF(std::cos(angle + M_PI / 6) * length, std::sin(angle + M_PI / 6) * length);
    if (item.arrowStyle == 3) {
        painter.setBrush(item.color);
        painter.drawPolygon(QPolygonF(QVector<QPointF>{to, a, b}));
    } else {
        painter.drawLine(to, a);
        painter.drawLine(to, b);
    }
}
} // namespace

QImage Document::render(bool frame) const {
    if (image.isNull())
        return {};
    QImage canvas = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (const auto &item : annotations) {
        // Effects sample the already-composited image and never mutate the original source.
        const bool effect =
            item.tool == Tool::Mosaic || item.tool == Tool::Blur || item.tool == Tool::Magnify;
        const QRectF rect = item.rect.normalized();
        QTransform transform;
        transform.translate(rect.center().x(), rect.center().y());
        transform.rotate(item.rotation);
        transform.translate(-rect.center().x(), -rect.center().y());
        const QRect effectRegion = transform.map(effectPath(item)).boundingRect().toAlignedRect()
                                       .intersected(canvas.rect());
        QRect sampleRegion = effectRegion;
        if (item.tool == Tool::Magnify && !effectRegion.isEmpty()) {
            const QSize size(qMax(1, effectRegion.width() / 2), qMax(1, effectRegion.height() / 2));
            sampleRegion = QRect(effectRegion.center() - QPoint(size.width() / 2, size.height() / 2), size);
        }
        const QImage before = effect && !sampleRegion.isEmpty() ? canvas.copy(sampleRegion) : QImage();
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(
            QPen(item.color, item.width, Qt::PenStyle(item.lineStyle), Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(item.fill);
        painter.setTransform(transform);
        switch (item.tool) {
        case Tool::Rectangle:
            painter.drawRect(rect);
            break;
        case Tool::Ellipse:
            painter.drawEllipse(rect);
            break;
        case Tool::RoundedRectangle:
            painter.drawRoundedRect(rect, 14, 14);
            break;
        case Tool::Line:
        case Tool::Arrow:
        case Tool::Polyline:
        case Tool::Pen: {
            if (item.points.isEmpty())
                break;
            if (item.tool == Tool::Pen && std::all_of(item.points.cbegin(), item.points.cend(),
                [&](const QPointF &point) { return point == item.points.first(); })) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(item.color);
                painter.drawEllipse(item.points.first(), item.width / 2, item.width / 2);
                break;
            }
            if (item.points.size() < 2)
                break;
            painter.drawPolyline(item.points.constData(), item.points.size());
            if (item.tool == Tool::Arrow) {
                if (item.arrowStyle != 2)
                    arrowHead(painter, item.points[item.points.size() - 2], item.points.last(), item);
                if (item.arrowStyle == 1 || item.arrowStyle == 2)
                    arrowHead(painter, item.points[1], item.points.first(), item);
            }
            break;
        }
        case Tool::Eraser:
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.fillPath(effectPath(item), Qt::transparent);
            break;
        case Tool::Mosaic:
        case Tool::Blur:
        case Tool::Magnify: {
            const QRect region = effectRegion;
            if (region.isEmpty())
                break;
            QImage patch = before;
            if (item.tool == Tool::Mosaic) {
                const int block = qMax(3, qRound(item.width * 3));
                patch = patch
                            .scaled(qMax(1, patch.width() / block), qMax(1, patch.height() / block),
                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .scaled(region.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            } else if (item.tool == Tool::Blur)
                patch = gaussianBlur(patch, qRound(item.width * 2));
            else
                patch = patch.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.setClipPath(effectPath(item));
            painter.resetTransform();
            painter.drawImage(region.topLeft(), patch);
            if (item.tool == Tool::Magnify) {
                painter.setTransform(transform);
                painter.drawPath(effectPath(item));
            }
            break;
        }
        case Tool::Text: {
            painter.fillRect(rect, item.fill);
            QFont font(QStringLiteral("Microsoft YaHei UI"));
            font.setPixelSize(qRound(item.fontSize));
            QFontMetricsF metrics(font);
            QPainterPath path;
            qreal baseline = rect.top() + metrics.ascent() + 4;
            for (const QString &line : item.text.split('\n')) {
                path.addText(QPointF(rect.left() + 5, baseline), font, line);
                baseline += metrics.height();
            }
            painter.setPen(item.outlineWidth > 0 ? QPen(item.outline, item.outlineWidth, Qt::SolidLine,
                                                        Qt::RoundCap, Qt::RoundJoin)
                                                 : QPen(Qt::NoPen));
            painter.setBrush(item.color);
            painter.drawPath(path);
            break;
        }
        case Tool::Number: {
            painter.setBrush(item.color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(rect);
            QFont font;
            font.setPixelSize(qRound(rect.height() * 0.55));
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(Qt::white);
            painter.drawText(rect, Qt::AlignCenter, QString::number(item.number));
            break;
        }
        case Tool::Select:
            break;
        }
    }
    if (cornerRadius > 0) {
        QImage rounded(canvas.size(), canvas.format());
        rounded.fill(Qt::transparent);
        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(canvas.rect()), cornerRadius, cornerRadius);
        painter.setClipPath(clip);
        painter.drawImage(0, 0, canvas);
        painter.end();
        canvas = rounded;
    }
    if (frame) {
        QImage result(canvas.width() + 48, canvas.height() + 48, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int i = 20; i > 0; --i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 3));
            painter.drawRoundedRect(QRectF(24 - i, 28 - i, canvas.width() + 2 * i, canvas.height() + 2 * i),
                                    i, i);
        }
        painter.fillRect(QRect(23, 23, canvas.width() + 2, canvas.height() + 2), QColor("#718096"));
        painter.drawImage(24, 24, canvas);
        painter.end();
        return result;
    }
    return canvas;
}

QJsonObject Document::toJson() const {
    QJsonArray items;
    for (const auto &item : annotations)
        items.append(item.toJson());
    QJsonArray elements;
    for (const auto &rect : detectedElements)
        elements.append(QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()});
    return {{"version", 1}, {"radius", cornerRadius}, {"annotations", items}, {"elements", elements},
            {"sourceRect", QJsonArray{sourceRect.x(), sourceRect.y(), sourceRect.width(), sourceRect.height()}}};
}
Document Document::fromJson(const QJsonObject &object, const QImage &image) {
    Document document;
    document.image = image;
    document.cornerRadius = qMax(0, object["radius"].toInt());
    const auto source = object["sourceRect"].toArray();
    document.sourceRect = QRect(source[0].toInt(), source[1].toInt(), source[2].toInt(), source[3].toInt());
    for (const auto &item : object["annotations"].toArray())
        document.annotations.append(Annotation::fromJson(item.toObject()));
    for (const auto &item : object["elements"].toArray()) {
        const auto rect = item.toArray();
        const QRect element(rect[0].toInt(), rect[1].toInt(), rect[2].toInt(), rect[3].toInt());
        if (image.rect().contains(element))
            document.detectedElements.append(element);
    }
    return document;
}
QString colorText(const QColor &color, const QString &format, bool uppercase) {
    if (format == "RGB")
        return QString("RGB(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
    if (format == "HSV")
        return QString("HSV(%1, %2%, %3%)")
            .arg(qMax(0, color.hue()))
            .arg(qRound(color.saturationF() * 100))
            .arg(qRound(color.valueF() * 100));
    return uppercase ? color.name().toUpper() : color.name();
}
QString decodeCodes(const QImage &source) {
    if (source.isNull())
        return {};
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    const ZXing::ImageView view(image.constBits(), image.width(), image.height(), ZXing::ImageFormat::Lum,
                                image.bytesPerLine());
    ZXing::DecodeHints hints;
    hints.setTryHarder(true).setTryRotate(true).setTryDownscale(true);
    const auto results = ZXing::ReadBarcodes(view, hints);
    QStringList messages;
    for (const auto &result : results)
        if (result.isValid()) {
            messages.append(QString::fromStdString(ZXing::ToString(result.format())) + "\n" +
                            QString::fromStdString(result.text()));
        }
    return messages.join("\n\n");
}
