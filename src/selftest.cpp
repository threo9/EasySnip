#include "application.h"
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
                         QStringLiteral("整理截图与参考资料\n高亮重要信息\n保存并回看历史"));
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
    check("eraser alpha", qAlpha(erased.render().pixel(30, 30)) == 0);
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
        editCanvas.setTool(Tool::Highlighter);
        auto brushStyle = editCanvas.style();
        brushStyle.shape = 3;
        brushStyle.width = 20;
        editCanvas.setStyle(brushStyle);
        drag(&editCanvas, {100, 300}, {200, 330}, Qt::ShiftModifier);
        const Annotation stroke = editCanvas.document().annotations.first();
        check("marker brush direction constraint", stroke.points.size() >= 2 &&
              qAbs(stroke.points.last().y() - stroke.points.first().y()) < 0.01);
        for (Tool effect : {Tool::Highlighter, Tool::Mosaic, Tool::Blur, Tool::Eraser}) {
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
        QToolButton *rectangleButton = nullptr;
        for (auto *button : inlineOverlay.findChildren<QToolButton *>())
            if (button->defaultAction() && button->defaultAction()->objectName() == "captureTool1")
                rectangleButton = button;
        check("rectangle tool is directly available", rectangleButton && rectangleButton->isVisible());
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
            inlineOverlay.findChild<QAction *>("adjustCaptureRegion")->trigger();
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
    for (const auto &region : {QRect(-170, 430, 160, 80), QRect(-879, 1, 40, 30), desktop.geometry}) {
        CaptureOverlay edgeOverlay(desktop, settings, region);
        edgeOverlay.setAttribute(Qt::WA_DontShowOnScreen);
        edgeOverlay.show();
        edgeOverlay.findChild<QAction *>("captureTool1")->trigger();
        QCoreApplication::processEvents();
        auto *toolbar = edgeOverlay.findChild<QWidget *>("captureEditorToolbar");
        check("edge capture toolbar fits " + QString::number(region.width()),
              toolbar && edgeOverlay.rect().contains(toolbar->geometry()));
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
        QCoreApplication::processEvents();
        auto *toolbar = multiOverlay.findChild<QWidget *>("captureEditorToolbar");
        check("multi-screen toolbar uses selection screen",
              toolbar && QRect(0, 0, 880, 520).contains(toolbar->geometry()));
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
    manager.grab().save(QDir(directory).filePath("library.png"));
    manager.hide();
    auto *editor = new EditorWindow(document, settings);
    editor->setAttribute(Qt::WA_DontShowOnScreen);
    editor->show();
    QCoreApplication::processEvents();
    QTest::qWait(100);
    editor->grab().save(QDir(directory).filePath("editor.png"));
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
        QCursor::setPos(frozenIcon.center());
        QTest::qWait(100);
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("capture click selects frozen tray icon", detectionReady && detection.selectedGlobal() == frozenIcon);
        if (detection.selectedGlobal() != frozenIcon)
            qWarning() << "Frozen icon selection" << detection.selectedGlobal() << "expected" << frozenIcon
                       << "overlay" << detection.geometry() << "cursor" << QCursor::pos();
        QTest::mouseClick(&detection, Qt::RightButton, {}, localPoint);
        QTest::qWait(100);
        QWheelEvent parentWheel(localPoint, frozenIcon.center(), QPoint(), QPoint(0, 120),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&detection, &parentWheel);
        QTest::qWait(100); // Reapplying the cache must preserve the wheel-selected parent.
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("capture wheel retains frozen parent selection", detection.selectedGlobal() == frozenRow);
        QTest::mouseClick(&detection, Qt::RightButton, {}, localPoint);
        QTest::keyClick(&detection, Qt::Key_Tab);
        QTest::qWait(100);
        QTest::mouseClick(&detection, Qt::LeftButton, {}, localPoint);
        check("Tab restores window-only selection", detection.selectedGlobal() == popup.geometry);
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
