#include "application.h"
#include "appearance.h"
#include <QtWidgets>
#include <QtTest>
#include <QtConcurrent>
#include <MultiFormatWriter.h>
#include <BitMatrix.h>
#include <windows.h>
#include <shellapi.h>

namespace {
Document sampleDocument() {
    Document document;
    document.image = QImage(880, 520, QImage::Format_RGB32);
    QPainter painter(&document.image);
    painter.fillRect(document.image.rect(), QColor("#f1f5f9"));
    painter.fillRect(0, 0, 880, 60, QColor("#1c2c40"));
    painter.setPen(Qt::white);
    QFont heading("Microsoft YaHei UI", 18);
    heading.setPixelSize(24);
    painter.setFont(heading);
    painter.drawText(32, 39, QStringLiteral("项目看板  /  EasySnip"));
    painter.setPen(QColor("#15243a"));
    heading.setPixelSize(30);
    painter.setFont(heading);
    painter.drawText(40, 124, QStringLiteral("把重点留在画面里"));
    QFont body("Microsoft YaHei UI", 12);
    body.setPixelSize(16);
    painter.setFont(body);
    painter.setPen(QColor("#52667e"));
    painter.drawText(40, 161, QStringLiteral("清晰记录每一个想法，标注细节，随时回看。"));
    for (int i = 0; i < 3; ++i) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRect(40 + i * 278, 205, 252, 240), 12, 12);
        painter.setPen(QColor("#1b304a"));
        painter.drawText(60 + i * 278, 245,
                         QStringList{QStringLiteral("需求整理"), QStringLiteral("设计与实现"),
                                     QStringLiteral("交付验证")}[i]);
        painter.setPen(QColor("#6a7e97"));
        painter.drawText(QRect(60 + i * 278, 270, 212, 130), Qt::TextWordWrap,
                         QStringLiteral("整理截图与参考资料\n标记重要信息\n保存并回看历史"));
        painter.setBrush(QColor("#d8f3e9"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRect(60 + i * 278, 390, 100, 28), 8, 8);
        painter.setPen(QColor("#16775b"));
        painter.drawText(QRect(60 + i * 278, 390, 100, 28), Qt::AlignCenter, QStringLiteral("进行中"));
    }
    painter.end();
    return document;
}
} // namespace

int runSelfTests(const QString &directory, bool native) {
    QDir().mkpath(directory);
    const QPoint cursorBackup = QCursor::pos();
    // Qt's Windows offscreen platform does not enumerate installed system fonts.
    if (QGuiApplication::platformName() == "offscreen") {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    }
    QTemporaryDir temporary;
    auto *clipboardBackup = new QMimeData;
    if (const auto *original = QApplication::clipboard()->mimeData()) {
        for (const auto &format : original->formats())
            clipboardBackup->setData(format, original->data(format));
        if (original->hasImage())
            clipboardBackup->setImageData(original->imageData());
        if (original->hasUrls())
            clipboardBackup->setUrls(original->urls());
    }
    Settings settings(temporary.path());
    settings.set("hotkeys/enabled", false);
    settings.set("capture/superDrag", false);
    settings.set("magnifier/enabled", false);
    settings.set("appearance/uiScale", 1.0);
    applyAppearance(settings);
    QJsonArray checks;
    int failures = 0;
    auto check = [&](const QString &name, bool result) {
        checks.append(QJsonObject{{"test", name}, {"passed", result}});
        if (!result)
            ++failures;
        QTextStream(stdout) << (result ? "PASS " : "FAIL ") << name << '\n';
    };
    auto document = sampleDocument();
    Annotation rectangle;
    rectangle.tool = Tool::RoundedRectangle;
    rectangle.rect = QRectF(309, 194, 269, 265);
    rectangle.color = QColor("#ef476f");
    rectangle.width = 4;
    document.annotations.append(rectangle);
    Annotation arrow;
    arrow.tool = Tool::Arrow;
    arrow.rect = QRectF(520, 130, 110, 100);
    arrow.points = {QPointF(630, 130), QPointF(550, 211)};
    arrow.color = QColor("#ef476f");
    arrow.width = 5;
    document.annotations.append(arrow);
    Annotation number;
    number.tool = Tool::Number;
    number.rect = QRectF(300, 183, 34, 34);
    number.number = 1;
    number.color = QColor("#ef476f");
    document.annotations.append(number);
    const auto rendered = document.render();
    check("render dimensions", rendered.size() == document.image.size());
    const auto restored = Document::fromJson(document.toJson(), document.image);
    check("annotation JSON round trip",
          restored.toJson() == document.toJson() && restored.render() == rendered);
    auto rounded = document;
    rounded.cornerRadius = 28;
    check("rounded corner alpha",
          qAlpha(rounded.render().pixel(0, 0)) == 0 && qAlpha(rounded.render().pixel(440, 260)) == 255);
    check("frame output size", document.render(true).size() == document.image.size() + QSize(48, 48));
    auto erased = document;
    Annotation eraser;
    eraser.tool = Tool::Eraser;
    eraser.rect = QRectF(10, 10, 50, 50);
    erased.annotations.append(eraser);
    check("eraser preserves original screenshot pixels", erased.render().pixelColor(30, 30) == erased.image.pixelColor(30, 30));
    {
        Document source;
        source.image = QImage(120, 100, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 120; ++x)
                source.image.setPixelColor(x, y, QColor(x * 2, y * 2, (x + y) % 2 ? 230 : 40));
        Annotation mark;
        mark.rect = QRectF(10, 10, 100, 80);
        mark.color = Qt::red;
        mark.fill = Qt::red;
        mark.width = 10;
        mark.text = "Text";
        Annotation erase;
        erase.tool = Tool::Eraser;
        erase.rect = source.image.rect();
        for (Tool tool : {Tool::Rectangle, Tool::Ellipse, Tool::RoundedRectangle, Tool::Line, Tool::Arrow,
                          Tool::Polyline, Tool::Pen, Tool::Text, Tool::Number, Tool::Mosaic, Tool::Blur, Tool::Magnify}) {
            Document modified = source;
            mark.tool = tool;
            mark.points = isLineTool(tool) || tool == Tool::Pen ? QVector<QPointF>{{20, 20}, {90, 70}} : QVector<QPointF>();
            modified.annotations = {mark};
            const QImage before = modified.render();
            modified.annotations.append(erase);
            check("eraser restores source under tool " + toolKey(tool), before != source.image && modified.render() == source.image);
        }
        mark.tool = Tool::Rectangle;
        mark.points.clear();
        source.annotations = {mark};
        const QImage marked = source.render();
        erase.shape = 3;
        erase.width = 16;
        erase.points = {{20, 40}, {90, 40}};
        source.annotations.append(erase);
        const QImage partial = source.render();
        check("eraser restores only its brush path", partial.pixelColor(60, 40) == source.image.pixelColor(60, 40) &&
              partial.pixelColor(60, 70) == marked.pixelColor(60, 70));
        check("eraser history JSON retains restored pixels", Document::fromJson(source.toJson(), source.image).render() == partial);
        source.annotations.last().shape = 0;
        source.annotations.last().points.clear();
        source.annotations.last().rect = QRectF(40, 20, 20, 60);
        source.annotations.last().rotation = 90;
        const QImage rotated = source.render();
        check("rotated eraser samples unrotated source coordinates", rotated.pixelColor(70, 50) == source.image.pixelColor(70, 50) &&
              rotated.pixelColor(50, 25) == marked.pixelColor(50, 25));
        Document transparent = source;
        transparent.image.fill(QColor(50, 100, 150, 100));
        transparent.image.setPixelColor(60, 40, Qt::transparent);
        erase.shape = 0;
        erase.points.clear();
        transparent.annotations = {mark, erase};
        check("eraser preserves original transparency", transparent.render() == transparent.image);
        source.annotations = {mark};
        Settings eraseSettings(temporary.path() + "/eraser");
        AnnotationCanvas canvas(source, eraseSettings);
        canvas.setAttribute(Qt::WA_DontShowOnScreen);
        canvas.show();
        canvas.setTool(Tool::Eraser);
        check("eraser defaults to freehand brush", canvas.style().shape == 3);
        auto eraseStyle = canvas.style();
        eraseStyle.width = 20;
        canvas.setStyle(eraseStyle);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, {20, 40});
        QMouseEvent stroke(QEvent::MouseMove, QPointF(90, 40), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &stroke);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, {90, 40});
        const QImage after = canvas.document().render();
        check("eraser starts over existing filled annotations", canvas.document().annotations.size() == 2 &&
              after.pixelColor(60, 40) == source.image.pixelColor(60, 40) && canvas.selectedIndex() == -1);
        canvas.undo();
        check("undo eraser restores annotations", canvas.document().render() == marked);
        canvas.redo();
        check("redo eraser restores stroke without selecting mask", canvas.document().render() == after && canvas.selectedIndex() == -1);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, {40, 70});
        check("eraser supports repeated strokes over annotations", canvas.document().annotations.size() == 3 &&
              canvas.document().render().pixelColor(40, 70) == source.image.pixelColor(40, 70));
        QSignalSpy eraserCopy(&canvas, &AnnotationCanvas::imageCopyRequested);
        QTest::mouseDClick(&canvas, Qt::LeftButton, {}, {70, 70});
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, {70, 70});
        check("eraser double click keeps editing instead of outputting", eraserCopy.isEmpty() &&
              canvas.document().render().pixelColor(70, 70) == source.image.pixelColor(70, 70));
        const int beforePen = canvas.document().annotations.size();
        canvas.setTool(Tool::Pen);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, {60, 40});
        check("new annotations can be drawn over erased areas", canvas.document().annotations.size() == beforePen + 1 &&
              canvas.document().annotations.last().tool == Tool::Pen &&
              canvas.document().render().pixelColor(60, 40) != source.image.pixelColor(60, 40));
        Library erasedHistory(eraseSettings);
        const QString erasedId = erasedHistory.add(canvas.document());
        check("eraser survives history save and reload", !erasedId.isEmpty() &&
              erasedHistory.document(erasedId).render() == canvas.document().render());
        canvas.document().render().save(QDir(directory).filePath("eraser-restores-source.png"));
        canvas.hide();
    }

    {
        check("remaining tool serialization IDs stay compatible", int(Tool::Pen) == 7 &&
              int(Tool::Mosaic) == 9 && int(Tool::Blur) == 10 && int(Tool::Eraser) == 11 &&
              int(Tool::Text) == 12 && int(Tool::Number) == 13 && int(Tool::Magnify) == 14 &&
              toolName(Tool(8)).isEmpty());
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QColor blended(255, 255, 153);
        for (int shape = 0; shape < 4; ++shape) {
            QJsonObject legacy{{"tool", 8}, {"shape", shape}, {"color", "#66ffff00"},
                {"width", 12}, {"lineStyle", int(Qt::DashLine)},
                {"rect", QJsonArray{20, 20, 60, 40}}};
            if (shape == 3) {
                legacy["rect"] = QJsonArray{20, 50, 60, 0};
                legacy["points"] = QJsonArray{QJsonArray{20, 50}, QJsonArray{80, 50}};
            }
            const auto item = Annotation::fromJson(legacy);
            const Tool expected = shape == 3 ? Tool::Pen : shape == 0 ? Tool::Rectangle : Tool::Ellipse;
            const QString suffix = QString::number(shape);
            check("legacy marker converts to an ordinary tool " + suffix,
                  item.tool == expected && item.toJson()["tool"].toInt() != 8 &&
                  (shape == 3 ? item.color.alpha() == 102 && item.lineStyle == int(Qt::SolidLine)
                              : item.color.alpha() == 0 && item.fill == QColor("#66ffff00")));
            Document converted;
            converted.image = image;
            converted.annotations = {item};
            const QImage rendered = converted.render();
            check("legacy marker retains visible color and alpha " + suffix,
                  rendered.pixelColor(shape == 3 ? QPoint(50, 50) : QPoint(40, 40)) == blended &&
                  rendered.pixelColor(10, 10) == Qt::white);
            check("converted marker round trip " + suffix,
                  Document::fromJson(converted.toJson(), image).render() == rendered);
        }
        QJsonObject circle{{"tool", 8}, {"shape", 2}, {"color", "#66ffff00"},
                           {"rotation", 90}, {"rect", QJsonArray{20, 20, 60, 40}}};
        const auto rotated = Annotation::fromJson(circle);
        check("legacy circle retains its rotated position", rotated.tool == Tool::Ellipse &&
              rotated.rect == QRectF(30, 10, 40, 40) && rotated.rotation == 90);
        Document circleDocument;
        circleDocument.image = image;
        circleDocument.annotations = {rotated};
        check("legacy rotated circle remains visible in the correct region",
              circleDocument.render().pixelColor(50, 30) == blended && circleDocument.render().pixelColor(25, 40) == Qt::white);
        QJsonObject dot{{"tool", 8}, {"shape", 3}, {"color", "#66ffff00"}, {"width", 12},
                        {"rect", QJsonArray{50, 50, 1, 1}}, {"points", QJsonArray{QJsonArray{50, 50}}}};
        Document dotDocument;
        dotDocument.image = image;
        dotDocument.annotations = {Annotation::fromJson(dot)};
        check("legacy single point marker remains visible", dotDocument.render().pixelColor(50, 50) == blended);
    }
    QImage impulse(31, 31, QImage::Format_ARGB32);
    impulse.fill(Qt::black);
    impulse.setPixelColor(15, 15, Qt::white);
    const auto blurred = gaussianBlur(impulse, 5);
    check("Gaussian spatial spread", qRed(blurred.pixel(15, 15)) < 255 && qRed(blurred.pixel(16, 15)) > 0 &&
                                         qRed(blurred.pixel(0, 0)) == 0);
    check("HEX color formatting", colorText(QColor(171, 205, 239), "HEX", true) == "#ABCDEF");
    check("RGB color formatting", colorText(QColor(1, 2, 3), "RGB", true) == "RGB(1, 2, 3)");
    check("process exclusion matching", Native::matchesProcess("C:/Apps/game.exe", {"GAME.EXE"}) &&
                                            !Native::matchesProcess("C:/Apps/editor.exe", {"game.exe"}));
    for (const auto format :
         {ZXing::BarcodeFormat::QRCode, ZXing::BarcodeFormat::Code128, ZXing::BarcodeFormat::EAN13}) {
        const std::string content =
            format == ZXing::BarcodeFormat::EAN13 ? "5901234123457" : "EasySnip-Qt-5.14.2";
        const auto matrix = ZXing::MultiFormatWriter(format).setMargin(12).encode(content, 500, 250);
        QImage code(matrix.width(), matrix.height(), QImage::Format_RGB32);
        code.fill(Qt::white);
        for (int y = 0; y < matrix.height(); ++y)
            for (int x = 0; x < matrix.width(); ++x)
                if (matrix.get(x, y))
                    code.setPixelColor(x, y, Qt::black);
        check("barcode " + QString::fromStdString(ZXing::ToString(format)),
              decodeCodes(code).contains(QString::fromStdString(content)));
    }
    QImage blank(100, 100, QImage::Format_RGB32);
    blank.fill(Qt::white);
    check("blank barcode input", decodeCodes(blank).isEmpty() && decodeCodes({}).isEmpty());
    Library library(settings);
    const QString id = library.add(document);
    check("atomic history write", !id.isEmpty() && library.document(id).render() == rendered);
    auto record = library.record(id);
    record.title = QStringLiteral("截图测试");
    library.updateRecord(record);
    Library reloaded(settings);
    check("persistent screenshot history", reloaded.record(id).title == record.title &&
          reloaded.document(id).annotations.size() == 3);
    QJsonObject legacyRecord = record.toJson();
    legacyRecord["pinned"] = true;
    legacyRecord["history"] = false;
    legacyRecord["closed"] = false;
    legacyRecord["group"] = QStringLiteral("旧分组");
    legacyRecord["geometry"] = QJsonArray{-500, 120, 500, 300};
    const Record migrated = Record::fromJson(legacyRecord);
    check("legacy image metadata becomes screenshot history", migrated.id == record.id &&
          migrated.document == record.document && !migrated.toJson().contains("pinned"));
    const QString backup = temporary.path() + "/test.esbackup";
    check("backup export", library.exportBackup(backup));
    const int count = library.records().size();
    check("backup import", library.importBackup(backup) && library.records().size() == count * 2);
    {
        QFile legacyBackup(temporary.path() + "/legacy.esbackup");
        legacyBackup.open(QIODevice::WriteOnly);
        QDataStream stream(&legacyBackup);
        stream.setVersion(QDataStream::Qt_5_14);
        QByteArray pixels;
        QBuffer buffer(&pixels);
        buffer.open(QIODevice::WriteOnly);
        document.image.save(&buffer, "PNG");
        const QJsonObject header{{"count", 1}, {"groups", QJsonArray{QStringLiteral("旧分组")}}};
        stream << quint32(0x45534e50) << quint32(2) << QJsonDocument(header).toJson()
               << QJsonDocument(legacyRecord).toJson() << pixels;
        legacyBackup.close();
        Settings importSettings(temporary.path() + "/legacy-import");
        Library legacyLibrary(importSettings);
        check("legacy archive imports as editable screenshot history", legacyLibrary.importBackup(legacyBackup.fileName()) &&
              legacyLibrary.records().size() == 1 &&
              legacyLibrary.document(legacyLibrary.records().first().id).render() == document.render());
    }
    QFile invalid(temporary.path() + "/invalid.esbackup");
    invalid.open(QIODevice::WriteOnly);
    QDataStream invalidStream(&invalid);
    invalidStream.setVersion(QDataStream::Qt_5_14);
    invalidStream << quint32(0x45534e50) << quint32(2) << quint32(0x7fffffff);
    invalid.close();
    const int previousCount = library.records().size();
    check("oversized archive header rejected",
          !library.importBackup(invalid.fileName()) && library.records().size() == previousCount);
    settings.set("history/limit", 0);
    library.prune();
    check("history limit removes expired records", library.records().isEmpty());
    Library afterPrune(settings);
    check("history pruning persists", afterPrune.records().isEmpty());
    settings.set("history/limit", 100);
    AnnotationCanvas canvas(sampleDocument(), settings);
    check("canvas defaults to selection mode", canvas.tool() == Tool::Select);
    canvas.setTool(Tool::Rectangle);
    canvas.setAttribute(Qt::WA_DontShowOnScreen);
    canvas.show();
    canvas.setZoom(1);
    QCoreApplication::processEvents();
    QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(40, 40));
    QMouseEvent drag(QEvent::MouseMove, QPointF(200, 130), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &drag);
    QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(200, 130));
    check("canvas rectangle input", canvas.document().annotations.size() == 1 &&
                                        canvas.document().annotations.first().rect.width() > 100);
    canvas.undo();
    check("canvas undo", canvas.document().annotations.isEmpty());
    canvas.redo();
    check("canvas redo", canvas.document().annotations.size() == 1);
    canvas.setTool(Tool::Select);
    QTest::mouseClick(&canvas, Qt::LeftButton, {}, QPoint(100, 80));
    canvas.copyObject();
    canvas.pasteObject();
    check("object copy paste", canvas.document().annotations.size() == 2);
    canvas.deleteObject();
    check("object deletion", canvas.document().annotations.size() == 1);
    canvas.hide();
    Document matching = sampleDocument();
    matching.detectedElements = {QRect(40, 40, 100, 70)};
    AnnotationCanvas matchingCanvas(matching, settings);
    matchingCanvas.setTool(Tool::Rectangle);
    matchingCanvas.setAttribute(Qt::WA_DontShowOnScreen);
    matchingCanvas.show();
    QTest::mouseClick(&matchingCanvas, Qt::LeftButton, Qt::ControlModifier, QPoint(60, 60));
    check("annotation matches detected element",
          matchingCanvas.document().annotations.size() == 1 &&
              matchingCanvas.document().annotations.first().rect == QRectF(40, 40, 100, 70));
    matchingCanvas.hide();
    {
        check("F1 capture default", settings.shortcut("capture") == "F1");
        settings.set("hotkeys/capture", "Ctrl+F8");
        check("custom shortcut preserved", settings.shortcut("capture") == "Ctrl+F8");
        settings.set("hotkeys/ignored/capture", "game*.exe;C:/Apps/private.exe");
        check("per-command process exclusion", settings.ignoresShortcut("capture", "C:/Games/GameClient.exe") &&
              !settings.ignoresShortcut("last", "C:/Games/GameClient.exe"));
        settings.set("hotkeys/ignored", "global.exe");
        check("legacy global exclusion preserved", settings.ignoresShortcut("last", "C:/Apps/global.exe"));
        settings.set("hotkeys/ignored", "");
        settings.set("tray/right", "clipboard");
        settings.set("corners/topRight", "toggle-pins");
        check("retired action settings fall back safely", settings.action("tray/right", "menu") == "menu" &&
              settings.action("corners/topRight", "none") == "none");
        settings.set("tray/right", "history");
        check("supported action settings remain usable", settings.action("tray/right", "menu") == "history");
        auto drag = [](QWidget *widget, const QPoint &from, const QPoint &to, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(widget, Qt::LeftButton, modifiers, from);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, modifiers);
            QApplication::sendEvent(widget, &move);
            QTest::mouseRelease(widget, Qt::LeftButton, modifiers, to);
        };
        AnnotationCanvas editCanvas(sampleDocument(), settings);
        editCanvas.setTool(Tool::Rectangle);
        editCanvas.setAttribute(Qt::WA_DontShowOnScreen);
        editCanvas.show();
        drag(&editCanvas, {100, 100}, {220, 180});
        drag(&editCanvas, {150, 140}, {170, 150});
        check("same-tool object reediting", editCanvas.document().annotations.size() == 1 &&
              editCanvas.document().annotations.first().rect.topLeft() == QPointF(120, 110));
        drag(&editCanvas, {170, 150}, {190, 170}, Qt::ControlModifier);
        check("control drag clones selected annotation", editCanvas.document().annotations.size() == 2 &&
              editCanvas.document().annotations.first().rect.topLeft() == QPointF(120, 110));
        QTest::keyClick(&editCanvas, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(&editCanvas, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        check("annotation copy paste shortcuts", editCanvas.document().annotations.size() == 3);
        editCanvas.undo();
        editCanvas.redo();
        check("redo selects restored annotation", editCanvas.selectedIndex() == 2);
        const double beforeWidth = editCanvas.style().width;
        QTest::keyClick(&editCanvas, Qt::Key_2);
        check("number key grows brush width", editCanvas.style().width == beforeWidth + 1);
        QTest::keyClick(&editCanvas, Qt::Key_1);
        check("number key reduces brush width", editCanvas.style().width == beforeWidth);
        editCanvas.setDocument(sampleDocument());
        editCanvas.setTool(Tool::Polyline);
        drag(&editCanvas, {100, 100}, {220, 140});
        check("polyline drag creates finished line", !editCanvas.drawing() &&
              editCanvas.document().annotations.first().points == QVector<QPointF>{{100, 100}, {220, 140}});
        QTest::keyClick(&editCanvas, Qt::Key_Tab);
        check("Tab switches to arrow", editCanvas.tool() == Tool::Arrow &&
              editCanvas.document().annotations.first().tool == Tool::Arrow);
        QTest::keyClick(&editCanvas, Qt::Key_Tab);
        check("Tab switches back to line", editCanvas.tool() == Tool::Line);
        for (Tool tool : {Tool::Line, Tool::Arrow}) {
            const QString label = tool == Tool::Line ? "line" : "arrow";
            editCanvas.setDocument(sampleDocument());
            editCanvas.setTool(tool);
            drag(&editCanvas, {250, 100}, {100, 220});
            const QImage selected = editCanvas.grab().toImage();
            const QColor handle = Ui::accent();
            check(label + " shows only endpoint handles",
                  selected.pixelColor(250, 100) == handle && selected.pixelColor(100, 220) == handle &&
                  selected.pixelColor(100, 100) != handle && selected.pixelColor(250, 220) != handle);
            selected.save(QDir(directory).filePath(label + "-endpoints.png"));
            drag(&editCanvas, {250, 100}, {300, 260});
            check(label + " start endpoint crosses opposite endpoint",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{300, 260}, {100, 220}});
            drag(&editCanvas, {100, 220}, {80, 60});
            check(label + " end endpoint moves independently",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{300, 260}, {80, 60}} &&
                  editCanvas.document().annotations.first().rect == QRectF(80, 60, 220, 200));
            editCanvas.undo();
            check(label + " endpoint undo restores both points",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{300, 260}, {100, 220}});
            editCanvas.redo();
            check(label + " endpoint redo restores selection",
                  editCanvas.selectedIndex() == 0 && editCanvas.tool() == tool &&
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{300, 260}, {80, 60}});
            drag(&editCanvas, {190, 160}, {200, 180});
            check(label + " body drag translates both endpoints",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{310, 280}, {90, 80}});

            Document axisDocument = sampleDocument();
            Annotation axisLine;
            axisLine.tool = tool;
            axisLine.points = {{100, 100}, {220, 100}};
            axisLine.rect = QRectF(100, 100, 120, 0);
            axisDocument.annotations = {axisLine};
            editCanvas.setDocument(axisDocument);
            editCanvas.setTool(tool);
            QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {160, 100});
            drag(&editCanvas, {220, 100}, {100, 220});
            check(label + " horizontal line becomes vertical through endpoint drag",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{100, 100}, {100, 220}});
            drag(&editCanvas, {100, 100}, {250, 220});
            check(label + " vertical line becomes horizontal through start drag",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{250, 220}, {100, 220}});
            drag(&editCanvas, {100, 220}, {70, 190}, Qt::ShiftModifier);
            const auto constrained = editCanvas.document().annotations.first().points;
            check(label + " Shift constrains endpoint relative to the fixed point",
                  constrained.first() == QPointF(250, 220) && qAbs(constrained.last().y() - 220) < 0.01);

            editCanvas.setDocument(axisDocument);
            editCanvas.setTool(Tool::Select);
            editCanvas.setZoom(0.5);
            QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {80, 50});
            drag(&editCanvas, {110, 50}, {160, 110});
            check(label + " endpoint editing respects canvas zoom in selection mode",
                  editCanvas.document().annotations.first().points == QVector<QPointF>{{100, 100}, {320, 220}});
            editCanvas.setZoom(1);

            axisDocument.annotations[0].rotation = 90;
            editCanvas.setDocument(axisDocument);
            editCanvas.setTool(tool);
            QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {160, 100});
            drag(&editCanvas, {160, 160}, {220, 180});
            const auto rotated = editCanvas.document().annotations.first();
            check(label + " endpoint edit preserves rotated opposite point",
                  QLineF(rotated.points.first(), QPointF(160, 40)).length() < 0.01 &&
                  rotated.points.last() == QPointF(220, 180) && rotated.rotation == 0);
        }
        editCanvas.setDocument(sampleDocument());
        editCanvas.setTool(Tool::Polyline);
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {100, 100});
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {200, 100});
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {250, 150});
        QMouseEvent preview(QEvent::MouseMove, QPointF(300, 120), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&editCanvas, &preview);
        QTest::mouseClick(&editCanvas, Qt::RightButton, {}, {300, 120});
        check("polyline ends at last committed vertex", !editCanvas.drawing() &&
              editCanvas.document().annotations.first().points == QVector<QPointF>{{100, 100}, {200, 100}, {250, 150}});
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {200, 100});
        drag(&editCanvas, {200, 100}, {210, 130});
        check("polyline vertex edit preserves every other vertex",
              editCanvas.document().annotations.first().points == QVector<QPointF>{{100, 100}, {210, 130}, {250, 150}});
        editCanvas.setDocument(matching);
        editCanvas.setTool(Tool::Ellipse);
        QTest::mouseClick(&editCanvas, Qt::RightButton, {}, {60, 60});
        check("right click annotates detected element", editCanvas.document().annotations.size() == 1 &&
              editCanvas.document().annotations.first().rect == QRectF(40, 40, 100, 70));

        Document textDocument = sampleDocument();
        Annotation textItem;
        textItem.tool = Tool::Text;
        textItem.rect = QRectF(200, 180, 120, 60);
        textItem.fontSize = 24;
        textItem.text = "Resize";
        textDocument.annotations.append(textItem);
        const QVector<QPoint> corners{{200, 180}, {320, 180}, {320, 240}, {200, 240}};
        const QVector<QPoint> grown{{80, 120}, {440, 120}, {440, 300}, {80, 300}};
        for (int i = 0; i < corners.size(); ++i) {
            editCanvas.setDocument(textDocument);
            editCanvas.setTool(Tool::Text);
            QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {250, 210});
            drag(&editCanvas, corners[i], grown[i]);
            const auto resized = editCanvas.document().annotations.first();
            check("text corner scales font " + QString::number(i), qAbs(resized.fontSize - 48) < 0.01 &&
                  resized.rect.size() == QSizeF(240, 120));
        }
        editCanvas.setDocument(textDocument);
        editCanvas.setTool(Tool::Text);
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {250, 210});
        drag(&editCanvas, {260, 153}, {317, 210});
        check("rotation handle rotates text", qAbs(editCanvas.document().annotations.first().rotation - 90) < 1);
        textDocument.annotations[0].rotation = 30;
        editCanvas.setDocument(textDocument);
        editCanvas.setTool(Tool::Text);
        QTest::mouseClick(&editCanvas, Qt::LeftButton, {}, {260, 210});
        QTransform rotated;
        rotated.translate(260, 210);
        rotated.rotate(30);
        rotated.translate(-260, -210);
        drag(&editCanvas, rotated.map(QPoint(320, 240)), {440, 300}, Qt::ShiftModifier);
        check("Shift resize resets text rotation", editCanvas.document().annotations.first().rotation == 0 &&
              editCanvas.document().annotations.first().fontSize > 24);

        editCanvas.setDocument(sampleDocument());
        editCanvas.setTool(Tool::Pen);
        auto brushStyle = editCanvas.style();
        brushStyle.shape = 3;
        brushStyle.width = 20;
        brushStyle.color.setAlpha(100);
        editCanvas.setStyle(brushStyle);
        drag(&editCanvas, {100, 300}, {200, 330}, Qt::ShiftModifier);
        const Annotation stroke = editCanvas.document().annotations.first();
        check("transparent pen direction constraint", stroke.tool == Tool::Pen && stroke.color.alpha() == 100 &&
              stroke.points.size() >= 2 && qAbs(stroke.points.last().y() - stroke.points.first().y()) < 0.01);
        for (Tool effect : {Tool::Mosaic, Tool::Blur}) {
            Document effectDocument;
            effectDocument.image = QImage(100, 100, QImage::Format_ARGB32);
            effectDocument.image.fill(Qt::white);
            effectDocument.image.setPixelColor(50, 50, Qt::black);
            Annotation effectStroke;
            effectStroke.tool = effect;
            effectStroke.shape = 3;
            effectStroke.width = 15;
            effectStroke.rect = QRectF(20, 50, 60, 0);
            effectStroke.points = {{20, 50}, {80, 50}};
            effectDocument.annotations = {effectStroke};
            const QImage result = effectDocument.render();
            check("effect brush changes only stroked pixels " + toolKey(effect),
                  result.pixelColor(50, 50) != effectDocument.image.pixelColor(50, 50) &&
                  result.pixelColor(50, 10) == Qt::white);
            check("effect brush JSON round trip " + toolKey(effect),
                  Document::fromJson(effectDocument.toJson(), effectDocument.image).render() == result);
        }
        BrushTips tips(&editCanvas, settings);
        const QStringList initialTips = settings.value("annotation/tips", "2,4,8").toString().split(',');
        for (auto *tip : tips.findChildren<QToolButton *>())
            if (tip->property("tipIndex").toInt() == 1 && tip->property("tipIndex").isValid()) {
                QWheelEvent wheel(QPointF(2, 2), QPointF(2, 2), QPoint(), QPoint(0, 120),
                                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(tip, &wheel);
            }
        const QStringList savedTips = settings.value("annotation/tips").toString().split(',');
        check("wheel updates one saved brush tip", savedTips.size() == initialTips.size() &&
              savedTips[0] == initialTips[0] && savedTips[1].toDouble() == initialTips[1].toDouble() + 1);
        settings.set("annotation/palette", QStringList{"#80ff0000", "#ff00ff00", "#ff0000ff"});
        PaletteWidget palette(&editCanvas, settings);
        const bool moved = palette.model()->moveRows({}, 0, 1, {}, 3);
        check("palette drag order persists", moved && settings.palette().last() == "#80ff0000");
        QTest::mouseClick(palette.viewport(), Qt::LeftButton, {}, palette.visualItemRect(palette.item(2)).center());
        check("palette retains alpha channel", editCanvas.style().color.alpha() == 128);
        editCanvas.hide();
    }
    {
        Settings editSettings(temporary.path() + "/editable-annotations");
        AnnotationCanvas editable(sampleDocument(), editSettings);
        editable.setAttribute(Qt::WA_DontShowOnScreen);
        editable.show();
        auto drag = [&](const QPoint &from, const QPoint &to) {
            QTest::mousePress(&editable, Qt::LeftButton, {}, from);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&editable, &move);
            QTest::mouseRelease(&editable, Qt::LeftButton, {}, to);
        };
        for (Tool tool : {Tool::Line, Tool::Arrow, Tool::Rectangle, Tool::Ellipse, Tool::Pen}) {
            editable.setDocument(sampleDocument());
            editable.setTool(tool);
            drag({100, 100}, {220, 160});
            check("new annotation stays active after drawing " + toolKey(tool),
                  editable.selectedIndex() == 0 && !editable.drawing());
            editable.setTool(tool == Tool::Pen ? Tool::Arrow : Tool::Pen);
            QTest::mouseClick(&editable, Qt::LeftButton, {}, tool == Tool::Rectangle ? QPoint(100, 130) :
                              tool == Tool::Ellipse ? QPoint(160, 100) : QPoint(160, 130));
            check("another drawing tool can activate existing annotation " + toolKey(tool),
                  editable.selectedIndex() == 0 && editable.document().annotations.size() == 1);
        }
        Document saved = sampleDocument();
        Annotation line;
        line.tool = Tool::Arrow;
        line.rect = QRectF(100, 100, 120, 0);
        line.points = {{100, 100}, {220, 100}};
        line.color = QColor("#ff536b");
        line.width = 8;
        saved.annotations = {line};
        editable.setDocument(saved);
        editable.setTool(Tool::Pen);
        QTest::mouseClick(&editable, Qt::LeftButton, {}, {195, 115});
        check("arrow head can activate the existing arrow", editable.selectedIndex() == 0 &&
              editable.document().annotations.size() == 1);
        check("selecting without moving creates no undo entry", !editable.canUndo());
        editable.setStyle(editable.style());
        check("unchanged style creates no undo entry", !editable.canUndo());
        auto changed = editable.style();
        changed.color = QColor("#58a6ff");
        changed.width = 14;
        editable.setStyle(changed);
        check("style edit changes selected annotation pixels", editable.document().render().pixelColor(160, 100) ==
              changed.color && editable.document().annotations.first().width == 14);
        editable.undo();
        check("style undo restores the original selected object", editable.document().annotations.first().toJson() == line.toJson());
        editable.redo();
        check("style redo reactivates the edited object and preserves the drawing tool", editable.selectedIndex() == 0 &&
              editable.style().width == 14 && editable.tool() == Tool::Pen);
        editSettings.set("annotation/selectNew", false);
        editable.setDocument(saved);
        editable.setTool(Tool::Rectangle);
        QTest::mouseClick(&editable, Qt::LeftButton, {}, {160, 100});
        check("manual selection remains active when auto selection is disabled", editable.selectedIndex() == 0);
        drag({220, 100}, {260, 180});
        check("endpoint remains editable when auto selection is disabled", editable.selectedIndex() == 0 &&
              editable.document().annotations.first().points == QVector<QPointF>{{100, 100}, {260, 180}});
        QTest::mouseClick(&editable, Qt::RightButton, {}, {400, 300});
        drag({320, 240}, {420, 300});
        check("auto selection preference still applies to new annotations", editable.selectedIndex() == -1 &&
              editable.document().annotations.size() == 2);
        editSettings.set("annotation/selectNew", true);
        Document outline = sampleDocument();
        Annotation box;
        box.tool = Tool::Rectangle;
        box.rect = QRectF(60, 60, 300, 200);
        outline.annotations = {box};
        editable.setDocument(outline);
        editable.setTool(Tool::Arrow);
        drag({120, 140}, {260, 160});
        check("drawing can start inside an unselected unfilled shape", editable.document().annotations.size() == 2 &&
              editable.document().annotations.last().tool == Tool::Arrow);
        for (Tool tool : {Tool::Text, Tool::Number}) {
            Document labeled = sampleDocument();
            Annotation label;
            label.tool = tool;
            label.rect = QRectF(200, 180, 120, 60);
            label.fontSize = 24;
            label.text = "Editable";
            labeled.annotations = {label};
            editable.setDocument(labeled);
            editable.setTool(Tool::Select);
            QTest::mouseClick(&editable, Qt::LeftButton, {}, {250, 210});
            auto next = editable.style();
            next.fontSize = 48;
            editable.setStyle(next);
            check("font property resizes the selected label " + toolKey(tool),
                  editable.style().rect.size() == QSizeF(240, 120) &&
                  (tool == Tool::Text ? editable.style().rect.topLeft() == label.rect.topLeft()
                                      : editable.style().rect.center() == label.rect.center()));
            editable.undo();
            check("font property undo restores label geometry " + toolKey(tool),
                  editable.document().annotations.first().toJson() == label.toJson());
        }
        editable.hide();
    }
    Native::Desktop desktop;
    desktop.image = sampleDocument().image;
    desktop.geometry = QRect(-880, 0, 880, 520);
    desktop.screens = {desktop.geometry};
    CaptureOverlay overlay(desktop, settings, QRect(-800, 60, 300, 180));
    overlay.setAttribute(Qt::WA_DontShowOnScreen);
    overlay.show();
    QCoreApplication::processEvents();
    check("negative-origin selection", overlay.selectedGlobal() == QRect(-800, 60, 300, 180));
    QTest::keyClick(&overlay, Qt::Key_Right);
    check("one-pixel selection movement", overlay.selectedGlobal().x() == -799);
    QTest::keyClicks(&overlay, "160x90");
    QTest::keyClick(&overlay, Qt::Key_Return);
    check("typed capture dimensions", overlay.selectedGlobal().size() == QSize(160, 90));
    overlay.hide();
    CaptureOverlay boundary(desktop, settings, QRect(-880, 0, 100, 100));
    boundary.setAttribute(Qt::WA_DontShowOnScreen);
    boundary.show();
    QTest::keyClick(&boundary, Qt::Key_Left);
    check("selection boundary preserves size", boundary.selectedGlobal() == QRect(-880, 0, 100, 100));
    boundary.hide();
    {
        CaptureOverlay shortcuts(desktop, settings, QRect(-800, 80, 300, 180));
        shortcuts.setAttribute(Qt::WA_DeleteOnClose, false);
        shortcuts.setAttribute(Qt::WA_DontShowOnScreen);
        shortcuts.show();
        QApplication::setActiveWindow(&shortcuts);
        shortcuts.findChild<QAction *>("captureTool1")->trigger();
        auto *canvas = shortcuts.findChild<AnnotationCanvas *>();
        QTest::mousePress(canvas, Qt::LeftButton, {}, {20, 20});
        QMouseEvent move(QEvent::MouseMove, QPointF(100, 80), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {100, 80});
        QString target;
        QObject::connect(&shortcuts, &CaptureOverlay::outputRequested, &shortcuts,
            [&](const Document &, const QRect &, const QString &action, bool) { target = action; });
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        check("object copy keeps capture open", target.isEmpty() && QApplication::clipboard()->mimeData()->hasFormat("application/x-easysnip-annotation"));
        QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier);
        check("Ctrl C outputs image with object selected", target == "copy");
        shortcuts.completeOutput(false);
        target.clear();
        QTest::keyClick(canvas, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        check("Ctrl Shift S requests quick save", target == "save");
        shortcuts.completeOutput(false);
        target.clear();
        QTest::keyClick(canvas, Qt::Key_S, Qt::ControlModifier);
        check("Ctrl S requests save dialog", target == "save-as");
        shortcuts.completeOutput(false);
        target.clear();
        auto *saveAction = shortcuts.findChild<QAction *>("captureOutput-save-as");
        for (auto *button : shortcuts.findChildren<QToolButton *>())
            if (button->defaultAction() == saveAction)
                QTest::mouseClick(button, Qt::LeftButton, Qt::ShiftModifier);
        check("Shift save button requests quick save", target == "save");
        shortcuts.completeOutput(false);
        auto *colorButton = shortcuts.findChild<QPushButton *>("annotationColor");
        auto style = canvas->style();
        style.color.setAlpha(248);
        canvas->setStyle(style);
        QWheelEvent alphaWheel(QPointF(2, 2), QPointF(2, 2), QPoint(), QPoint(0, 120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(colorButton, &alphaWheel);
        check("large color button wheel clamps alpha", canvas->style().color.alpha() == 255);
        QMouseEvent cursorMove(QEvent::MouseMove, QPointF(60, 60), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &cursorMove);
        QTest::keyClick(canvas, Qt::Key_Alt);
        QTest::keyClick(canvas, Qt::Key_C);
        check("Alt magnifier works during annotation", QApplication::clipboard()->text() ==
              colorText(canvas->document().render().pixelColor(60, 60), "HEX", true));
        QTest::keyClick(canvas, Qt::Key_Shift);
        QTest::keyClick(canvas, Qt::Key_C);
        check("annotation magnifier switches color format", QApplication::clipboard()->text().startsWith("RGB("));
        settings.set("color/format", "HEX");
        target.clear();
        canvas->setTool(Tool::Select);
        QTest::mouseDClick(canvas, Qt::LeftButton, {}, {240, 140});
        check("double click copies annotated capture", target == "copy");
        shortcuts.completeOutput(false);
        shortcuts.hide();
    }
    {
        const QPoint cursorBefore = QCursor::pos();
        CaptureOverlay controls(desktop, settings, QRect(-780, 100, 200, 140));
        controls.setAttribute(Qt::WA_DeleteOnClose, false);
        controls.setAttribute(Qt::WA_DontShowOnScreen);
        controls.show();
        const QRect original = controls.selectedGlobal();
        QTest::keyClick(&controls, Qt::Key_Left, Qt::ControlModifier);
        QTest::keyClick(&controls, Qt::Key_Right, Qt::ControlModifier);
        QTest::keyClick(&controls, Qt::Key_Up, Qt::ControlModifier);
        QTest::keyClick(&controls, Qt::Key_Down, Qt::ControlModifier);
        check("Ctrl arrows expand corresponding edge", controls.selectedGlobal() == original.adjusted(-1, -1, 1, 1));
        QTest::keyClick(&controls, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(&controls, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(&controls, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(&controls, Qt::Key_Down, Qt::ShiftModifier);
        check("Shift arrows shrink corresponding edge", controls.selectedGlobal() == original);
        QTest::mouseMove(&controls, {150, 150});
        QTest::keyClick(&controls, Qt::Key_D);
        check("WASD leaves selection fixed", controls.selectedGlobal() == original);
        QTest::mousePress(&controls, Qt::LeftButton, {}, {299, 239});
        QTest::keyClick(&controls, Qt::Key_D);
        QTest::mouseRelease(&controls, Qt::LeftButton, {}, {300, 239});
        check("WASD resizes during drag", controls.selectedGlobal().width() == 201);
        QTest::mouseMove(&controls, {30, 70});
        QTest::keyClick(&controls, Qt::Key_Alt);
        QTest::keyClick(&controls, Qt::Key_C);
        check("Alt enables magnifier and C copies color", QApplication::clipboard()->text() ==
              colorText(desktop.image.pixelColor(30, 70), settings.value("color/format", "HEX").toString(), true));
        QTest::keyClick(&controls, Qt::Key_Shift);
        QTest::keyClick(&controls, Qt::Key_C);
        check("Shift changes magnifier color format", QApplication::clipboard()->text().startsWith("RGB("));
        QTest::keyClick(&controls, Qt::Key_Alt);
        QApplication::clipboard()->setText("unchanged");
        QTest::keyClick(&controls, Qt::Key_C);
        check("hidden magnifier does not copy color", QApplication::clipboard()->text() == "unchanged");
        QCursor::setPos(cursorBefore);
        controls.hide();
    }
    {
        CaptureOverlay replay(desktop, settings, QRect(-800, 80, 300, 180));
        replay.setAttribute(Qt::WA_DeleteOnClose, false);
        replay.setAttribute(Qt::WA_DontShowOnScreen);
        replay.show();
        Document historical = document;
        historical.sourceRect = QRect(-300, 30, historical.image.width(), historical.image.height());
        int loads = 0;
        replay.setHistory(2, [&](int index) { ++loads; return index == 0 ? historical : Document(); });
        const QRect liveRegion = replay.selectedGlobal();
        const QImage liveImage = replay.findChild<AnnotationCanvas *>()->document().image;
        QTest::keyClick(&replay, Qt::Key_Comma);
        auto *historyCanvas = replay.findChild<AnnotationCanvas *>();
        check("capture history restores editable original", loads == 1 &&
              historyCanvas->document().toJson() == historical.toJson() && historyCanvas->document().image == historical.image);
        check("large history preview fits desktop", replay.rect().contains(historyCanvas->geometry()));
        historyCanvas->setTool(Tool::Select);
        QTest::mouseClick(historyCanvas, Qt::LeftButton, {}, (QPointF(440, 300) * historyCanvas->zoom()).toPoint());
        QTest::keyClick(historyCanvas, Qt::Key_Delete);
        const int editedCount = historyCanvas->document().annotations.size();
        QTest::keyClick(historyCanvas, Qt::Key_Comma);
        check("unreadable history skipped", replay.findChild<AnnotationCanvas *>() == historyCanvas);
        QTest::keyClick(historyCanvas, Qt::Key_Period);
        check("history returns to live capture", replay.selectedGlobal() == liveRegion &&
              replay.findChild<AnnotationCanvas *>()->document().image == liveImage);
        QTest::keyClick(&replay, Qt::Key_Comma);
        historyCanvas = replay.findChild<AnnotationCanvas *>();
        check("history navigation preserves session edits", historyCanvas->document().annotations.size() == editedCount && loads == 3);
        Document exported;
        QString action;
        QObject::connect(&replay, &CaptureOverlay::outputRequested, &replay,
            [&](const Document &result, const QRect &, const QString &target, bool) { exported = result; action = target; });
        QTest::keyClick(historyCanvas, Qt::Key_Return);
        check("Enter copies full-resolution history", action == "copy" && exported.image.size() == historical.image.size());
        replay.completeOutput(false);
        Document abandoned;
        QObject::connect(&replay, &CaptureOverlay::canceled, &replay, [&](const Document &result) { abandoned = result; });
        QTest::keyClick(historyCanvas, Qt::Key_Escape);
        check("cancel during history keeps live draft", abandoned.image == liveImage);
    }
    {
        CaptureOverlay longReplay(desktop, settings, QRect(-800, 80, 300, 180));
        longReplay.setAttribute(Qt::WA_DeleteOnClose, false);
        longReplay.setAttribute(Qt::WA_DontShowOnScreen);
        longReplay.show();
        Document longImage;
        longImage.image = QImage(600, 12000, QImage::Format_RGB32);
        longImage.image.fill(Qt::white);
        longReplay.setHistory(1, [&](int) { return longImage; });
        Document draft;
        QObject::connect(&longReplay, &CaptureOverlay::draftAbandoned, &longReplay,
                         [&](const Document &image) { draft = image; });
        QTest::keyClick(&longReplay, Qt::Key_Comma);
        auto *canvas = longReplay.findChild<AnnotationCanvas *>();
        check("long history image scales below five percent", canvas->zoom() < 0.05 &&
              longReplay.rect().contains(canvas->geometry()));
        longReplay.completeOutput(true);
        check("outputting history retains unfinished live capture", draft.image.size() == QSize(300, 180));
    }
    {
        CaptureOverlay inlineOverlay(desktop, settings);
        inlineOverlay.setAttribute(Qt::WA_DeleteOnClose, false);
        inlineOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        inlineOverlay.show();
        int outputs = 0, cancellations = 0;
        Document exported;
        QRect exportedRegion;
        QString exportedAction;
        QObject::connect(&inlineOverlay, &CaptureOverlay::outputRequested, &inlineOverlay,
                         [&](const Document &result, const QRect &region, const QString &action, bool) {
            ++outputs;
            exported = result;
            exportedRegion = region;
            exportedAction = action;
        });
        QObject::connect(&inlineOverlay, &CaptureOverlay::canceled, &inlineOverlay,
                         [&](const Document &) { ++cancellations; });
        auto *toolbar = inlineOverlay.findChild<QWidget *>("captureEditorToolbar");
        check("capture has no retired highlighter tool", !inlineOverlay.findChild<QAction *>("captureTool8"));
        check("capture toolbar has no region adjustment action", !inlineOverlay.findChild<QAction *>("adjustCaptureRegion"));
        check("capture toolbar has no pin output", !inlineOverlay.findChild<QAction *>("captureOutput-pin"));
        check("capture tools hidden before selection", toolbar && !toolbar->isVisible());
        QTest::mousePress(&inlineOverlay, Qt::LeftButton, {}, QPoint(90, 110));
        QMouseEvent selectionDrag(QEvent::MouseMove, QPointF(650, 360), Qt::NoButton, Qt::LeftButton,
                                   Qt::NoModifier);
        QApplication::sendEvent(&inlineOverlay, &selectionDrag);
        QTest::mouseRelease(&inlineOverlay, Qt::LeftButton, {}, QPoint(650, 360));
        QCoreApplication::processEvents();
        auto *inlineCanvas = inlineOverlay.findChild<AnnotationCanvas *>("captureCanvas");
        check("selection immediately shows inline tools", inlineCanvas && toolbar && toolbar->isVisible() &&
                                                           outputs == 0);
        check("inline canvas matches physical capture pixels",
              inlineCanvas && inlineCanvas->geometry() == QRect(90, 110, 561, 251) &&
                  inlineCanvas->document().image == desktop.image.copy(90, 110, 561, 251));
        check("capture defaults to interactive selection without a selection button",
              inlineCanvas && inlineCanvas->tool() == Tool::Select && inlineCanvas->isVisible() &&
              !inlineCanvas->testAttribute(Qt::WA_TransparentForMouseEvents) &&
              !inlineOverlay.findChild<QAction *>("captureTool0"));
        QToolButton *rectangleButton = nullptr;
        for (auto *button : inlineOverlay.findChildren<QToolButton *>())
            if (button->defaultAction() && button->defaultAction()->objectName() == "captureTool1")
                rectangleButton = button;
        check("rectangle tool is directly available", rectangleButton && rectangleButton->isVisible());
        auto *lineButton = inlineOverlay.findChild<QToolButton *>("captureLineToolButton");
        auto *lineAction = inlineOverlay.findChild<QAction *>("captureTool" + toolKey(Tool::Line));
        auto *arrowAction = inlineOverlay.findChild<QAction *>("captureTool" + toolKey(Tool::Arrow));
        auto *toolVariant = inlineOverlay.findChild<OptionStrip *>("captureToolVariant");
        auto *lineType = inlineOverlay.findChild<LineTypeButton *>("captureLineTypeButton");
        check("inline line primary button has no secondary menu",
              lineButton && !lineButton->menu() && lineAction && arrowAction);
        bool polylineDirect = false;
        for (auto *button : inlineOverlay.findChildren<QToolButton *>())
            if (button->defaultAction() && button->defaultAction()->objectName() == "captureTool" + toolKey(Tool::Polyline))
                polylineDirect = !button->menu();
        check("inline polyline remains separately available", polylineDirect);
        if (inlineCanvas && lineButton && lineAction && arrowAction) {
            lineAction->trigger();
            check("inline line mode uses one cycling icon button",
                  lineType && lineType->isVisible() && toolVariant && !toolVariant->isVisible() &&
                  lineType->toolButtonStyle() == Qt::ToolButtonIconOnly && !lineType->menu());
            const QImage lineIcon = lineType->icon().pixmap(QSize(64, 64)).toImage();
            check("line mode shows a dark line and a gray arrow", lineIcon.pixelColor(32, 46).lightness() < 60 &&
                  lineIcon.pixelColor(32, 22).lightness() > 140);
            lineType->grab().save(QDir(directory).filePath("line-type-line.png"));
            check("inline line style updates the shared button",
                  inlineCanvas->tool() == Tool::Line && lineButton->defaultAction() == lineAction && lineAction->isChecked());
            lineType->click();
            check("inline mode button switches from line to arrow", inlineCanvas->tool() == Tool::Arrow &&
                  lineType->tool() == Tool::Arrow);
            const QImage arrowIcon = lineType->icon().pixmap(QSize(64, 64)).toImage();
            check("arrow mode shows a dark arrow and a gray line", arrowIcon.pixelColor(32, 22).lightness() < 60 &&
                  arrowIcon.pixelColor(32, 46).lightness() > 140);
            lineType->grab().save(QDir(directory).filePath("line-type-arrow.png"));
            lineType->click();
            check("same mode button switches back to line", inlineCanvas->tool() == Tool::Line &&
                  lineType->tool() == Tool::Line);
            lineType->click();
            QTest::keyClick(inlineCanvas, Qt::Key_Tab);
            check("inline Tab synchronizes the line type button", inlineCanvas->tool() == Tool::Line &&
                  lineType->tool() == Tool::Line);
            QTest::keyClick(inlineCanvas, Qt::Key_Tab);
            check("inline Tab switch updates the shared button",
                  inlineCanvas->tool() == Tool::Arrow && lineButton->defaultAction() == arrowAction && arrowAction->isChecked());
            QTest::mousePress(inlineCanvas, Qt::LeftButton, {}, {60, 200});
            QMouseEvent arrowDrag(QEvent::MouseMove, QPointF(300, 60), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(inlineCanvas, &arrowDrag);
            QTest::mouseRelease(inlineCanvas, Qt::LeftButton, {}, {300, 60});
            check("inline arrow uses the chosen two-point style",
                  inlineCanvas->document().annotations.size() == 1 &&
                  inlineCanvas->document().annotations.first().tool == Tool::Arrow &&
                  inlineCanvas->document().annotations.first().points == QVector<QPointF>{{60, 200}, {300, 60}});
            inlineOverlay.grab().save(QDir(directory).filePath("capture-line-endpoints.png"));
            inlineOverlay.findChild<QWidget *>("captureToolOptions")->grab().save(
                QDir(directory).filePath("line-style-options.png"));
            lineType->grab().save(QDir(directory).filePath("line-type-button.png"));
            inlineCanvas->undo();
            lineAction->trigger();
            inlineOverlay.findChild<QAction *>("captureTool1")->trigger();
            lineButton->click();
            check("inline line button remembers the chosen style", inlineCanvas->tool() == Tool::Line);
        }
        if (inlineCanvas && rectangleButton) {
            rectangleButton->click();
            check("inline tool activates canvas input", inlineCanvas->tool() == Tool::Rectangle &&
                  !inlineCanvas->testAttribute(Qt::WA_TransparentForMouseEvents));
            QTest::mousePress(inlineCanvas, Qt::LeftButton, {}, QPoint(25, 25));
            QMouseEvent annotationDrag(QEvent::MouseMove, QPointF(220, 125), Qt::NoButton, Qt::LeftButton,
                                       Qt::NoModifier);
            QApplication::sendEvent(inlineCanvas, &annotationDrag);
            QTest::mouseRelease(inlineCanvas, Qt::LeftButton, {}, QPoint(220, 125));
            check("inline annotation drawing", inlineCanvas->document().annotations.size() == 1);
            QTest::keyClick(inlineCanvas, Qt::Key_Z, Qt::ControlModifier);
            check("inline annotation undo", inlineCanvas->document().annotations.isEmpty());
            QTest::keyClick(inlineCanvas, Qt::Key_Y, Qt::ControlModifier);
            check("inline annotation redo", inlineCanvas->document().annotations.size() == 1);
            QWheelEvent zoomWheel(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, 120),
                                  Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(inlineCanvas, &zoomWheel);
            check("inline canvas keeps capture scale", inlineCanvas->zoom() == 1 &&
                  inlineCanvas->size() == inlineOverlay.selectedGlobal().size());
            QCoreApplication::processEvents();
            check("capture toolbar stays inside screen", inlineOverlay.rect().contains(toolbar->geometry()));
            bool outputsVisible = true;
            for (auto *action : inlineOverlay.findChildren<QAction *>())
                if (action->objectName().startsWith("captureOutput-"))
                    for (auto *bar : toolbar->findChildren<QToolBar *>())
                        if (bar->actions().contains(action))
                            outputsVisible = outputsVisible && bar->widgetForAction(action)->isVisible();
            check("capture output buttons stay directly visible", outputsVisible);
            inlineOverlay.grab().save(QDir(directory).filePath("capture-inline.png"));
            QTest::mouseClick(inlineCanvas, Qt::RightButton);
            check("right click finishes annotation editing", inlineCanvas->selectedIndex() == -1 &&
                  !inlineCanvas->testAttribute(Qt::WA_TransparentForMouseEvents));
            rectangleButton->click();
            QTest::keyClick(&inlineOverlay, Qt::Key_Right);
            QTest::keyClicks(&inlineOverlay, "600x300");
            QTest::keyClick(&inlineOverlay, Qt::Key_Return);
            check("capture region adjustment preserves annotations",
                  inlineCanvas->document().annotations.size() == 1 &&
                      inlineOverlay.selectedGlobal() == QRect(-789, 110, 600, 300) &&
                      inlineCanvas->document().image == desktop.image.copy(91, 110, 600, 300));
        }
        auto *copy = inlineOverlay.findChild<QAction *>("captureOutput-copy");
        if (copy)
            copy->trigger();
        check("inline output includes annotations and source region",
              outputs == 1 && exportedAction == "copy" && exported.annotations.size() == 1 &&
                  exportedRegion == inlineOverlay.selectedGlobal() &&
                  exported.image.size() == inlineOverlay.selectedGlobal().size());
        if (copy)
            copy->trigger();
        check("capture prevents overlapping output requests", outputs == 1);
        inlineOverlay.completeOutput(false);
        check("failed output retains inline editing", inlineOverlay.isVisible() && inlineCanvas &&
              inlineCanvas->document().annotations.size() == 1 && cancellations == 0);
        if (copy)
            copy->trigger();
        check("failed output can be retried", outputs == 2);
        inlineOverlay.completeOutput(true);
        check("successful inline output closes capture", !inlineOverlay.isVisible() && cancellations == 0);
    }
    {
        CaptureOverlay canceledOverlay(desktop, settings, QRect(-800, 80, 300, 180));
        canceledOverlay.setAttribute(Qt::WA_DeleteOnClose, false);
        canceledOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        canceledOverlay.show();
        Document abandoned;
        int cancellations = 0;
        QObject::connect(&canceledOverlay, &CaptureOverlay::canceled, &canceledOverlay,
                         [&](const Document &result) { abandoned = result; ++cancellations; });
        canceledOverlay.findChild<QAction *>("captureTool1")->trigger();
        auto *inlineCanvas = canceledOverlay.findChild<AnnotationCanvas *>();
        QTest::mousePress(inlineCanvas, Qt::LeftButton, {}, QPoint(20, 20));
        QMouseEvent annotationDrag(QEvent::MouseMove, QPointF(120, 90), Qt::NoButton, Qt::LeftButton,
                                   Qt::NoModifier);
        QApplication::sendEvent(inlineCanvas, &annotationDrag);
        QTest::mouseRelease(inlineCanvas, Qt::LeftButton, {}, QPoint(120, 90));
        QTest::keyClick(inlineCanvas, Qt::Key_Escape);
        check("escape cancels inline editing once with annotations",
              cancellations == 1 && abandoned.annotations.size() == 1 && !canceledOverlay.isVisible());
    }
    {
        CaptureOverlay contextual(desktop, settings, QRect(-780, 90, 600, 210));
        contextual.setAttribute(Qt::WA_DeleteOnClose, false);
        contextual.setAttribute(Qt::WA_DontShowOnScreen);
        contextual.show();
        QCoreApplication::processEvents();
        auto *primary = contextual.findChild<QWidget *>("captureEditorToolbar");
        auto *options = contextual.findChild<QWidget *>("captureToolOptions");
        auto *canvas = contextual.findChild<AnnotationCanvas *>();
        auto *variant = contextual.findChild<OptionStrip *>("captureToolVariant");
        auto *effectShape = contextual.findChild<OptionStrip *>("captureEffectShape");
        auto *color = contextual.findChild<QPushButton *>("annotationColor");
        auto *font = contextual.findChild<ValueSlider *>("captureFontSize");
        auto *width = contextual.findChild<ValueSlider *>("captureStrokeWidth");
        auto *opacity = contextual.findChild<ValueSlider *>("captureOpacity");
        auto *colors = contextual.findChild<QWidget *>("captureColorPresets");
        auto *tips = contextual.findChild<QWidget *>("captureBrushTips");
        const QRect primaryGeometry = primary->geometry();
        check("capture initially shows only primary tools", primary->isVisible() && !options->isVisible());
        check("secondary options float separately from primary tools", options->parentWidget() == &contextual);
        bool iconsOnly = true;
        QToolButton *shapeButton = nullptr;
        for (auto *button : primary->findChildren<QToolButton *>())
            if (button->property("captureToolGroup").isValid()) {
                iconsOnly = iconsOnly && !button->menu() && button->toolButtonStyle() == Qt::ToolButtonIconOnly;
                if (button->property("captureToolGroup").toList().contains(int(Tool::Rectangle)))
                    shapeButton = button;
            }
        check("primary annotation tools are icons without option menus", iconsOnly && shapeButton);
        contextual.grab().save(QDir(directory).filePath("capture-primary-only.png"));
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Rectangle))->trigger();
        check("activating shape tool reveals its secondary options",
              options->isVisible() && variant->isVisible() && variant->count() == 3 &&
              color->isVisible() && colors->isVisible() && width->isVisible() && !font->isVisible() && !effectShape->isVisible());
        check("secondary options do not move the primary toolbar", primary->geometry() == primaryGeometry);
        check("secondary options fit screen without covering primary tools",
              contextual.rect().contains(options->geometry()) && !primary->geometry().intersects(options->geometry()));
        check("capture strength controls use sliders", width && font && opacity &&
              options->findChildren<QAbstractSpinBox *>().isEmpty());
        width->setValue(12.5);
        check("stroke slider updates fractional width", qAbs(canvas->style().width - 12.5) < 0.01);
        opacity->setValue(50);
        check("opacity slider updates alpha", canvas->style().color.alpha() == 128);
        opacity->setValue(0);
        check("opacity slider reaches transparent endpoint", canvas->style().color.alpha() == 0);
        opacity->setValue(100);
        check("opacity slider reaches opaque endpoint", canvas->style().color.alpha() == 255);
        check("expanded annotation panel contains no dropdowns", options->findChildren<QComboBox *>().isEmpty());
        auto *directLine = options->findChild<OptionStrip *>("captureLineStyle");
        directLine->findChildren<QToolButton *>().at(1)->click();
        check("two line-style icons select solid or dashed strokes", directLine->count() == 2 &&
              directLine->findChildren<QToolButton *>().first()->toolButtonStyle() == Qt::ToolButtonIconOnly &&
              canvas->style().lineStyle == int(Qt::DashLine));
        auto *directFill = options->findChild<OptionStrip *>("captureFillMode");
        directFill->findChildren<QToolButton *>().at(1)->click();
        check("direct fill button sets drawing fill", canvas->style().fill == canvas->style().color);
        directFill->findChildren<QToolButton *>().at(0)->click();
        check("direct hollow button clears drawing fill", canvas->style().fill.alpha() == 0);
        const int ellipseIndex = variant->findData(int(Tool::Ellipse));
        variant->setCurrentIndex(ellipseIndex);
        QMetaObject::invokeMethod(variant, "activated", Qt::DirectConnection, Q_ARG(int, ellipseIndex));
        check("secondary shape selection updates primary icon and drawing tool",
              canvas->tool() == Tool::Ellipse && shapeButton->defaultAction()->data().toInt() == int(Tool::Ellipse));
        shapeButton->click();
        check("clicking active primary tool collapses secondary options",
              !options->isVisible() && canvas->tool() == Tool::Select && primary->geometry() == primaryGeometry);
        shapeButton->click();
        check("reactivating primary tool restores chosen shape",
              options->isVisible() && canvas->tool() == Tool::Ellipse && variant->currentData().toInt() == int(Tool::Ellipse));
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Text))->trigger();
        check("text secondary options show font instead of stroke width",
              options->isVisible() && font->isVisible() && !width->isVisible() && color->isVisible() &&
              variant->count() == 2 && !effectShape->isVisible());
        font->setValue(48);
        check("font slider updates text size", canvas->style().fontSize == 48);
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Mosaic))->trigger();
        check("mosaic secondary options show effect shapes without color palette",
              options->isVisible() && effectShape->isVisible() && !color->isVisible() && !colors->isVisible() &&
              !font->isVisible() && width->isVisible());
        effectShape->setCurrentIndex(3);
        QMetaObject::invokeMethod(effectShape, "activated", Qt::DirectConnection, Q_ARG(int, 3));
        check("effect shape option changes brush mode", canvas->style().shape == 3 && tips->isVisible());
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Magnify))->trigger();
        check("magnify secondary options omit color font and stroke controls",
              options->isVisible() && !color->isVisible() && !font->isVisible() && !width->isVisible() && !colors->isVisible());
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Magnify))->trigger();
        check("closing magnify restores default selection and hides options",
              canvas->tool() == Tool::Select && !options->isVisible());
        contextual.findChild<QAction *>("captureTool" + toolKey(Tool::Rectangle))->trigger();
        contextual.setHistory(1, [](int) { return sampleDocument(); });
        QTest::keyClick(&contextual, Qt::Key_Comma);
        check("history opens with only primary tools", !options->isVisible());
        check("history preview does not expose live capture resize border",
              !contextual.findChild<QWidget *>("captureResizeBorder")->isVisible());
        QTest::keyClick(&contextual, Qt::Key_Period);
        check("returning from history restores active secondary options", options->isVisible() &&
              contextual.findChild<AnnotationCanvas *>()->tool() == Tool::Rectangle);
        contextual.grab().save(QDir(directory).filePath("capture-secondary-shapes.png"));
        shapeButton->click();
        check("deactivating shape tool collapses secondary options", !options->isVisible());
        QTest::mouseClick(&contextual, Qt::RightButton, {}, {400, 250});
        check("clearing capture selection hides both tool levels", !primary->isVisible() && !options->isVisible());
        contextual.hide();
    }
    {
        Settings activeSettings(temporary.path() + "/active-style-toolbar");
        activeSettings.set("magnifier/enabled", false);
        activeSettings.set("annotation/palette", QStringList{"#ff536b", "#58a6ff"});
        CaptureOverlay active(desktop, activeSettings, QRect(-800, 60, 640, 320));
        active.setAttribute(Qt::WA_DeleteOnClose, false);
        active.setAttribute(Qt::WA_DontShowOnScreen);
        active.show();
        auto *canvas = active.findChild<AnnotationCanvas *>();
        auto *options = active.findChild<QWidget *>("captureToolOptions");
        auto *width = active.findChild<ValueSlider *>("captureStrokeWidth");
        auto *opacity = active.findChild<ValueSlider *>("captureOpacity");
        auto *lineType = active.findChild<LineTypeButton *>("captureLineTypeButton");
        auto drag = [&](const QPoint &from, const QPoint &to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, from);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, to);
        };
        active.findChild<QAction *>("captureTool" + toolKey(Tool::Arrow))->trigger();
        width->setValue(4);
        const Annotation defaults = canvas->style();
        check("toolbar edits drawing defaults without an active object", canvas->selectedIndex() == -1 &&
              options->isVisible() && width->value() == defaults.width);
        drag({70, 90}, {250, 90});
        check("drawn arrow activates toolbar editing", canvas->selectedIndex() == 0 && options->isVisible());
        drag({250, 90}, {290, 150});
        drag({180, 120}, {200, 140});
        check("active arrow supports endpoint and position editing", canvas->document().annotations.size() == 1 &&
              canvas->document().annotations.first().points == QVector<QPointF>{{90, 110}, {310, 170}});
        width->slider()->setSliderDown(true);
        width->setValue(8);
        width->setValue(12);
        width->slider()->setSliderDown(false);
        check("capture slider changes active arrow width", canvas->style().width == 12);
        canvas->undo();
        check("capture slider drag undoes as one edit", canvas->document().annotations.first().width == 4);
        canvas->redo();
        check("capture slider redo restores selection and inspector", canvas->selectedIndex() == 0 && width->value() == 12);
        auto *palette = active.findChild<QListWidget *>("annotationPalette");
        QTest::mouseClick(palette->viewport(), Qt::LeftButton, {}, palette->visualItemRect(palette->item(1)).center());
        opacity->setValue(50);
        check("palette and opacity edit the active arrow", canvas->style().color == QColor(88, 166, 255, 128) &&
              canvas->document().annotations.size() == 1);
        lineType->click();
        check("secondary variant edits active object without resetting its geometry", canvas->selectedIndex() == 0 &&
              canvas->style().tool == Tool::Line && canvas->tool() == Tool::Arrow &&
              canvas->style().points == QVector<QPointF>{{90, 110}, {310, 170}} &&
              canvas->style().width == 12 && canvas->style().color == QColor(88, 166, 255, 128));
        canvas->undo();
        check("mode toggle undo restores the arrow", canvas->document().annotations.first().tool == Tool::Arrow &&
              lineType->tool() == Tool::Arrow && canvas->document().annotations.size() == 1);
        canvas->redo();
        check("type conversion redo restores the selected button", canvas->selectedIndex() == 0 &&
              lineType->tool() == Tool::Line && canvas->style().tool == Tool::Line);
        lineType->click();
        check("mode button toggles the active object back to arrow", canvas->selectedIndex() == 0 &&
              canvas->style().tool == Tool::Arrow && lineType->tool() == Tool::Arrow);
        lineType->click();
        active.grab().save(QDir(directory).filePath("capture-active-annotation.png"));
        QTest::mouseClick(canvas, Qt::RightButton, {}, {450, 220});
        check("ending object editing restores drawing defaults", canvas->selectedIndex() == -1 &&
              options->isVisible() && width->value() == defaults.width &&
              canvas->style().color == defaults.color && canvas->style().tool == Tool::Arrow);
        width->setValue(7);
        opacity->setValue(80);
        check("unselected toolbar changes preserve existing objects", canvas->document().annotations.first().width == 12 &&
              canvas->document().annotations.first().color == QColor(88, 166, 255, 128));
        drag({400, 220}, {520, 260});
        check("unselected toolbar changes apply to the next operation", canvas->document().annotations.size() == 2 &&
              canvas->style().width == 7 && canvas->style().color.alpha() == 204 && canvas->style().tool == Tool::Arrow);
        active.findChild<QAction *>("captureTool" + toolKey(Tool::Rectangle))->trigger();
        const Annotation shapeDefaults = canvas->style();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {200, 140});
        check("other drawing tool reactivates existing line with matching inspector", canvas->tool() == Tool::Rectangle &&
              canvas->selectedIndex() == 0 && width->value() == 12 && lineType->isVisible() &&
              lineType->tool() == Tool::Line);
        width->setValue(16);
        QTest::mouseClick(canvas, Qt::RightButton, {}, {450, 220});
        check("editing another tool's object preserves current tool defaults", canvas->tool() == Tool::Rectangle &&
              canvas->selectedIndex() == -1 && canvas->style().width == shapeDefaults.width &&
              canvas->style().color == shapeDefaults.color);
        active.findChild<QAction *>("captureTool" + toolKey(Tool::Rectangle))->trigger();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {200, 140});
        check("default selection exposes active object properties", canvas->tool() == Tool::Select &&
              canvas->selectedIndex() == 0 && options->isVisible());
        width->setValue(18);
        check("default selection toolbar edits the selected object", canvas->document().annotations.first().width == 18);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {550, 200});
        check("blank click clears the active object inspector", canvas->selectedIndex() == -1 && !options->isVisible());
        active.hide();
    }
    {
        CaptureOverlay neutral(desktop, settings, QRect(-780, 90, 600, 210));
        neutral.setAttribute(Qt::WA_DeleteOnClose, false);
        neutral.setAttribute(Qt::WA_DontShowOnScreen);
        neutral.show();
        auto *canvas = neutral.findChild<AnnotationCanvas *>();
        auto *options = neutral.findChild<QWidget *>("captureToolOptions");
        auto inactive = [&] {
            for (auto *action : neutral.findChildren<QAction *>())
                if (action->objectName().startsWith("captureTool") && action->isCheckable() && action->isChecked())
                    return false;
            return true;
        };
        check("default selection leaves every primary tool inactive", inactive() && canvas->tool() == Tool::Select);
        Document marked = canvas->document();
        Annotation mark;
        mark.tool = Tool::Rectangle;
        mark.rect = QRectF(70, 60, 100, 70);
        marked.annotations = {mark};
        canvas->setDocument(marked);
        const QRect region = neutral.selectedGlobal();
        QTest::mousePress(canvas, Qt::LeftButton, {}, {100, 90});
        QMouseEvent move(QEvent::MouseMove, QPointF(120, 100), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {120, 100});
        check("default selection directly moves existing annotations",
              canvas->document().annotations.size() == 1 && canvas->selectedIndex() == 0 &&
              canvas->document().annotations.first().rect == QRectF(90, 70, 100, 70) &&
              neutral.selectedGlobal() == region && canvas->tool() == Tool::Select);
        check("selecting an annotation shows its options without activating a primary tool", inactive() && options->isVisible());
        canvas->undo();
        canvas->redo();
        check("redo preserves default selection mode",
              canvas->tool() == Tool::Select && canvas->selectedIndex() == 0 && inactive() && options->isVisible());
        canvas->copyObject();
        QTest::keyClick(canvas, Qt::Key_Delete);
        check("default selection deletes existing annotations", canvas->document().annotations.isEmpty());
        QTest::keyClick(&neutral, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        if (canvas->document().annotations.size() != 1 || canvas->tool() != Tool::Select || !inactive() || !options->isVisible())
            QTextStream(stdout) << "Neutral annotation paste state: count=" << canvas->document().annotations.size()
                                << " selected=" << canvas->selectedIndex() << " tool=" << int(canvas->tool())
                                << " inactive=" << inactive() << " options=" << options->isVisible()
                                << " capture=" << neutral.isVisible() << " clipboard="
                                << QApplication::clipboard()->mimeData()->formats().join(',') << '\n';
        check("annotation paste needs no selection toolbar action",
              canvas->document().annotations.size() == 1 && canvas->tool() == Tool::Select && inactive() && options->isVisible());
        QTest::keyClick(canvas, Qt::Key_Delete);
        QTest::mousePress(canvas, Qt::LeftButton, {}, {250, 140});
        QMouseEvent blankDrag(QEvent::MouseMove, QPointF(280, 160), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &blankDrag);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {280, 160});
        check("blank selection drag moves capture without creating annotations", canvas->document().annotations.isEmpty() &&
              neutral.selectedGlobal() == region.translated(30, 20) && inactive());
        int outputs = 0;
        QObject::connect(&neutral, &CaptureOverlay::outputRequested, &neutral, [&](Document, QRect, QString, bool) { ++outputs; });
        QTest::keyClicks(canvas, "520x240");
        QTest::keyClick(canvas, Qt::Key_Return);
        check("default selection retains typed capture dimensions with canvas focus",
              neutral.selectedGlobal().size() == QSize(520, 240) && outputs == 0 && canvas->tool() == Tool::Select);
        for (auto *button : neutral.findChildren<QToolButton *>())
            if (button->property("captureToolGroup").isValid()) {
                const Tool tool = Tool(button->defaultAction()->data().toInt());
                button->click();
                check("primary tool activates from default selection " + toolKey(tool), canvas->tool() == tool &&
                      button->isChecked() && options->isVisible());
                button->click();
                check("second primary click returns to default selection " + toolKey(tool),
                      canvas->tool() == Tool::Select && inactive() && !options->isVisible() &&
                      !canvas->testAttribute(Qt::WA_TransparentForMouseEvents) && canvas->cursor().shape() == Qt::ArrowCursor);
            }
        neutral.hide();
    }
    {
        CaptureOverlay moving(desktop, settings, QRect(-780, 70, 420, 240));
        moving.setAttribute(Qt::WA_DeleteOnClose, false);
        moving.setAttribute(Qt::WA_DontShowOnScreen);
        moving.show();
        auto *canvas = moving.findChild<AnnotationCanvas *>();
        auto *toolbar = moving.findChild<QWidget *>("captureEditorToolbar");
        auto *options = moving.findChild<QWidget *>("captureToolOptions");
        Document document = canvas->document();
        Annotation mark;
        mark.tool = Tool::Rectangle;
        mark.rect = QRectF(80, 60, 100, 70);
        document.annotations = {mark};
        canvas->setDocument(document);
        const QRect original = moving.selectedGlobal();
        const QRect local = canvas->geometry();
        auto sendMove = [](QWidget *widget, const QPoint &point) {
            QMouseEvent move(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(widget, &move);
        };
        auto drag = [&](const QPoint &from, const QPoint &to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, from);
            sendMove(canvas, to);
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, to);
        };
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {120, 90});
        check("selection hit testing keeps annotation body and handles interactive",
              !canvas->canStartBackgroundDrag({120, 90}) && !canvas->canStartBackgroundDrag({80, 60}) &&
              !canvas->canStartBackgroundDrag({130, 33}) && canvas->canStartBackgroundDrag({300, 180}));
        QTest::mousePress(canvas, Qt::LeftButton, {}, {300, 180});
        check("blank press deselects annotation without moving capture or hiding toolbar",
              canvas->selectedIndex() == -1 && moving.selectedGlobal() == original && toolbar->isVisible());
        const QPoint small(qMax(0, QApplication::startDragDistance() - 1), 0);
        sendMove(canvas, QPoint(300, 180) + small);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(300, 180) + small);
        check("sub-threshold mouse motion does not move capture", moving.selectedGlobal() == original && toolbar->isVisible());
        const QImage annotated = canvas->document().render();
        QTest::mousePress(canvas, Qt::LeftButton, {}, {300, 180});
        sendMove(canvas, {360, 215});
        check("blank drag moves entire capture while keeping size",
              moving.selectedGlobal() == original.translated(60, 35) && moving.selectedGlobal().size() == original.size());
        check("capture drag hides controls until release", !toolbar->isVisible() && !options->isVisible());
        const QImage preview = moving.grab().toImage();
        check("capture drag preview moves annotations with the selection",
              preview.pixelColor(local.topLeft() + QPoint(60, 35) + QPoint(100, 60)) == annotated.pixelColor(100, 60));
        QTest::mouseRelease(&moving, Qt::LeftButton, {}, local.topLeft() + QPoint(360, 215));
        const QRect translated = original.translated(60, 35);
        check("capture drag release updates background and source coordinates",
              toolbar->isVisible() && canvas->geometry() == translated.translated(-desktop.geometry.topLeft()) &&
              canvas->document().sourceRect == translated &&
              canvas->document().image == desktop.image.copy(translated.translated(-desktop.geometry.topLeft())));
        check("capture movement preserves relative annotation geometry and selection mode",
              canvas->document().annotations.first().toJson() == mark.toJson() && canvas->tool() == Tool::Select && !options->isVisible());
        drag({300, 180}, {2200, 1800});
        check("capture movement clamps to right and bottom desktop edges",
              moving.selectedGlobal() == QRect(desktop.geometry.bottomRight() - QPoint(original.width() - 1, original.height() - 1), original.size()));
        drag({300, 180}, {-1600, -1400});
        check("capture movement clamps to left and top desktop edges",
              moving.selectedGlobal() == QRect(desktop.geometry.topLeft(), original.size()));
        const QRect clamped = moving.selectedGlobal();
        drag({120, 90}, {140, 100});
        check("dragging annotation in selection mode keeps capture fixed",
              moving.selectedGlobal() == clamped && canvas->document().annotations.first().rect == QRectF(100, 70, 100, 70));
        drag({100, 70}, {80, 30});
        check("dragging selected annotation corner does not move capture",
              moving.selectedGlobal() == clamped && canvas->document().annotations.first().rect == QRectF(80, 30, 120, 110));
        canvas->undo();
        check("moving capture creates no annotation undo entry",
              canvas->document().annotations.first().rect == QRectF(100, 70, 100, 70));
        canvas->undo();
        check("annotation undo after capture movement restores original relative geometry",
              canvas->document().annotations.first().toJson() == mark.toJson() && moving.selectedGlobal() == clamped);
        moving.findChild<QAction *>("captureTool" + toolKey(Tool::Arrow))->trigger();
        drag({300, 180}, {350, 205});
        check("active drawing tool still annotates instead of moving capture",
              moving.selectedGlobal() == clamped && canvas->document().annotations.size() == 2 &&
              canvas->document().annotations.last().tool == Tool::Arrow);
        moving.findChild<QAction *>("captureTool" + toolKey(Tool::Arrow))->trigger();
        moving.setHistory(1, [](int) { return sampleDocument(); });
        QTest::keyClick(&moving, Qt::Key_Comma);
        canvas = moving.findChild<AnnotationCanvas *>();
        const QRect historyGeometry = canvas->geometry();
        drag({300, 180}, {340, 205});
        check("history preview drag does not move live capture selection", canvas->geometry() == historyGeometry);
        moving.hide();
    }
    {
        CaptureOverlay penOverlay(desktop, settings, QRect(-780, 90, 400, 230));
        penOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        penOverlay.show();
        auto *pen = penOverlay.findChild<QAction *>("captureTool" + toolKey(Tool::Pen));
        pen->trigger();
        auto *canvas = penOverlay.findChild<AnnotationCanvas *>();
        auto *opacity = penOverlay.findChild<ValueSlider *>("captureOpacity");
        auto *width = penOverlay.findChild<ValueSlider *>("captureStrokeWidth");
        check("pen exposes opacity without a separate marker tool", opacity->isVisible() && width->isVisible() &&
              !penOverlay.findChild<OptionStrip *>("captureEffectShape")->isVisible() && !penOverlay.findChild<QAction *>("captureTool8"));
        opacity->setValue(40);
        width->setValue(18);
        QTest::mousePress(canvas, Qt::LeftButton, {}, {40, 80});
        QMouseEvent draw(QEvent::MouseMove, QPointF(220, 80), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &draw);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {220, 80});
        const auto stroke = canvas->document().annotations.first();
        QImage expected(1, 1, QImage::Format_ARGB32_Premultiplied);
        expected.fill(canvas->document().image.pixelColor(120, 80));
        { QPainter blend(&expected); blend.fillRect(expected.rect(), stroke.color); }
        check("pen opacity slider produces a translucent stroke", stroke.tool == Tool::Pen &&
              stroke.color.alpha() == 102 && stroke.width == 18 &&
              canvas->document().render().pixelColor(120, 80) == expected.pixelColor(0, 0));
        penOverlay.grab().save(QDir(directory).filePath("capture-pen-opacity.png"));
        pen->trigger();
        pen->trigger();
        check("pen remembers chosen opacity when reactivated", canvas->style().color.alpha() == 102 && qAbs(opacity->value() - 40) < 0.01);
        opacity->setValue(100);
        check("pen still supports fully opaque drawing", canvas->style().color.alpha() == 255);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {280, 140});
        check("pen single click produces a visible dot", canvas->document().annotations.size() == 2 &&
              canvas->document().render().pixelColor(280, 140) == canvas->document().annotations.last().color);
        pen->trigger();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {280, 140});
        check("pen dot remains selectable in default selection mode", canvas->tool() == Tool::Select && canvas->selectedIndex() == 1);
        penOverlay.hide();
    }
    {
        Native::Desktop roomy = desktop;
        roomy.geometry.setHeight(desktop.geometry.height() + uiPixels(400));
        roomy.screens = {roomy.geometry};
        roomy.image = QImage(roomy.geometry.size(), QImage::Format_RGB32);
        roomy.image.fill(Qt::white);
        CaptureOverlay optionsOverlay(roomy, settings, QRect(-780, 90, 600, 210));
        optionsOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        optionsOverlay.show();
        optionsOverlay.findChild<QAction *>("captureTool1")->trigger();
        auto *options = optionsOverlay.findChild<QWidget *>("captureToolOptions");
        auto *canvas = optionsOverlay.findChild<AnnotationCanvas *>();
        check("secondary options avoid capture region when space permits",
              options->isVisible() && optionsOverlay.rect().contains(options->geometry()) &&
              !canvas->geometry().intersects(options->geometry()));
        optionsOverlay.hide();
    }
    {
        Native::Desktop resizeDesktop = desktop;
        resizeDesktop.geometry.setHeight(1000);
        resizeDesktop.screens = {resizeDesktop.geometry};
        resizeDesktop.image = QImage(resizeDesktop.geometry.size(), QImage::Format_RGB32);
        resizeDesktop.image.fill(Qt::white);
        const QRect initial(-780, 80, 460, 260);
        const QRect local = initial.translated(-resizeDesktop.geometry.topLeft());
        const QVector<QPoint> starts{local.topLeft(), {local.left() + 65, local.top()}, local.topRight(),
            {local.right(), local.top() + 65}, local.bottomRight(), {local.left() + 85, local.bottom()},
            local.bottomLeft(), {local.left(), local.top() + 75}};
        const QVector<QPoint> deltas{{-20, -15}, {0, -25}, {30, -15}, {30, 0}, {30, 25}, {0, 25}, {-20, 25}, {-20, 0}};
        const QVector<QRect> expected{initial.adjusted(-20, -15, 0, 0), initial.adjusted(0, -25, 0, 0),
            initial.adjusted(0, -15, 30, 0), initial.adjusted(0, 0, 30, 0), initial.adjusted(0, 0, 30, 25),
            initial.adjusted(0, 0, 0, 25), initial.adjusted(-20, 0, 0, 25), initial.adjusted(-20, 0, 0, 0)};
        auto borderMove = [](QWidget *border, const QPoint &overlayPoint, bool release) {
            const QPoint point = border->mapFromParent(overlayPoint);
            QMouseEvent move(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(border, &move);
            if (release)
                QTest::mouseRelease(border, Qt::LeftButton, {}, border->mapFromParent(overlayPoint));
        };
        for (int handle = 0; handle < starts.size(); ++handle) {
            CaptureOverlay resizing(resizeDesktop, settings, initial);
            resizing.setAttribute(Qt::WA_DontShowOnScreen);
            resizing.show();
            resizing.findChild<QAction *>("captureTool" + toolKey(Tool::Arrow))->trigger();
            auto *canvas = resizing.findChild<AnnotationCanvas *>();
            QTest::mousePress(canvas, Qt::LeftButton, {}, {80, 160});
            QMouseEvent draw(QEvent::MouseMove, QPointF(180, 60), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &draw);
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, {180, 60});
            auto style = canvas->style();
            style.width = 9;
            canvas->setStyle(style);
            auto *border = resizing.findChild<QWidget *>("captureResizeBorder");
            if (handle == 0) {
                check("resize border is present while annotation tool is active", border->isVisible());
                check("resize border leaves annotation interior interactive",
                      !border->mask().contains(border->mapFromParent(local.center())) &&
                      border->mask().contains(border->mapFromParent(starts[1])));
                check("widget hit testing sends edges to resize border", resizing.childAt(starts[1]) == border);
                check("widget hit testing sends interior to annotation canvas", resizing.childAt(local.center()) == canvas);
            }
            QTest::mousePress(border, Qt::LeftButton, {}, border->mapFromParent(starts[handle]));
            borderMove(border, starts[handle] + deltas[handle], handle != 0);
            if (handle == 0) {
                const QImage dragging = resizing.grab().toImage();
                check("annotations remain visible during border dragging",
                      dragging.pixelColor(local.topLeft() + QPoint(80, 160)) == canvas->document().render().pixelColor(80, 160));
                QTest::mouseRelease(border, Qt::LeftButton, {}, border->mapFromParent(starts[handle] + deltas[handle]));
            }
            const QString suffix = QString::number(handle);
            check("direct capture edge or corner resize " + suffix, resizing.selectedGlobal() == expected[handle] &&
                  canvas->document().sourceRect == expected[handle] && canvas->document().image.size() == expected[handle].size());
            const auto points = canvas->document().annotations.first().points;
            check("capture resize preserves annotation screen position " + suffix,
                  points.first() + QPointF(expected[handle].topLeft()) == QPointF(initial.topLeft() + QPoint(80, 160)) &&
                  points.last() + QPointF(expected[handle].topLeft()) == QPointF(initial.topLeft() + QPoint(180, 60)) &&
                  canvas->tool() == Tool::Arrow && resizing.findChild<QWidget *>("captureToolOptions")->isVisible());
            canvas->undo();
            check("capture resize translates annotation undo history " + suffix,
                  canvas->document().annotations.size() == 1 && canvas->document().annotations.first().width == 3 &&
                  canvas->document().annotations.first().points == points);
            canvas->redo();
            check("capture resize translates annotation redo history " + suffix,
                  canvas->document().annotations.first().width == 9 && canvas->document().annotations.first().points == points);
            if (handle == 0)
                resizing.grab().save(QDir(directory).filePath("capture-border-sliders.png"));
            resizing.hide();
        }
        CaptureOverlay crossing(resizeDesktop, settings, initial);
        crossing.setAttribute(Qt::WA_DontShowOnScreen);
        crossing.show();
        crossing.findChild<QAction *>("captureTool" + toolKey(Tool::Pen))->trigger();
        auto *border = crossing.findChild<QWidget *>("captureResizeBorder");
        QTest::mousePress(border, Qt::LeftButton, {}, border->mapFromParent(local.topLeft()));
        borderMove(border, local.bottomRight() + QPoint(20, 20), false);
        borderMove(border, local.bottomRight() + QPoint(50, 40), true);
        check("corner crossing retains the original opposite corner",
              crossing.selectedGlobal() == QRect(initial.bottomRight(), initial.bottomRight() + QPoint(50, 40)));
        QTest::mousePress(border, Qt::LeftButton, {}, border->mapFromParent(
            crossing.selectedGlobal().translated(-resizeDesktop.geometry.topLeft()).topLeft()));
        borderMove(border, {-40, -40}, true);
        check("direct corner resize clamps to captured desktop", crossing.selectedGlobal().topLeft() == resizeDesktop.geometry.topLeft());
        crossing.hide();
    }
    for (const auto &region : {QRect(-170, 430, 160, 80), QRect(-879, 1, 40, 30), desktop.geometry}) {
        CaptureOverlay edgeOverlay(desktop, settings, region);
        edgeOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        edgeOverlay.show();
        edgeOverlay.findChild<QAction *>("captureTool1")->trigger();
        QCoreApplication::processEvents();
        auto *toolbar = edgeOverlay.findChild<QWidget *>("captureEditorToolbar");
        check("edge capture toolbar fits " + QString::number(region.width()),
              toolbar && edgeOverlay.rect().contains(toolbar->geometry()));
        auto *options = edgeOverlay.findChild<QWidget *>("captureToolOptions");
        check("edge secondary options fit " + QString::number(region.width()),
              options && options->isVisible() && edgeOverlay.rect().contains(options->geometry()) &&
              !toolbar->geometry().intersects(options->geometry()));
        if (region.width() == 160)
            check("bottom-edge toolbar moves above selection", toolbar->geometry().bottom() < 430);
        edgeOverlay.hide();
    }
    {
        Native::Desktop monitors;
        monitors.geometry = QRect(-880, -200, 1760, 720);
        monitors.image = QImage(monitors.geometry.size(), QImage::Format_RGB32);
        monitors.image.fill(Qt::white);
        monitors.screens = {QRect(-880, -200, 880, 520), QRect(0, 0, 880, 520)};
        CaptureOverlay multiOverlay(monitors, settings, QRect(-400, -160, 500, 300));
        multiOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        multiOverlay.show();
        multiOverlay.findChild<QAction *>("captureTool1")->trigger();
        QCoreApplication::processEvents();
        auto *toolbar = multiOverlay.findChild<QWidget *>("captureEditorToolbar");
        check("multi-screen toolbar uses selection screen",
              toolbar && QRect(0, 0, 880, 520).contains(toolbar->geometry()));
        check("multi-screen secondary options use selection screen",
              QRect(0, 0, 880, 520).contains(multiOverlay.findChild<QWidget *>("captureToolOptions")->geometry()));
        multiOverlay.hide();
    }
    for (int i = 0; i < 4; ++i) {
        auto item = document;
        if (i == 1)
            item.cornerRadius = 24;
        const QString added = library.add(item);
        auto meta = library.record(added);
        meta.title = QStringList{QStringLiteral("工作流标注"), QStringLiteral("页面布局参考"),
                                 QStringLiteral("项目进度快照"), QStringLiteral("设计反馈")}[i];
        library.updateRecord(meta);
    }
    LibraryWindow manager(library, settings);
    manager.setAttribute(Qt::WA_DontShowOnScreen);
    manager.show();
    QCoreApplication::processEvents();
    QTest::qWait(100);
    manager.grab().save(QDir(directory).filePath("library-compact.png"));
    manager.resize(uiPixels(1120), uiPixels(760));
    QCoreApplication::processEvents();
    manager.grab().save(QDir(directory).filePath("library.png"));
    {
        auto *gallery = manager.findChild<QListWidget *>("historyGallery");
        QSignalSpy editRequest(&manager, &LibraryWindow::recordAction);
        if (gallery && gallery->count()) {
            const QPoint at = gallery->visualItemRect(gallery->item(0)).center();
            QTest::mouseClick(gallery->viewport(), Qt::LeftButton, {}, at);
            QTest::mouseDClick(gallery->viewport(), Qt::LeftButton, {}, at);
        }
        check("history cards retain double-click editing", editRequest.count() == 1 &&
              editRequest.first().at(1).toString() == "edit");
        auto *search = manager.findChild<QLineEdit *>("historySearch");
        search->setText("no matching screenshot");
        check("history search shows an empty result", gallery->isHidden());
        manager.grab().save(QDir(directory).filePath("library-search-empty.png"));
        search->clear();
    }
    manager.hide();
    auto *editor = new EditorWindow(document, settings);
    editor->setAttribute(Qt::WA_DontShowOnScreen);
    editor->show();
    QCoreApplication::processEvents();
    QTest::qWait(100);
    editor->grab().save(QDir(directory).filePath("editor-compact.png"));
    editor->resize(uiPixels(1240), uiPixels(820));
    QCoreApplication::processEvents();
    const auto *canvasArea = editor->findChild<QScrollArea *>("canvasArea");
    editor->canvas()->setZoom(qMin(1.0, qMin(double(canvasArea->viewport()->width() - 24) / document.image.width(),
                                           double(canvasArea->viewport()->height() - 24) / document.image.height())));
    editor->grab().save(QDir(directory).filePath("editor.png"));
    check("editor has no retired highlighter tool", !editor->findChild<QAction *>("editorTool8"));
    check("editor defaults to selection without a selection button",
          editor->canvas()->tool() == Tool::Select && !editor->findChild<QAction *>("editorTool0"));
    auto *lineType = editor->findChild<LineTypeButton *>("editorLineTypeButton");
    auto *lineAction = editor->findChild<QAction *>("editorTool" + toolKey(Tool::Line));
    auto *arrowAction = editor->findChild<QAction *>("editorTool" + toolKey(Tool::Arrow));
    check("editor offers one cycling line mode button",
          lineType && lineType->isVisible() && lineType->toolButtonStyle() == Qt::ToolButtonIconOnly && !lineType->menu());
    if (lineType && lineAction && arrowAction) {
        lineAction->trigger();
        check("editor line style updates the direct button",
              editor->canvas()->tool() == Tool::Line && lineType->tool() == Tool::Line && lineAction->isChecked());
        editor->canvas()->setTool(Tool::Rectangle);
        lineType->click();
        check("editor mode button toggles and activates the line tool", editor->canvas()->tool() == Tool::Arrow &&
              lineType->tool() == Tool::Arrow);
        QTest::keyClick(editor->canvas(), Qt::Key_Tab);
        check("editor Tab switch updates the mode button",
              editor->canvas()->tool() == Tool::Line && lineType->tool() == Tool::Line && lineAction->isChecked());
        editor->canvas()->setTool(Tool::Polyline);
        auto *polylineAction = editor->findChild<QAction *>("editorTool" + toolKey(Tool::Polyline));
        check("editor polyline remains separately available",
              polylineAction && polylineAction->isChecked() && !arrowAction->isChecked());
        lineType->click();
        lineType->click();
        check("repeated editor mode clicks alternate line types", editor->canvas()->tool() == Tool::Line &&
              lineType->tool() == Tool::Line && lineAction->isChecked());
        auto *canvas = editor->canvas();
        canvas->setDocument(sampleDocument());
        canvas->setTool(Tool::Arrow);
        canvas->setZoom(1);
        QTest::mousePress(canvas, Qt::LeftButton, {}, {100, 300});
        QMouseEvent drag(QEvent::MouseMove, QPointF(300, 300), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &drag);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {300, 300});
        const Annotation original = canvas->style();
        lineType->click();
        check("editor line button converts the selected arrow and preserves its properties",
              canvas->selectedIndex() == 0 && canvas->style().tool == Tool::Line &&
              canvas->style().points == original.points && canvas->style().color == original.color &&
              canvas->style().width == original.width && lineType->tool() == Tool::Line);
        canvas->undo();
        check("editor type button conversion supports undo", canvas->document().annotations.first().toJson() == original.toJson());
        canvas->redo();
        check("editor type button conversion supports redo", canvas->selectedIndex() == 0 &&
              canvas->style().tool == Tool::Line && lineType->tool() == Tool::Line);
        canvas->setTool(Tool::Select);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {200, 300});
        lineType->click();
        check("editor mode button edits objects in default selection mode", canvas->tool() == Tool::Select &&
              canvas->selectedIndex() == 0 && canvas->style().tool == Tool::Arrow && lineType->tool() == Tool::Arrow);
        lineType->grab().save(QDir(directory).filePath("editor-line-type-button.png"));
        editor->grab().save(QDir(directory).filePath("editor-line-types.png"));
    }
    {
        auto *width = editor->findChild<ValueSlider *>("editorStrokeWidth");
        auto *opacity = editor->findChild<ValueSlider *>("editorOpacity");
        auto *radius = editor->findChild<ValueSlider *>("editorCornerRadius");
        check("editor numeric style controls use sliders", width && opacity && radius &&
              editor->findChildren<QAbstractSpinBox *>().isEmpty() && editor->findChildren<ValueSlider *>().size() == 6);
        auto *canvas = editor->canvas();
        canvas->setDocument(sampleDocument());
        canvas->setTool(Tool::Rectangle);
        canvas->setZoom(1);
        auto initialStyle = canvas->style();
        initialStyle.width = 3;
        canvas->setStyle(initialStyle);
        QTest::mousePress(canvas, Qt::LeftButton, {}, {100, 100});
        QMouseEvent drag(QEvent::MouseMove, QPointF(220, 180), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &drag);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, {220, 180});
        width->slider()->setSliderDown(true);
        width->setValue(8);
        width->setValue(12);
        width->setValue(16);
        width->slider()->setSliderDown(false);
        check("slider drag updates selected annotation and value readout", canvas->style().width == 16 &&
              width->findChild<QLabel *>("valueReadout")->text() == "16.0");
        canvas->undo();
        check("one undo restores the entire slider drag", canvas->document().annotations.size() == 1 &&
              canvas->document().annotations.first().width == 3);
        canvas->redo();
        check("one redo restores the final slider value", canvas->style().width == 16 && width->value() == 16);
        opacity->setValue(50);
        check("editor opacity slider uses percent", canvas->style().color.alpha() == 128);
        radius->setValue(32);
        check("editor corner radius slider updates image", canvas->document().cornerRadius == 32);
        editor->grab().save(QDir(directory).filePath("editor-sliders.png"));

        auto legacyDocument = canvas->document();
        legacyDocument.annotations.first().lineStyle = int(Qt::DotLine);
        canvas->setDocument(Document::fromJson(legacyDocument.toJson(), legacyDocument.image));
        canvas->setTool(Tool::Select);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, {100, 140});
        auto *lineStyle = editor->findChild<OptionStrip *>("editorLineStyle");
        check("legacy dotted annotation has no removed option in editor", lineStyle && lineStyle->count() == 2 &&
              lineStyle->currentIndex() == -1 && canvas->selectedIndex() == 0);
        width->setValue(18);
        check("editing legacy annotation width preserves its stored line style", canvas->style().width == 18 &&
              canvas->style().lineStyle == int(Qt::DotLine));
        lineStyle->findChildren<QToolButton *>().at(1)->click();
        check("editor dashed icon updates the selected annotation", canvas->style().lineStyle == int(Qt::DashLine));
        canvas->undo();
        check("undo line-style icon restores the legacy stroke",
              canvas->document().annotations.first().lineStyle == int(Qt::DotLine));
    }
    editor->hide();
    delete editor;
    document.render().save(QDir(directory).filePath("annotated.png"));
    {
        class HiddenWindows : public QObject {
          public:
            bool eventFilter(QObject *object, QEvent *event) override {
                auto *widget = qobject_cast<QWidget *>(object);
                if (widget && widget->isWindow() && event->type() == QEvent::Polish)
                    widget->setAttribute(Qt::WA_DontShowOnScreen);
                return false;
            }
        } hiddenWindows;
        qApp->installEventFilter(&hiddenWindows);
        {
            Settings startupSettings(temporary.path() + "/startup");
            startupSettings.set("hotkeys/enabled", false);
            startupSettings.set("capture/superDrag", false);
            startupSettings.set("general/notifications", false);
            startupSettings.set("pins/restore", true);
            Library startupHistory(startupSettings);
            for (int i = 0; i < 2; ++i) {
                const QString legacyId = startupHistory.add(document);
                QJsonObject metadata = startupHistory.record(legacyId).toJson();
                metadata["pinned"] = true;
                metadata["history"] = false;
                metadata["closed"] = false;
                metadata["geometry"] = QJsonArray{100 + i * 200, 100, 200, 140};
                QFile file(startupSettings.historyDirectory() + '/' + legacyId + ".json");
                if (file.open(QIODevice::WriteOnly))
                    file.write(QJsonDocument(metadata).toJson());
            }
            const auto windowsBeforeStartup = QApplication::topLevelWidgets();
            Application startupController(startupSettings);
            LibraryWindow *startupWindow = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *candidate = qobject_cast<LibraryWindow *>(widget))
                    if (candidate != &manager)
                        startupWindow = candidate;
            bool extraWindow = false;
            for (auto *widget : QApplication::topLevelWidgets())
                if (!windowsBeforeStartup.contains(widget) && widget != startupWindow && widget->isVisible())
                    extraWindow = true;
            check("legacy images do not create floating startup windows", !extraWindow);
            Library migratedHistory(startupSettings);
            check("legacy image files remain accessible as history", migratedHistory.records().size() == 2 &&
                  migratedHistory.document(migratedHistory.records().first().id).image == document.image);
            const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
            const auto *tray = startupController.findChild<QSystemTrayIcon *>();
            check("startup tray visibility", tray && tray->isVisible() == trayAvailable);
            startupController.start({"EasySnip"});
            QCoreApplication::processEvents();
            check(trayAvailable ? "default startup stays in tray" : "startup without system tray exposes controls",
                  startupWindow && startupWindow->isVisible() == !trayAvailable);
            startupController.processArguments({"EasySnip"});
            check("repeated default launch preserves tray startup",
                  startupWindow && startupWindow->isVisible() == !trayAvailable);
            startupController.command("history");
            check("explicit history command opens manager", startupWindow && startupWindow->isVisible());
            if (startupWindow)
                startupWindow->hide();
            startupController.processArguments({"EasySnip", "--tray"});
            check("explicit tray startup compatibility", startupWindow && startupWindow->isVisible() == !trayAvailable);
        }
        Settings applicationSettings(temporary.path() + "/application");
        applicationSettings.set("hotkeys/enabled", false);
        applicationSettings.set("capture/superDrag", false);
        applicationSettings.set("general/tray", false);
        applicationSettings.set("general/notifications", false);
        applicationSettings.set("output/autoSave", true);
        applicationSettings.set("output/pin", true); // Exercise settings saved by the previous release.
        applicationSettings.set("output/copy", false);
        applicationSettings.set("output/directory", temporary.path() + "/exports");
        const QString input = temporary.path() + "/input.png";
        document.image.save(input);
        {
            Application controller(applicationSettings);
            controller.start({"EasySnip"});
            bool fallbackVisible = false;
            for (auto *widget : QApplication::topLevelWidgets())
                if (qobject_cast<LibraryWindow *>(widget) && widget != &manager && widget->isVisible())
                    fallbackVisible = true;
            check("disabled tray exposes history controls", fallbackVisible);
            controller.processArguments({"EasySnip", "--open", input});
            EditorWindow *opened = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *candidate = qobject_cast<EditorWindow *>(widget))
                    opened = candidate;
            check("application opens image editor",
                  opened && opened->canvas()->document().image.size() == document.image.size());
            if (opened) {
                opened->outputRequested(opened->canvas()->document(), "pin", false);
                Library afterRetiredOutput(applicationSettings);
                check("retired pin output is rejected", afterRetiredOutput.records().isEmpty());
                for (auto *button : opened->findChildren<QPushButton *>())
                    if (button->text() == QStringLiteral("完成")) {
                        button->click();
                        break;
                    }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            const auto *clipboard = QApplication::clipboard()->mimeData();
            check("combined output clipboard image and file",
                  clipboard && clipboard->hasImage() && clipboard->hasUrls() &&
                      QFile::exists(clipboard->urls().first().toLocalFile()));
            check("combined output auto-save",
                  QDir(temporary.path() + "/exports").entryList({"*.png"}, QDir::Files).size() == 1);
            Library outputLibrary(applicationSettings);
            check("combined output persists editable history",
                  outputLibrary.records().size() == 1 &&
                  outputLibrary.document(outputLibrary.records().first().id).image == document.image);
            controller.command("clipboard");
            controller.command("toggle-pins");
            controller.command("recover");
            Library afterRemovedCommands(applicationSettings);
            check("retired commands cannot create image windows or records", afterRemovedCommands.records().size() == 1);
            if (native) {
                controller.command("capture");
                QTest::qWait(350);
                QPointer<CaptureOverlay> capture;
                bool standaloneEditor = false;
                for (auto *widget : QApplication::topLevelWidgets()) {
                    if (auto *candidate = qobject_cast<CaptureOverlay *>(widget))
                        if (candidate->isVisible())
                            capture = candidate;
                    if (qobject_cast<EditorWindow *>(widget) && widget->isVisible())
                        standaloneEditor = true;
                }
                check("application capture opens overlay", capture && !standaloneEditor);
                if (capture) {
                    QTest::mousePress(capture, Qt::LeftButton, {}, QPoint(100, 100));
                    QMouseEvent regionDrag(QEvent::MouseMove, QPointF(400, 300), Qt::NoButton,
                                            Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(capture, &regionDrag);
                    QTest::mouseRelease(capture, Qt::LeftButton, {}, QPoint(400, 300));
                    capture->findChild<QAction *>("captureTool1")->trigger();
                    auto *inlineCanvas = capture->findChild<AnnotationCanvas *>();
                    QTest::mousePress(inlineCanvas, Qt::LeftButton, {}, QPoint(20, 20));
                    QMouseEvent annotationDrag(QEvent::MouseMove, QPointF(150, 100), Qt::NoButton,
                                               Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(inlineCanvas, &annotationDrag);
                    QTest::mouseRelease(inlineCanvas, Qt::LeftButton, {}, QPoint(150, 100));
                    const Document expected = inlineCanvas->document();
                    const QRect expectedRegion = capture->selectedGlobal();
                    const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
                    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
                    bool saveDialogCanceled = false;
                    QTimer::singleShot(0, [&] {
                        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                        if (dialog) {
                            saveDialogCanceled = true;
                            dialog->reject();
                        }
                    });
                    capture->findChild<QAction *>("captureOutput-save-as")->trigger();
                    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
                    Library afterCancel(applicationSettings);
                    check("save dialog cancel keeps capture and annotations",
                          saveDialogCanceled && capture && capture->isVisible() &&
                              inlineCanvas->document().render() == expected.render() &&
                              afterCancel.records().size() == 1);
                    capture->findChild<QAction *>("captureOutput-finish")->trigger();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    Library capturedLibrary(applicationSettings);
                    check("inline completion closes application capture", capture.isNull());
                    check("inline completion copies composited image",
                          QApplication::clipboard()->image() == expected.render());
                    check("inline completion stores editable history",
                          capturedLibrary.records().size() == 2 &&
                              capturedLibrary.document(capturedLibrary.records().first().id).annotations.size() == 1);
                    check("inline completion updates reusable capture region",
                          applicationSettings.value("capture/lastRect").toRect() == expectedRegion);
                    check("inline completion retains auto-save output",
                          QDir(temporary.path() + "/exports").entryList({"*.png"}, QDir::Files).size() == 2);
                    for (bool includeCanceled : {false, true}) {
                        applicationSettings.set("history/includeCanceled", includeCanceled);
                        controller.command("last");
                        QTest::qWait(350);
                        for (auto *widget : QApplication::topLevelWidgets())
                            if (auto *candidate = qobject_cast<CaptureOverlay *>(widget))
                                if (candidate->isVisible())
                                    capture = candidate;
                        check("reuse last capture creates selection", capture && capture->selectedGlobal() == expectedRegion);
                        if (capture) {
                            if (includeCanceled) {
                                QTest::keyClick(capture, Qt::Key_Comma);
                                auto *replayed = capture->findChild<AnnotationCanvas *>();
                                check("application history replay loads saved annotations", replayed &&
                                      replayed->document().annotations.size() == 1 &&
                                      replayed->document().image == expected.image);
                            }
                            QTest::keyClick(capture, Qt::Key_Escape);
                            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                        }
                        Library canceledHistory(applicationSettings);
                        check(includeCanceled ? "canceled capture retained when enabled" : "canceled capture excluded by default",
                              canceledHistory.records().size() == (includeCanceled ? 3 : 2));
                    }
                }
            }
        }
        qApp->removeEventFilter(&hiddenWindows);
    }
    {
        Native::WindowSnapshot popup;
        popup.handle = 1; // A captured popup can be destroyed after the overlay takes focus.
        popup.geometry = QRect(-500, 100, 400, 200);
        popup.executable = "C:/Windows/explorer.exe";
        popup.cached = true;
        const QRect icon(-400, 130, 70, 70);
        const QRect row(-480, 120, 350, 90);
        popup.nativeElements = {popup.geometry};
        popup.elements = {popup.geometry, row, icon, icon, QRect(-420, 140, 150, 70)};
        const QVector<QRect> expected{icon, row, popup.geometry};
        check("snapshot selects icon and nested parents at negative coordinates",
              Native::cachedElementsAt(icon.center(), {popup}) == expected);
        check("closed popup retains captured icon geometry",
              Native::elementsAt(icon.center(), {}, {}, {popup}) == expected);
        check("native algorithm override applies to cached popups",
              Native::cachedElementsAt(icon.center(), {popup}, {}, {"explorer.exe"}) == QVector<QRect>{popup.geometry} &&
              Native::elementsAt(icon.center(), {}, {"explorer.exe"}, {popup}) == QVector<QRect>{popup.geometry});
        auto front = popup;
        front.handle = 2;
        front.executable = "front.exe";
        front.elements = {front.geometry};
        front.cached = false;
        check("incomplete front snapshot leaves live detection available",
              Native::cachedElementsAt(icon.center(), {front, popup}).isEmpty());
        check("window detection respects snapshot stacking order",
              Native::elementsAt(icon.center(), {}, {}, {front, popup}) == QVector<QRect>{front.geometry});
        check("excluded window exposes next captured window",
              Native::cachedElementsAt(icon.center(), {front, popup}, {"front.exe"}) == expected);
        check("snapshot misses do not inspect newly opened windows",
              Native::windowAt(QPoint(5, 5), {}, {popup}).isEmpty());

        Native::Desktop frozen;
        frozen.image = sampleDocument().image;
        frozen.geometry = QRect(QGuiApplication::primaryScreen()->geometry().topLeft(), frozen.image.size());
        frozen.screens = {frozen.geometry};
        const QPoint offset = frozen.geometry.topLeft() + QPoint(420, 100) - popup.geometry.topLeft();
        popup.geometry.translate(offset);
        popup.elements = {icon.translated(offset), row.translated(offset), popup.geometry};
        popup.nativeElements = {popup.geometry};
        frozen.windows = {popup};
        const QRect frozenIcon = icon.translated(offset);
        const QRect frozenRow = row.translated(offset);
        const QPoint localPoint = frozenIcon.center() - frozen.geometry.topLeft();
        Settings detectionSettings(temporary.path() + "/detection");
        detectionSettings.set("capture/elements", true);
        detectionSettings.set("capture/cancelOnDeactivate", false);
        detectionSettings.set("capture/animateDetection", false);
        detectionSettings.set("magnifier/enabled", false);
        CaptureOverlay detection(frozen, detectionSettings);
        detection.setAttribute(Qt::WA_DeleteOnClose, false);
        detection.setAttribute(Qt::WA_DontShowOnScreen, !native);
        detection.show();
        bool detectionReady = true;
        if (native) {
            detection.raise();
            detection.activateWindow();
            detectionReady = QTest::qWaitForWindowExposed(&detection);
        }
        // Feed the synthetic snapshot a fixed point; desktop mouse movement must not change the fixture.
        detection.detectionTimer_.stop();
        detection.updateDetectionAt(frozenIcon.center());
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("capture click selects frozen tray icon", detectionReady && detection.selectedGlobal() == frozenIcon);
        if (detection.selectedGlobal() != frozenIcon)
            qWarning() << "Frozen icon selection" << detection.selectedGlobal() << "expected" << frozenIcon
                       << "overlay" << detection.geometry() << "cursor" << QCursor::pos();
        QTest::mouseClick(&detection, Qt::RightButton, {}, localPoint);
        detection.updateDetectionAt(frozenIcon.center());
        QWheelEvent parentWheel(localPoint, frozenIcon.center(), QPoint(), QPoint(0, 120),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&detection, &parentWheel);
        detection.updateDetectionAt(frozenIcon.center()); // Reapplying the cache must preserve the wheel-selected parent.
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("capture wheel retains frozen parent selection", detection.selectedGlobal() == frozenRow);
        if (detection.selectedGlobal() != frozenRow) {
            QString detail;
            QDebug(&detail) << "Frozen parent selection" << detection.selectedGlobal() << "expected" << frozenRow
                            << "cursor" << QCursor::pos() << "point" << frozenIcon.center();
            QTextStream(stdout) << detail << '\n';
        }
        QTest::mouseClick(&detection, Qt::RightButton, {}, localPoint);
        QTest::keyClick(&detection, Qt::Key_Tab);
        detection.updateDetectionAt(frozenIcon.center());
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("Tab restores window-only selection", detection.selectedGlobal() == popup.geometry);
        if (detection.selectedGlobal() != popup.geometry) {
            QString detail;
            QDebug(&detail) << "Frozen window selection" << detection.selectedGlobal() << "expected" << popup.geometry
                            << "cursor" << QCursor::pos() << "point" << frozenIcon.center();
            QTextStream(stdout) << detail << '\n';
        }
        detection.hide();
        QCursor::setPos(cursorBackup);
    }
    if (native) {
        // The real Win32 provider exercises our COM ABI and hit-testing with an
        // overlay present. Pump the GUI thread while the MTA worker queries it.
        const QPoint origin = QGuiApplication::primaryScreen()->geometry().topLeft() + QPoint(50, 80);
        HWND fixture = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"EasySnip element test",
                                      WS_POPUP | WS_VISIBLE, origin.x(), origin.y(), 420, 180,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND button = CreateWindowExW(0, L"BUTTON", L"Test control", WS_CHILD | WS_VISIBLE,
                                     60, 40, 120, 52, fixture, nullptr, GetModuleHandleW(nullptr), nullptr);
        const QRect buttonBounds(origin + QPoint(60, 40), QSize(120, 52));
        const QPoint point = buttonBounds.center();
        auto query = QtConcurrent::run([fixture] { return Native::automationElements(quintptr(fixture)); });
        QElapsedTimer queryTimer;
        queryTimer.start();
        while (!query.isFinished() && queryTimer.elapsed() < 5000)
            QTest::qWait(10);
        check("UI Automation reads real child control bounds",
              fixture && button && query.isFinished() && query.result().contains(buttonBounds));
        Native::WindowSnapshot snapshot;
        snapshot.handle = quintptr(fixture);
        snapshot.geometry = QRect(origin, QSize(420, 180));
        snapshot.executable = "fixture.exe";
        snapshot.elements = {snapshot.geometry};
        HWND covering = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"STATIC", L"Overlay test",
                                       WS_POPUP | WS_VISIBLE, origin.x(), origin.y(), 420, 180,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        query = QtConcurrent::run([point, snapshot] { return Native::elementsAt(point, {}, {}, {snapshot}); });
        queryTimer.restart();
        while (!query.isFinished() && queryTimer.elapsed() < 5000)
            QTest::qWait(10);
        check("live detection refines partial snapshot underneath an overlay",
              covering && query.isFinished() && query.result().value(0) == buttonBounds);
        query = QtConcurrent::run([point, snapshot] { return Native::elementsAt(point, {}, {"fixture.exe"}, {snapshot}); });
        queryTimer.restart();
        while (!query.isFinished() && queryTimer.elapsed() < 5000)
            QTest::qWait(10);
        check("native child fallback stays inside the captured window",
              query.isFinished() && query.result().value(0) == buttonBounds);
        DestroyWindow(covering);
        DestroyWindow(button);
        auto containerQuery = QtConcurrent::run([fixture] {
            bool complete = true;
            Native::automationElements(quintptr(fixture), nullptr, &complete);
            return !complete;
        });
        queryTimer.restart();
        while (!containerQuery.isFinished() && queryTimer.elapsed() < 5000)
            QTest::qWait(10);
        check("container-only UI Automation snapshot remains incomplete",
              containerQuery.isFinished() && containerQuery.result());
        DestroyWindow(fixture);

        auto rootQuery = QtConcurrent::run([] {
            bool complete = true;
            const auto bounds = Native::automationElements(0, nullptr, &complete);
            return bounds.isEmpty() && !complete;
        });
        queryTimer.restart();
        while (!rootQuery.isFinished() && queryTimer.elapsed() < 5000)
            QTest::qWait(10);
        check("invalid UI Automation HWND stays incomplete", rootQuery.isFinished() && rootQuery.result());
        const QStringList arguments{"EasySnip.exe", "--data-dir", QStringLiteral("C:\\测试 目录\\"),
                                    "a\\\"b", "", "--admin"};
        const std::wstring commandLine = Native::quoteArguments(arguments).toStdWString();
        int argumentCount = 0;
        LPWSTR *parsedArguments = CommandLineToArgvW(commandLine.c_str(), &argumentCount);
        QStringList parsed;
        if (parsedArguments) {
            for (int i = 0; i < argumentCount; ++i)
                parsed.append(QString::fromWCharArray(parsedArguments[i]));
            LocalFree(parsedArguments);
        }
        check("elevation arguments round trip", parsed == arguments);
        const auto live = Native::captureDesktop();
        check("native desktop capture", !live.image.isNull() && live.image.size() == live.geometry.size());
        check("native screen enumeration", !live.screens.isEmpty());
        bool consistent = live.screens.size() == QGuiApplication::screens().size();
        for (auto *screen : QGuiApplication::screens())
            consistent = consistent && live.screens.contains(screen->geometry());
        check("Qt and Win32 coordinate agreement", consistent);
        GlobalInput input;
        QString error;
        check("native global hotkey registration",
              input.registerShortcut(100, QKeySequence("Ctrl+Alt+Shift+F11"), &error));
        check("repeated hotkey registration is idempotent",
              input.registerShortcut(100, QKeySequence("Ctrl+Alt+Shift+F11"), &error));
        const bool secondRegistered = input.registerShortcut(101, QKeySequence("Ctrl+Alt+Shift+F10"), &error);
        bool alreadyInUse = false;
        check("hotkey conflict reports Windows code", secondRegistered &&
              !input.registerShortcut(100, QKeySequence("Ctrl+Alt+Shift+F10"), &error, &alreadyInUse) &&
              alreadyInUse && error.contains("1409"));
        QSignalSpy activated(&input, &GlobalInput::activated);
        MSG hotkey{};
        hotkey.message = WM_HOTKEY;
        hotkey.wParam = 100;
        hotkey.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11);
        long result = 0;
        check("failed rebind retains original dispatch", input.nativeEventFilter({}, &hotkey, &result) &&
              activated.count() == 1 && activated.first().first().toInt() == 100);
        hotkey.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F10);
        check("stale shortcut messages are ignored", !input.nativeEventFilter({}, &hotkey, &result));
        hotkey.wParam = 999;
        check("unowned shortcut messages are ignored", !input.nativeEventFilter({}, &hotkey, &result));
        check("reserved key rejected without error pointer", !input.registerShortcut(103, QKeySequence("F12"), nullptr));
        activated.clear();
        const bool f1Registered = input.registerShortcut(103, QKeySequence("F1"), &error);
        check("F1 system-wide registration", f1Registered);
        if (f1Registered) {
            // SendInput must target a window at this process's integrity level.
            HWND inputTarget = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"EasySnip hotkey test",
                                               WS_POPUP | WS_VISIBLE, -32000, -32000, 1, 1,
                                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetForegroundWindow(inputTarget);
            QTest::qWait(30);
            INPUT events[3]{};
            for (auto &event : events) {
                event.type = INPUT_KEYBOARD;
                event.ki.wVk = VK_F1;
            }
            events[2].ki.dwFlags = KEYEVENTF_KEYUP;
            const UINT sent = SendInput(3, events, sizeof(INPUT));
            QTest::qWait(150);
            check("F1 reaches Qt native dispatcher once per press", sent == 3 && activated.count() == 1 &&
                  activated.first().first().toInt() == 103);
            DestroyWindow(inputTarget);
        }
        input.clear();
        const bool released = RegisterHotKey(nullptr, 0x7e01, MOD_NOREPEAT, VK_F1);
        check("F1 released on input shutdown", released);
        if (released)
            UnregisterHotKey(nullptr, 0x7e01);
        QWidget shareOwner;
        shareOwner.setAttribute(Qt::WA_DontShowOnScreen);
        const QString shareFile = temporary.path() + "/share.png";
        document.image.save(shareFile);
        error.clear();
        check("WinRT bitmap share preparation", Native::shareImage(&shareOwner, shareFile, &error, false));
        if (!error.isEmpty())
            QTextStream(stdout) << error << '\n';
        settings.set("runtime/gpuPreview", true);
        AnnotationCanvas accelerated(document, settings);
        accelerated.setAttribute(Qt::WA_DontShowOnScreen);
        accelerated.show();
        QTest::qWait(600);
        check("OpenGL preview initialization", accelerated.acceleratedPreview());
        accelerated.hide();
        settings.set("runtime/gpuPreview", false);
    }
    // Render actual widgets at common Windows display scales, including the modal settings pages.
    // All settings use the temporary test directory and the dialog is always canceled.
    {
        class HidePreviewDialogs final : public QObject {
          public:
            bool eventFilter(QObject *object, QEvent *event) override {
                if (event->type() == QEvent::Polish && object->objectName() == "settingsDialog")
                    if (auto *widget = qobject_cast<QWidget *>(object))
                        widget->setAttribute(Qt::WA_DontShowOnScreen);
                return false;
            }
        } hideDialogs;
        qApp->installEventFilter(&hideDialogs);
        for (int percent : {100, 150, 200}) {
            settings.set("appearance/uiScale", percent / 100.0);
            applyAppearance(settings);
            const QString suffix = QString::number(percent);
            LibraryWindow scaledLibrary(library, settings);
            scaledLibrary.setAttribute(Qt::WA_DontShowOnScreen);
            scaledLibrary.show();
            scaledLibrary.resize(uiPixels(1120), uiPixels(760));
            auto *scaledEditor = new EditorWindow(document, settings);
            scaledEditor->setAttribute(Qt::WA_DontShowOnScreen);
            scaledEditor->show();
            scaledEditor->resize(uiPixels(1240), uiPixels(820));
            QCoreApplication::processEvents();
            QTest::qWait(20);
            scaledLibrary.grab().save(QDir(directory).filePath("library-" + suffix + ".png"));
            scaledEditor->grab().save(QDir(directory).filePath("editor-" + suffix + ".png"));
            auto *strip = scaledEditor->findChild<QFrame *>("editorToolStrip");
            const auto *toolScroll = strip->findChild<QScrollArea *>();
            check("editor toolbar fits at " + suffix + " percent", toolScroll->horizontalScrollBar()->maximum() == 0);
            scaledLibrary.hide();
            scaledEditor->hide();
            delete scaledEditor;

            Native::Desktop previewDesktop;
            previewDesktop.image = document.image.scaled(uiPixels(1280), uiPixels(800), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            previewDesktop.geometry = previewDesktop.image.rect();
            previewDesktop.screens = {previewDesktop.geometry};
            CaptureOverlay scaledCapture(previewDesktop, settings,
                QRect(uiPixels(100), uiPixels(120), uiPixels(900), uiPixels(360)));
            scaledCapture.setAttribute(Qt::WA_DontShowOnScreen);
            scaledCapture.show();
            scaledCapture.findChild<QAction *>("captureTool" + toolKey(Tool::Pen))->trigger();
            QCoreApplication::processEvents();
            scaledCapture.grab().save(QDir(directory).filePath("capture-" + suffix + ".png"));
            auto *toolbar = scaledCapture.findChild<QFrame *>("captureEditorToolbar");
            auto *options = scaledCapture.findChild<QFrame *>("captureToolOptions");
            check("capture panels fit at " + suffix + " percent",
                  scaledCapture.rect().contains(toolbar->geometry()) && scaledCapture.rect().contains(options->geometry()));
            for (Tool tool : {Tool::Rectangle, Tool::Arrow, Tool::Mosaic, Tool::Eraser, Tool::Text}) {
                scaledCapture.findChild<QAction *>("captureTool" + toolKey(tool))->trigger();
                QCoreApplication::processEvents();
                check("direct tool panel fits " + toolKey(tool) + " at " + suffix,
                      scaledCapture.rect().contains(options->geometry()) && options->findChildren<QComboBox *>().isEmpty());
                options->grab().save(QDir(directory).filePath("tool-panel-" + toolKey(tool) + "-" + suffix + ".png"));
            }
            scaledCapture.hide();

            bool navigated = false;
            QTimer::singleShot(0, qApp, [&] {
                QDialog *dialog = nullptr;
                for (auto *widget : QApplication::topLevelWidgets())
                    if (widget->objectName() == "settingsDialog")
                        dialog = qobject_cast<QDialog *>(widget);
                if (!dialog)
                    return;
                dialog->resize(uiPixels(960), uiPixels(760));
                auto *navigation = dialog->findChild<QListWidget *>("settingsNavigation");
                auto *pages = dialog->findChild<QStackedWidget *>();
                navigated = navigation && pages && navigation->count() == 4;
                if (navigated) {
                    for (int row = 0; row < 4; ++row) {
                        navigation->setCurrentRow(row);
                        QCoreApplication::processEvents();
                        navigated = navigated && pages->currentIndex() == row && pages->currentWidget()->isVisible();
                        dialog->grab().save(QDir(directory).filePath(QString("settings-%1-%2.png").arg(row).arg(suffix)));
                    }
                    if (percent == 100) {
                        navigation->setCurrentRow(0);
                        dialog->resize(736, 552);
                        QCoreApplication::processEvents();
                        dialog->grab().save(QDir(directory).filePath("settings-compact.png"));
                    }
                }
                // Exercise Cancel after making an unsaved edit.
                const auto edits = dialog->findChildren<QLineEdit *>();
                if (!edits.isEmpty())
                    edits.first()->setText("unsaved preview edit");
                dialog->reject();
            });
            const QString before = settings.value("appearance/accent", "#0067c0").toString();
            const bool accepted = showSettings(settings, nullptr);
            check("settings categories navigate at " + suffix + " percent", navigated);
            check("cancel settings preserves preferences at " + suffix + " percent",
                  !accepted && settings.value("appearance/accent", "#0067c0").toString() == before);
        }
        qApp->removeEventFilter(&hideDialogs);
        settings.set("appearance/uiScale", 1.0);
        applyAppearance(settings);
    }
    QJsonObject report{{"qt", qVersion()}, {"native", native}, {"elevated", Native::isElevated()},
                       {"failed", failures}, {"checks", checks}};
    QFile reportFile(QDir(directory).filePath("results.json"));
    if (reportFile.open(QIODevice::WriteOnly))
        reportFile.write(QJsonDocument(report).toJson());
    QTextStream(stdout) << checks.size() - failures << "/" << checks.size() << " passed\n";
    QApplication::clipboard()->setMimeData(clipboardBackup);
    QCursor::setPos(cursorBackup);
    return failures == 0 ? 0 : 1;
}
