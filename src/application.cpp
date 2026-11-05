#include "application.h"
#include <QtWidgets>
#include <QtPrintSupport>
#include <QtConcurrent>
#include <QTextDocument>
#include <QtMath>

namespace {
QIcon applicationIcon() {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#172638"));
    painter.drawRoundedRect(QRectF(0, 0, 64, 64), 15, 15);
    painter.setPen(QPen(QColor("#54d6b0"), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(14, 26), QPointF(14, 14), QPointF(29, 14)}));
    painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(50, 38), QPointF(50, 50), QPointF(35, 50)}));
    painter.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(24, 40, 41, 23);
    return QIcon(pixmap);
}
} // namespace

Application::Application(Settings &settings, QObject *parent)
    : QObject(parent), settings_(settings), library_(settings, this), input_(this) {
    applyAppearance(settings_);
    QApplication::setWindowIcon(applicationIcon());
    window_ = new LibraryWindow(library_, settings_);
    connect(window_, &LibraryWindow::command, this, &Application::command);
    connect(window_, &LibraryWindow::recordAction, this, &Application::recordAction);
    connect(window_, &LibraryWindow::fileOpened, this, &Application::openImage);
    connect(&library_, &Library::changed, this, &Application::syncPins);
    connect(&library_, &Library::error, this, [this](const QString &message) { notify(message, true); });
    tray_ = new QSystemTrayIcon(this);
    tray_->setToolTip(QStringLiteral("EasySnip · F1 截图"));
    trayMenu_ = new QMenu;
    for (const auto &pair :
         QVector<QPair<QString, QString>>{{QStringLiteral("区域截图"), "capture"},
                                          {QStringLiteral("当前屏幕"), "screen"},
                                          {QStringLiteral("全屏截图"), "fullscreen"},
                                          {QStringLiteral("活动窗口"), "active"},
                                          {QStringLiteral("复用上次区域"), "last"},
                                          {QStringLiteral("预设 / 延时截图"), "preset"},
                                          {QStringLiteral("剪贴板贴图"), "clipboard"},
                                          {QStringLiteral("显示 / 隐藏贴图"), "toggle-pins"},
                                          {QStringLiteral("恢复最近关闭贴图"), "recover"},
                                          {QStringLiteral("历史与贴图"), "history"},
                                          {QStringLiteral("设置"), "settings"},
                                          {QStringLiteral("退出"), "quit"}})
        connect(trayMenu_->addAction(pair.first), &QAction::triggered, this,
                [this, pair] { command(pair.second); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        QString action;
        if (reason == QSystemTrayIcon::Trigger)
            action = settings_.value("tray/left", "capture").toString();
        else if (reason == QSystemTrayIcon::MiddleClick)
            action = settings_.value("tray/middle", "clipboard").toString();
        else if (reason == QSystemTrayIcon::Context)
            action = settings_.value("tray/right", "menu").toString();
        if (!action.isEmpty())
            command(action);
    });
    connect(&input_, &GlobalInput::activated, this, [this](int id) {
        if (Native::matchesProcess(
                Native::foregroundExecutable(),
                settings_.value("hotkeys/ignored").toString().split(';', Qt::SkipEmptyParts)))
            return;
        const QStringList actions{"capture", "clipboard", "toggle-pins",   "last",
                                  "active",  "history",   "custom-command"};
        if (id >= 1 && id <= actions.size())
            command(actions[id - 1]);
    });
    connect(&input_, &GlobalInput::superDragged, this, [this](const QRect &region) {
        if (capture_ || capturePending_)
            return;
        if (Native::matchesProcess(
                Native::foregroundExecutable(),
                settings_.value("hotkeys/ignored").toString().split(';', Qt::SkipEmptyParts)))
            return;
        const auto desktop = Native::captureDesktop();
        const QRect selected = region.intersected(desktop.geometry);
        if (selected.isEmpty() || desktop.image.isNull())
            return;
        Document document;
        document.image = desktop.image.copy(selected.translated(-desktop.geometry.topLeft()));
        edit(document, selected);
    });
    corners_.setInterval(100);
    connect(&corners_, &QTimer::timeout, this, &Application::checkCorners);
    corners_.start();
    themeTimer_.setInterval(10000);
    connect(&themeTimer_, &QTimer::timeout, this, [this] {
        if (settings_.value("appearance/followSystem", false).toBool())
            applyAppearance(settings_);
    });
    themeTimer_.start();
    configureInput();
    if (!settings_.value("pins/restore", true).toBool())
        pinsHidden_ = true;
    syncPins();
}
Application::~Application() {
    input_.clear();
    for (const auto &editor : editors_)
        if (editor)
            delete editor.data();
    for (const auto &pin : pins_)
        if (pin)
            delete pin.data();
    delete trayMenu_;
    delete window_;
}
void Application::start(const QStringList &arguments) {
    processArguments(arguments);
}
void Application::processArguments(const QStringList &arguments) {
    if (arguments.contains("--capture"))
        command("capture");
    else if (arguments.contains("--screen"))
        command("screen");
    else if (arguments.contains("--fullscreen"))
        command("fullscreen");
    else if (arguments.contains("--active"))
        command("active");
    else if (arguments.contains("--last"))
        command("last");
    else if (arguments.contains("--clipboard"))
        command("clipboard");
    else if (arguments.contains("--toggle-pins"))
        command("toggle-pins");
    else if (arguments.contains("--recover"))
        command("recover");
    else if (arguments.contains("--settings"))
        command("settings");
    else if (arguments.contains("--quit"))
        command("quit");
    else if (arguments.contains("--open")) {
        const int index = arguments.indexOf("--open");
        if (index + 1 < arguments.size())
            openImage(arguments[index + 1]);
    } else if (!arguments.contains("--tray") || !QSystemTrayIcon::isSystemTrayAvailable() ||
               !settings_.value("general/tray", true).toBool())
        command("history");
}
void Application::notify(const QString &message, bool error) {
    qInfo().noquote() << message;
    window_->statusBar()->showMessage(message, 8000);
    if (tray_->isVisible() && (error || settings_.value("general/notifications", true).toBool()))
        tray_->showMessage("EasySnip", message,
                           error ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 4000);
    else if (error)
        QMessageBox::warning(window_, QStringLiteral("EasySnip"), message);
}
void Application::configureInput() {
    input_.clear();
    const auto ignoredPrograms = settings_.value("hotkeys/ignored").toString().split(';', Qt::SkipEmptyParts);
    inputIgnored_ =
        !ignoredPrograms.isEmpty() && Native::matchesProcess(Native::foregroundExecutable(), ignoredPrograms);
    if (settings_.value("hotkeys/enabled", true).toBool() && !inputIgnored_) {
        const QStringList keys{"capture", "clipboard", "toggle", "last", "active", "history", "command"};
        const QStringList defaults{"F1", "F3", "Ctrl+Shift+H", "F4", "Ctrl+Shift+F1", "Ctrl+Shift+F3", ""};
        for (int i = 0; i < keys.size(); ++i) {
            const QKeySequence sequence(settings_.value("hotkeys/" + keys[i], defaults[i]).toString());
            if (sequence.isEmpty())
                continue;
            const int key = sequence[0] & ~Qt::KeyboardModifierMask;
            if (!(sequence[0] & Qt::KeyboardModifierMask) && key < Qt::Key_Escape &&
                !settings_.value("hotkeys/allowSimple", false).toBool()) {
                QTimer::singleShot(0, this, [this, sequence] {
                    notify(QStringLiteral("未启用字母数字单键快捷键 %1").arg(sequence.toString()), true);
                });
                continue;
            }
            QString error;
            if (!input_.registerShortcut(i + 1, sequence, &error))
                QTimer::singleShot(0, this, [this, error] { notify(error, true); });
        }
    }
    input_.enableSuperDrag(settings_.value("capture/superDrag", true).toBool() && !inputIgnored_);
    const QString icon = settings_.value("appearance/icon").toString();
    tray_->setIcon(QFileInfo::exists(icon) ? QIcon(icon) : applicationIcon());
    tray_->setVisible(settings_.value("general/tray", true).toBool() &&
                      QSystemTrayIcon::isSystemTrayAvailable());
    if (!tray_->isVisible())
        window_->show();
    Native::setPriority(settings_.value("runtime/priority", "normal").toString());
}
void Application::command(const QString &action) {
    if (action == "capture")
        capture();
    else if (action == "fullscreen" || action == "screen" || action == "active" || action == "last")
        capture(action);
    else if (action == "preset")
        capturePreset();
    else if (action == "history") {
        window_->show();
        window_->raise();
        window_->activateWindow();
    } else if (action == "settings") {
        if (showSettings(settings_, window_)) {
            configureInput();
            library_.prune();
            syncPins();
        }
    } else if (action == "menu")
        trayMenu_->popup(QCursor::pos());
    else if (action == "clipboard") {
        pinClipboard();
    } else if (action == "toggle-pins") {
        pinsHidden_ = !pinsHidden_;
        syncPins();
    } else if (action == "recover") {
        Record latest;
        for (const auto &item : library_.records())
            if (item.pinned && item.closed && (latest.id.isEmpty() || item.closedAt > latest.closedAt))
                latest = item;
        if (latest.id.isEmpty())
            notify(QStringLiteral("没有可恢复的贴图。"));
        else {
            library_.setActiveGroup(latest.group);
            pin(latest.id);
        }
    } else if (action == "clear-history") {
        if (QMessageBox::question(window_, QStringLiteral("清空截图历史"),
                                  QStringLiteral("清空全部截图历史？贴图及其恢复记录会继续保留。")) ==
            QMessageBox::Yes)
            library_.clearHistory();
    } else if (action == "backup") {
        const QString path =
            QFileDialog::getSaveFileName(window_, QStringLiteral("导出贴图和历史备份"),
                                         "EasySnip-backup.esbackup", "EasySnip backup (*.esbackup)");
        if (!path.isEmpty())
            notify(library_.exportBackup(path) ? QStringLiteral("备份已保存。")
                                               : QStringLiteral("备份保存失败。"),
                   !QFile::exists(path));
    } else if (action == "restore") {
        const QString path = QFileDialog::getOpenFileName(
            window_, QStringLiteral("导入备份（合并到当前记录）"), {}, "EasySnip backup (*.esbackup)");
        if (!path.isEmpty()) {
            const bool ok = library_.importBackup(path);
            notify(ok ? QStringLiteral("备份已合并恢复。")
                      : QStringLiteral("备份无效、超过内存限制或写入失败。"),
                   !ok);
        }
    } else if (action == "custom-command") {
        const QString line = settings_.value("hotkeys/commandLine").toString();
        if (!line.isEmpty() && !QProcess::startDetached(line))
            notify(QStringLiteral("自定义命令启动失败。"), true);
    } else if (action == "quit")
        quit();
}
void Application::capture(const QString &mode, const QRect &preset, int delay) {
    if (capture_ || capturePending_)
        return;
    const QRect activeWindow = Native::foregroundWindow();
    const QPoint mouse = QCursor::pos();
    capturePending_ = true;
    window_->hide();
    const int milliseconds = (delay < 0 ? settings_.value("capture/delay", 0).toInt() : delay) * 1000;
    if (milliseconds > 0)
        notify(QStringLiteral("%1 秒后开始截图").arg(milliseconds / 1000));
    QTimer::singleShot(qMax(150, milliseconds), this, [this, mode, preset, activeWindow, mouse] {
        capturePending_ = false;
        if (quitting_)
            return;
        auto desktop = Native::captureDesktop();
        if (desktop.image.isNull()) {
            notify(QStringLiteral("无法获取桌面图像。"), true);
            return;
        }
        QRect initial = preset;
        if (mode == "fullscreen")
            initial = desktop.geometry;
        if (mode == "active")
            initial = activeWindow;
        if (mode == "screen")
            for (const auto &screen : desktop.screens)
                if (screen.contains(mouse))
                    initial = screen;
        if (mode == "last")
            initial = settings_.value("capture/lastRect").toRect();
        capture_ = new CaptureOverlay(std::move(desktop), settings_, initial);
        connect(capture_, &CaptureOverlay::captured, this, [this](Document document, QRect rect) {
            settings_.set("capture/lastRect", rect);
            settings_.sync();
            edit(document, rect);
        });
        connect(capture_, &CaptureOverlay::canceled, this, [this](Document document) {
            if (settings_.value("history/includeCanceled", false).toBool() && !document.image.isNull()) {
                library_.add(document);
                library_.prune();
            }
        });
        capture_->show();
        capture_->raise();
        capture_->activateWindow();
        capture_->setFocus();
    });
}
void Application::capturePreset() {
    QDialog dialog(window_);
    dialog.setWindowTitle(QStringLiteral("截图前预设"));
    QFormLayout form(&dialog);
    QSpinBox x, y, width, height, delay;
    x.setRange(-32768, 32767);
    y.setRange(-32768, 32767);
    width.setRange(1, 32768);
    height.setRange(1, 32768);
    delay.setRange(0, 120);
    const QRect last = settings_.value("capture/lastRect", QRect(0, 0, 800, 600)).toRect();
    x.setValue(last.x());
    y.setValue(last.y());
    width.setValue(last.width());
    height.setValue(last.height());
    delay.setValue(settings_.value("capture/delay", 0).toInt());
    QCheckBox fixed(QStringLiteral("使用指定选区"));
    fixed.setChecked(true);
    form.addRow(&fixed);
    form.addRow("X", &x);
    form.addRow("Y", &y);
    form.addRow(QStringLiteral("宽度"), &width);
    form.addRow(QStringLiteral("高度"), &height);
    form.addRow(QStringLiteral("延迟 / 秒"), &delay);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        capture("region",
                fixed.isChecked() ? QRect(x.value(), y.value(), width.value(), height.value()) : QRect(),
                delay.value());
}
void Application::edit(Document document, const QRect &sourceRect, const QString &recordId) {
    if (document.image.isNull())
        return;
    auto *editor = new EditorWindow(document, settings_, sourceRect);
    editor->setProperty("recordId", recordId);
    editors_.append(editor);
    connect(editor, &EditorWindow::outputRequested, this,
            [this, editor](const Document &document, const QString &action, bool skip) {
                QString id = editor->property("recordId").toString();
                const bool complete = output(document, action, editor, id, skip);
                editor->setProperty("recordId", id);
                if (complete) {
                    editor->acknowledgeOutput();
                    if (action == "finish")
                        editor->close();
                }
            });
    connect(editor, &EditorWindow::abandoned, this, [this, editor](const Document &document) {
        if (settings_.value("history/includeCanceled", false).toBool() &&
            editor->property("recordId").toString().isEmpty()) {
            library_.add(document);
            library_.prune();
        }
    });
    editor->show();
    editor->raise();
    editor->activateWindow();
}
void Application::openImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (qint64(reader.size().width()) * reader.size().height() > 150000000) {
        notify(QStringLiteral("图像超过 1.5 亿像素限制。"), true);
        return;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        notify(QStringLiteral("无法打开图片 %1\n%2").arg(path, reader.errorString()), true);
        return;
    }
    Document document;
    document.image = image;
    edit(document);
}
void Application::pinImage(const QImage &image) {
    if (image.isNull())
        return;
    Document document;
    document.image = image;
    const QString id = library_.add(document, false);
    if (!id.isEmpty() && !pin(id))
        library_.remove(id);
}
void Application::pinClipboard() {
    const auto *mime = QApplication::clipboard()->mimeData();
    if (!mime) {
        notify(QStringLiteral("剪贴板为空。"), true);
        return;
    }
    QImage image = QApplication::clipboard()->image();
    if (!image.isNull()) {
        pinImage(image);
        return;
    }
    const QList<QUrl> urls = mime->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        image.load(urls.first().toLocalFile());
        if (!image.isNull())
            pinImage(image);
        else
            notify(QStringLiteral("剪贴板文件无法作为图像读取。"), true);
        return;
    }
    const QString text = mime->text().trimmed();
    const QUrl url = urls.isEmpty() ? QUrl(text) : urls.first();
    if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty()) {
        const qint64 limit =
            qBound(1024LL, settings_.value("output/webImageMaxBytes", 8 * 1024 * 1024).toLongLong(),
                   128LL * 1024 * 1024);
        auto *watcher = new QFutureWatcher<QPair<QByteArray, QString>>(this);
        connect(watcher, &QFutureWatcher<QPair<QByteArray, QString>>::finished, this, [this, watcher] {
            auto result = watcher->result();
            watcher->deleteLater();
            if (!result.second.isEmpty()) {
                notify(result.second, true);
                return;
            }
            QBuffer buffer(&result.first);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            reader.setAutoTransform(true);
            const QSize size = reader.size();
            if (!size.isValid() || qint64(size.width()) * size.height() > 150000000)
                notify(QStringLiteral("链接内容无法作为允许尺寸的图像读取。"), true);
            else {
                const QImage image = reader.read();
                if (image.isNull())
                    notify(QStringLiteral("图片解码失败。"), true);
                else
                    pinImage(image);
            }
        });
        watcher->setFuture(QtConcurrent::run([url, limit] { return Native::downloadImage(url, limit); }));
        notify(QStringLiteral("正在下载剪贴板中的图片链接…"));
        return;
    }
    if (mime->hasHtml() || !text.isEmpty()) {
        class ClipboardDocument : public QTextDocument {
          protected:
            QVariant loadResource(int, const QUrl &) override {
                return {};
            }
        } content;
        const QString html = mime->html();
        if (html.size() + text.size() > 256 * 1024) {
            notify(QStringLiteral("剪贴板文本超过 256 KiB 限制。"), true);
            return;
        }
        content.setDefaultFont(QApplication::font());
        content.setDefaultStyleSheet("body { color:#172131; background:white; }");
        if (mime->hasHtml())
            content.setHtml(html);
        else
            content.setPlainText(text);
        QTextOption option;
        const bool wrap = settings_.value("pins/htmlForceWrap", true).toBool();
        option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        content.setDefaultTextOption(option);
        content.setTextWidth(wrap ? 600 : -1);
        const QSize size(qBound(120, qCeil(content.size().width()) + 32, 4096),
                         qBound(64, qCeil(content.size().height()) + 32, 8192));
        QImage image(size, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.translate(16, 16);
        content.drawContents(&painter);
        painter.end();
        pinImage(image);
        return;
    }
    notify(QStringLiteral("剪贴板中没有可用的图像或文本。"), true);
}
bool Application::pin(const QString &id) {
    auto record = library_.record(id);
    if (record.id.isEmpty())
        return false;
    if (library_.document(id).image.isNull()) {
        notify(QStringLiteral("贴图源文件无法读取。"), true);
        return false;
    }
    record.pinned = true;
    record.closed = false;
    record.group = library_.activeGroup();
    pinsHidden_ = false;
    if (!library_.updateRecord(record))
        return false;
    syncPins();
    return true;
}
void Application::syncPins() {
    if (quitting_)
        return;
    QSet<QString> valid;
    for (const auto &record : library_.records()) {
        if (!record.pinned || record.closed)
            continue;
        valid.insert(record.id);
        const bool show = !pinsHidden_ && record.group == library_.activeGroup();
        if (!pins_.value(record.id) && show) {
            const auto document = library_.document(record.id);
            if (document.image.isNull())
                continue;
            auto *pin = new PinWindow(record, document, settings_, library_.groups());
            pins_.insert(record.id, pin);
            connect(pin, &PinWindow::actionRequested, this, &Application::recordAction);
            connect(pin, &PinWindow::geometryChanged, this,
                    [this](const QString &id, const QRect &geometry, double opacity) {
                        auto record = library_.record(id);
                        if (record.id.isEmpty())
                            return;
                        record.geometry = geometry;
                        record.opacity = opacity;
                        library_.updateRecord(record);
                    });
            connect(pin, &PinWindow::groupRequested, this, [this](const QString &id, const QString &group) {
                auto record = library_.record(id);
                record.group = group;
                library_.updateRecord(record);
            });
        }
        if (pins_.value(record.id)) {
            pins_[record.id]->setGroups(library_.groups());
            pins_[record.id]->setVisible(show);
        }
    }
    for (const auto &id : pins_.keys())
        if (!valid.contains(id)) {
            if (pins_[id]) {
                pins_[id]->hide();
                pins_[id]->deleteLater();
            }
            pins_.remove(id);
        }
}
void Application::recordAction(const QString &id, const QString &action) {
    auto item = library_.record(id);
    if (item.id.isEmpty())
        return;
    if (action == "edit")
        edit(library_.document(id), {}, id);
    else if (action == "pin")
        pin(id);
    else if (action == "close") {
        if (!item.pinned)
            return;
        item.closed = true;
        item.closedAt = QDateTime::currentDateTime();
        library_.updateRecord(item);
        library_.prune();
    } else if (action == "destroy") {
        if (QMessageBox::question(window_, QStringLiteral("销毁记录"),
                                  QStringLiteral("永久删除该图像及其恢复记录？")) == QMessageBox::Yes)
            library_.remove(id);
    } else if (action == "rename") {
        bool ok = false;
        const QString title = QInputDialog::getText(
            window_, QStringLiteral("图像标题"), QStringLiteral("标题"), QLineEdit::Normal, item.title, &ok);
        if (ok && !title.trimmed().isEmpty()) {
            item.title = title.trimmed();
            library_.updateRecord(item);
        }
    } else if (action == "decode") {
        const auto document = library_.document(id);
        const QString result = decodeCodes(document.render());
        QInputDialog dialog(window_);
        dialog.setWindowTitle(QStringLiteral("识别结果"));
        dialog.setLabelText(QStringLiteral("可选中并复制识别内容"));
        dialog.setOption(QInputDialog::UsePlainTextEditForTextInput);
        dialog.setTextValue(result.isEmpty() ? QStringLiteral("未检测到可读取的二维码或条形码。") : result);
        dialog.resize(600, 400);
        dialog.exec();
    } else {
        QString recordId = id;
        output(library_.document(id), action, window_, recordId, true);
    }
}
bool Application::saveImage(const QImage &source, const QString &path) {
    QImage image = source;
    const QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
    if (format == "jpg" || format == "jpeg" || format == "bmp") {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter painter(&flat);
        painter.drawImage(0, 0, image);
        painter.end();
        image = flat;
    } else if (settings_.value("output/removeOpaqueAlpha", true).toBool() && image.hasAlphaChannel()) {
        bool opaque = true;
        for (int y = 0; y < image.height() && opaque; ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) != 255) {
                    opaque = false;
                    break;
                }
        if (opaque)
            image = image.convertToFormat(QImage::Format_RGB32);
    }
    if (settings_.value("output/metadata", true).toBool())
        image.setText("Software", "EasySnip 0.1.0 / Qt 5.14.2");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        notify(file.errorString(), true);
        return false;
    }
    QImageWriter writer(&file, format.isEmpty() ? "png" : format);
    writer.setQuality(95);
    if (!writer.write(image) || !file.commit()) {
        notify(QStringLiteral("保存失败 %1\n%2").arg(path, writer.errorString()), true);
        return false;
    }
    return true;
}
QString Application::quickSave(const QImage &image, bool cache) {
    const QString directory = cache ? settings_.dataDirectory() + "/clipboard" : settings_.outputDirectory();
    if (!QDir().mkpath(directory)) {
        notify(QStringLiteral("无法创建保存目录 %1").arg(directory), true);
        return {};
    }
    const QString extension = cache ? "png" : settings_.value("output/format", "png").toString();
    const QString name = "EasySnip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" +
                         QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + '.' + extension;
    const QString path = QDir(directory).filePath(name);
    return saveImage(image, path) ? path : QString();
}
bool Application::output(const Document &document, const QString &action, QWidget *owner, QString &recordId,
                         bool skipAutoSave) {
    if (document.image.isNull())
        return false;
    if (!recordId.isEmpty() && library_.record(recordId).id.isEmpty())
        recordId.clear();
    const QImage image = document.render(settings_.value("output/frame", false).toBool());
    QStringList targets;
    if (action == "finish") {
        if (settings_.value("output/copy", true).toBool())
            targets.append("copy");
        if (settings_.value("output/save", false).toBool())
            targets.append("save");
        if (settings_.value("output/pin", false).toBool())
            targets.append("pin");
        if (targets.isEmpty())
            targets.append("copy");
    } else
        targets.append(action);
    if (!skipAutoSave && settings_.value("output/autoSave", false).toBool() && !targets.contains("save") &&
        !targets.contains("save-as"))
        targets.prepend("save");
    bool successful = true;
    for (const auto &target : targets) {
        if (target == "copy") {
            QString file;
            if (settings_.value("output/copyFile", true).toBool()) {
                file = quickSave(image, true);
                if (file.isEmpty()) {
                    successful = false;
                    continue;
                }
            }
            auto *mime = new QMimeData;
            mime->setImageData(image);
            if (!file.isEmpty())
                mime->setUrls({QUrl::fromLocalFile(file)});
            QApplication::clipboard()->setMimeData(mime);
        } else if (target == "save") {
            const QString path = quickSave(image);
            if (path.isEmpty())
                successful = false;
            else
                notify(QStringLiteral("已保存 %1").arg(path));
        } else if (target == "save-as") {
            const bool visible = owner && owner->isVisible();
            if (visible)
                owner->hide();
            QString filter;
            QString path = QFileDialog::getSaveFileName(
                nullptr, QStringLiteral("保存图像"),
                settings_.outputDirectory() + "/EasySnip_" +
                    QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
                "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp)", &filter);
            if (visible) {
                owner->show();
                owner->raise();
            }
            if (path.isEmpty()) {
                successful = false;
                continue;
            }
            if (QFileInfo(path).suffix().isEmpty())
                path += filter.startsWith("JPEG") ? ".jpg" : filter.startsWith("BMP") ? ".bmp" : ".png";
            if (!saveImage(image, path))
                successful = false;
        } else if (target == "pin") {
            if (recordId.isEmpty())
                recordId = library_.add(document);
            else if (!library_.updateDocument(recordId, document)) {
                successful = false;
                continue;
            }
            if (recordId.isEmpty()) {
                successful = false;
                continue;
            }
            if (!pin(recordId)) {
                successful = false;
                continue;
            }
            if (pins_.value(recordId))
                pins_[recordId]->setDocument(document);
        } else if (target == "print") {
            QPrinter printer(QPrinter::HighResolution);
            QPrintDialog dialog(&printer, owner);
            if (dialog.exec() != QDialog::Accepted) {
                successful = false;
                continue;
            }
            QPainter painter;
            if (!painter.begin(&printer)) {
                notify(QStringLiteral("打印机初始化失败。"), true);
                successful = false;
                continue;
            }
            QRect viewport = painter.viewport();
            const QSize size = image.size().scaled(viewport.size(), Qt::KeepAspectRatio);
            painter.drawImage(QRect(viewport.center() - QPoint(size.width() / 2, size.height() / 2), size),
                              image);
            painter.end();
        } else if (target == "share") {
            const QString file = quickSave(image, true);
            QString error;
            if (file.isEmpty() || !Native::shareImage(owner, file, &error)) {
                if (!error.isEmpty())
                    notify(error, true);
                successful = false;
            }
        } else if (target == "external") {
            const QString program = settings_.value("output/program").toString();
            if (program.isEmpty()) {
                notify(QStringLiteral("请先在设置中填写外部程序路径。"), true);
                successful = false;
                continue;
            }
            const QString file = quickSave(image, true);
            if (file.isEmpty()) {
                successful = false;
                continue;
            }
            QStringList arguments = settings_.value("output/arguments", "{file}").toString().split("\\n");
            for (auto &argument : arguments)
                argument.replace("{file}", file);
            if (!QProcess::startDetached(program, arguments)) {
                notify(QStringLiteral("外部程序启动失败。"), true);
                successful = false;
            }
        }
    }
    if (!successful)
        return false;
    if (recordId.isEmpty())
        recordId = library_.add(document);
    else if (!library_.updateDocument(recordId, document))
        return false;
    if (recordId.isEmpty())
        return false;
    if (pins_.value(recordId))
        pins_[recordId]->setDocument(document);
    library_.prune();
    if (action != "save")
        notify(QStringLiteral("图像已输出，%1 × %2 像素").arg(image.width()).arg(image.height()));
    return true;
}
void Application::checkCorners() {
    Native::foregroundWindow();
    const auto ignoredPrograms = settings_.value("hotkeys/ignored").toString().split(';', Qt::SkipEmptyParts);
    const bool ignored =
        !ignoredPrograms.isEmpty() && Native::matchesProcess(Native::foregroundExecutable(), ignoredPrograms);
    if (ignored != inputIgnored_)
        configureInput();
    if (!settings_.value("corners/enabled", false).toBool() || capture_ || capturePending_ ||
        QApplication::activeModalWidget())
        return;
    const QPoint cursor = QCursor::pos();
    QString corner;
    for (const auto *screen : QGuiApplication::screens()) {
        const QRect rect = screen->geometry();
        if ((cursor - rect.topLeft()).manhattanLength() <= 2)
            corner = "topLeft";
        if ((cursor - rect.topRight()).manhattanLength() <= 2)
            corner = "topRight";
        if ((cursor - rect.bottomLeft()).manhattanLength() <= 2)
            corner = "bottomLeft";
        if ((cursor - rect.bottomRight()).manhattanLength() <= 2)
            corner = "bottomRight";
    }
    if (corner != corner_) {
        corner_ = corner;
        cornerTime_.restart();
        cornerTriggered_ = false;
    }
    if (!corner.isEmpty() && !cornerTriggered_ && cornerTime_.elapsed() >= 600) {
        cornerTriggered_ = true;
        command(settings_.value("corners/" + corner, "none").toString());
    }
}
void Application::quit() {
    if (quitting_)
        return;
    quitting_ = true;
    input_.clear();
    input_.enableSuperDrag(false);
    tray_->hide();
    for (const auto &pin : pins_)
        if (pin) {
            auto item = library_.record(pin->recordId());
            item.geometry = pin->geometry();
            item.opacity = pin->windowOpacity();
            library_.updateRecord(item);
        }
    settings_.sync();
    QApplication::quit();
}
