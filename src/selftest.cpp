#include "application.h"
#include <QtWidgets>
#include <QtTest>
#include <MultiFormatWriter.h>
#include <BitMatrix.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtConcurrent>

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
                         QStringLiteral("整理截图与参考资料\n高亮重要信息\n保存到项目分组"));
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
    record.pinned = true;
    record.geometry = QRect(-500, 120, 500, 300);
    library.updateRecord(record);
    Library reloaded(settings);
    check("persistent pin restoration", reloaded.record(id).geometry == record.geometry &&
                                            reloaded.record(id).pinned &&
                                            reloaded.document(id).annotations.size() == 3);
    library.setGroups({QStringLiteral("默认分组"), QStringLiteral("项目 A")});
    library.renameGroup(QStringLiteral("默认分组"), QStringLiteral("参考"));
    check("group rename references", library.record(id).group == QStringLiteral("参考"));
    const QString backup = temporary.path() + "/test.esbackup";
    check("backup export", library.exportBackup(backup));
    const int count = library.records().size();
    check("backup import", library.importBackup(backup) && library.records().size() == count * 2);
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
    check("history eviction preserves pins", !library.record(id).history && library.record(id).pinned);
    auto closed = library.record(id);
    closed.closed = true;
    closed.closedAt = QDateTime::currentDateTime();
    library.updateRecord(closed);
    settings.set("pins/recoverLimit", 0);
    library.prune();
    check("closed pin recovery limit", library.record(id).id.isEmpty());
    settings.set("history/limit", 100);
    settings.set("pins/recoverLimit", 20);
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
        Settings applicationSettings(temporary.path() + "/application");
        applicationSettings.set("hotkeys/enabled", false);
        applicationSettings.set("capture/superDrag", false);
        applicationSettings.set("general/tray", false);
        applicationSettings.set("general/notifications", false);
        applicationSettings.set("output/autoSave", true);
        applicationSettings.set("output/pin", true);
        applicationSettings.set("output/directory", temporary.path() + "/exports");
        const QString input = temporary.path() + "/input.png";
        document.image.save(input);
        {
            Application controller(applicationSettings);
            controller.processArguments({"EasySnip", "--open", input});
            EditorWindow *opened = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *candidate = qobject_cast<EditorWindow *>(widget))
                    opened = candidate;
            check("application opens image editor",
                  opened && opened->canvas()->document().image.size() == document.image.size());
            if (opened) {
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
            check("combined output persistent pin",
                  outputLibrary.records().size() == 1 && outputLibrary.records().first().pinned);
            PinWindow *pinned = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *candidate = qobject_cast<PinWindow *>(widget))
                    pinned = candidate;
            if (pinned)
                pinned->close();
            Library closedLibrary(applicationSettings);
            check("pin close event persists recoverable state",
                  closedLibrary.records().size() == 1 && closedLibrary.records().first().closed);
            controller.command("recover");
            Library recoveredLibrary(applicationSettings);
            check("application restores closed pin",
                  recoveredLibrary.records().size() == 1 && !recoveredLibrary.records().first().closed);
            auto *html = new QMimeData;
            html->setHtml("<h2>EasySnip HTML</h2><p>clipboard rendering</p>");
            QApplication::clipboard()->setMimeData(html);
            controller.command("clipboard");
            Library htmlLibrary(applicationSettings);
            check("HTML clipboard pin",
                  htmlLibrary.records().size() == 2 &&
                      !htmlLibrary.document(htmlLibrary.records().first().id).image.isNull());
        }
        qApp->removeEventFilter(&hiddenWindows);
    }
    if (native) {
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
        input.clear();
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
        QTcpServer server;
        const bool listening = server.listen(QHostAddress::LocalHost);
        QByteArray response;
        QBuffer outputBuffer(&response);
        outputBuffer.open(QIODevice::WriteOnly);
        document.image.save(&outputBuffer, "PNG");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, response] {
                    socket->readAll();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                                  QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" +
                                  response);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const QUrl localUrl(QString("http://127.0.0.1:%1/image.png").arg(server.serverPort()));
        auto download = [&](qint64 limit) {
            QFutureWatcher<QPair<QByteArray, QString>> watcher;
            QEventLoop loop;
            QObject::connect(&watcher, &QFutureWatcher<QPair<QByteArray, QString>>::finished, &loop,
                             &QEventLoop::quit);
            watcher.setFuture(
                QtConcurrent::run([localUrl, limit] { return Native::downloadImage(localUrl, limit); }));
            loop.exec();
            return watcher.result();
        };
        const auto downloaded = listening ? download(1024 * 1024) : QPair<QByteArray, QString>();
        check("WinHTTP image download",
              listening && downloaded.second.isEmpty() && downloaded.first == response);
        const auto limited = listening ? download(64) : QPair<QByteArray, QString>();
        check("web image byte limit", listening && limited.first.isEmpty() && !limited.second.isEmpty());
    }
    QJsonObject report{{"qt", qVersion()}, {"native", native}, {"failed", failures}, {"checks", checks}};
    QFile reportFile(QDir(directory).filePath("results.json"));
    if (reportFile.open(QIODevice::WriteOnly))
        reportFile.write(QJsonDocument(report).toJson());
    QTextStream(stdout) << checks.size() - failures << "/" << checks.size() << " passed\n";
    QApplication::clipboard()->setMimeData(clipboardBackup);
    return failures == 0 ? 0 : 1;
}
