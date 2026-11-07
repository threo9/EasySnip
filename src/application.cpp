#include "application.h"
#include <QtWidgets>
#include <QtPrintSupport>
#include <QtConcurrent>

namespace {
QIcon applicationIcon() {
    const QIcon cached = qvariant_cast<QIcon>(qApp->property("EasySnipApplicationIcon"));
    if (!cached.isNull())
        return cached;
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
    painter.end();
    const QIcon icon(pixmap);
    qApp->setProperty("EasySnipApplicationIcon", icon);
    return icon;
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
    connect(&library_, &Library::error, this, [this](const QString &message) { notify(message, true); });
    tray_ = new QSystemTrayIcon(this);
    tray_->setToolTip(QStringLiteral("EasySnip · %1 截图").arg(settings_.shortcut("capture")));
    trayMenu_ = new QMenu;
    for (const auto &pair :
         QVector<QPair<QString, QString>>{{QStringLiteral("区域截图"), "capture"},
                                          {QStringLiteral("当前屏幕"), "screen"},
                                          {QStringLiteral("全屏截图"), "fullscreen"},
                                          {QStringLiteral("活动窗口"), "active"},
                                          {QStringLiteral("复用上次区域"), "last"},
                                          {QStringLiteral("预设 / 延时截图"), "preset"},
                                          {QStringLiteral("截图历史"), "history"},
                                          {QStringLiteral("设置"), "settings"},
                                          {QStringLiteral("退出"), "quit"}})
        connect(trayMenu_->addAction(pair.first), &QAction::triggered, this,
                [this, pair] { command(pair.second); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        QString action;
        if (reason == QSystemTrayIcon::Trigger)
            action = settings_.action("tray/left", "capture");
        else if (reason == QSystemTrayIcon::MiddleClick)
            action = settings_.action("tray/middle", "none");
        else if (reason == QSystemTrayIcon::Context)
            action = settings_.action("tray/right", "menu");
        if (!action.isEmpty())
            command(action);
    });
    connect(&input_, &GlobalInput::activated, this, [this](int id) {
        const QStringList keys{"capture", "last", "active", "history", "command"};
        if (id < 1 || id > keys.size() ||
            settings_.ignoresShortcut(keys[id - 1], Native::foregroundExecutable()))
            return;
        const QStringList actions{"capture", "last", "active", "history", "custom-command"};
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
        capture("region", region, 0);
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
    hotkeyRetry_.setInterval(2000);
    connect(&hotkeyRetry_, &QTimer::timeout, this, [this] {
        for (int id : pendingHotkeys_.keys()) {
            QString error;
            if (input_.registerShortcut(id, pendingHotkeys_.value(id), &error))
                pendingHotkeys_.remove(id);
        }
        if (pendingHotkeys_.isEmpty())
            hotkeyRetry_.stop();
    });
    configureInput();
}
Application::~Application() {
    input_.clear();
    delete capture_.data();
    for (const auto &editor : editors_)
        if (editor)
            delete editor.data();
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
    else if (arguments.contains("--settings"))
        command("settings");
    else if (arguments.contains("--quit"))
        command("quit");
    else if (arguments.contains("--open")) {
        const int index = arguments.indexOf("--open");
        if (index + 1 < arguments.size())
            openImage(arguments[index + 1]);
    } else if (!QSystemTrayIcon::isSystemTrayAvailable() ||
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
    hotkeyRetry_.stop();
    pendingHotkeys_.clear();
    input_.clear();
    const auto ignoredPrograms = settings_.value("hotkeys/ignored").toString().split(';', Qt::SkipEmptyParts);
    inputIgnored_ =
        !ignoredPrograms.isEmpty() && Native::matchesProcess(Native::foregroundExecutable(), ignoredPrograms);
    if (settings_.value("hotkeys/enabled", true).toBool()) {
        const QStringList keys{"capture", "last", "active", "history", "command"};
        for (int i = 0; i < keys.size(); ++i) {
            const QKeySequence sequence(settings_.shortcut(keys[i]));
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
            bool alreadyInUse = false;
            if (!input_.registerShortcut(i + 1, sequence, &error, &alreadyInUse)) {
                if (alreadyInUse)
                    pendingHotkeys_.insert(i + 1, sequence);
                QTimer::singleShot(0, this, [this, error] { notify(error, true); });
            }
        }
    }
    if (!pendingHotkeys_.isEmpty())
        hotkeyRetry_.start();
    input_.enableSuperDrag(settings_.value("capture/superDrag", true).toBool() && !inputIgnored_,
                          settings_.value("capture/superModifiers", "Alt+Shift").toString());
    tray_->setToolTip(QStringLiteral("EasySnip · %1 截图").arg(settings_.shortcut("capture")));
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
        }
    } else if (action == "menu")
        trayMenu_->popup(QCursor::pos());
    else if (action == "clear-history") {
        if (QMessageBox::question(window_, QStringLiteral("清空截图历史"),
                                  QStringLiteral("清空全部截图历史及其图像文件？")) ==
            QMessageBox::Yes)
            library_.clearHistory();
    } else if (action == "backup") {
        const QString path =
            QFileDialog::getSaveFileName(window_, QStringLiteral("导出截图历史备份"),
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
    QElapsedTimer elapsed;
    elapsed.start();
    const bool hideInterface = window_->isVisible() || QApplication::activePopupWidget();
    window_->hide();
    const int milliseconds = (delay < 0 ? settings_.value("capture/delay", 0).toInt() : delay) * 1000;
    if (milliseconds > 0)
        notify(QStringLiteral("%1 秒后开始截图").arg(milliseconds / 1000));
    QTimer::singleShot(qMax(hideInterface ? 80 : 0, milliseconds), this,
                      [this, mode, preset, activeWindow, mouse, elapsed] {
        if (quitting_) {
            capturePending_ = false;
            return;
        }
        auto *watcher = new QFutureWatcher<Native::Desktop>(this);
        connect(watcher, &QFutureWatcher<Native::Desktop>::finished, this,
                [this, watcher, mode, preset, activeWindow, mouse, elapsed] {
        capturePending_ = false;
        auto desktop = watcher->result();
        watcher->deleteLater();
        if (quitting_)
            return;
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
        showCapture(std::move(desktop), initial);
        qInfo() << "Capture overlay ready in" << elapsed.elapsed() << "ms";
        });
        watcher->setFuture(QtConcurrent::run([] { return Native::captureDesktop(true); }));
    });
}
void Application::showCapture(Native::Desktop desktop, const QRect &initial) {
    capture_ = new CaptureOverlay(std::move(desktop), settings_, initial);
    auto *overlay = capture_.data();
    QStringList history;
    for (const auto &record : library_.records())
        history.append(record.id);
    overlay->setHistory(history.size(), [this, history](int index) {
        return library_.document(history.value(index));
    });
    connect(overlay, &CaptureOverlay::outputRequested, this,
            [this, overlay](const Document &document, const QRect &rect, const QString &action, bool skip) {
                QString id = overlay->property("recordId").toString();
                const bool successful = output(document, action, overlay, id, skip);
                overlay->setProperty("recordId", id);
                if (successful && !rect.isEmpty()) {
                    settings_.set("capture/lastRect", rect);
                    settings_.sync();
                }
                // The Windows share session keeps its data-request handler on the owner window.
                const bool closeCapture = action != "share" && action != "print" && action != "external";
                overlay->completeOutput(successful, closeCapture);
            });
    connect(overlay, &CaptureOverlay::canceled, this, [this, overlay](const Document &document) {
        if (settings_.value("history/includeCanceled", false).toBool() && !document.image.isNull() &&
            overlay->property("recordId").toString().isEmpty()) {
            library_.add(document);
            library_.prune();
        }
    });
    connect(overlay, &CaptureOverlay::draftAbandoned, this, [this](const Document &document) {
        if (settings_.value("history/includeCanceled", false).toBool() && !document.image.isNull()) {
            library_.add(document);
            library_.prune();
        }
    });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
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
void Application::recordAction(const QString &id, const QString &action) {
    auto item = library_.record(id);
    if (item.id.isEmpty())
        return;
    if (action == "edit")
        edit(library_.document(id), {}, id);
    else if (action == "destroy") {
        if (QMessageBox::question(window_, QStringLiteral("销毁记录"),
                                  QStringLiteral("永久删除该截图及其图像文件？")) == QMessageBox::Yes)
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
    } else if (settings_.value("output/removeOpaqueAlpha", true).toBool() && image.hasAlphaChannel() &&
               hasOnlyOpaquePixels(image))
        image = image.convertToFormat(QImage::Format_RGB32);
    const bool metadata = settings_.value("output/metadata", true).toBool();
    const QString software = "EasySnip 0.1.0 / Qt 5.14.2";
    const bool writerMetadata = metadata && !image.textKeys().contains("Software");
    // Preserve replacement semantics for existing metadata; avoid detaching unchanged pixels otherwise.
    if (metadata && !writerMetadata && image.text("Software") != software)
        image.setText("Software", software);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        notify(file.errorString(), true);
        return false;
    }
    QImageWriter writer(&file, format.isEmpty() ? "png" : format);
    writer.setQuality(95);
    if (writerMetadata) {
        if (writer.supportsOption(QImageIOHandler::Description))
            writer.setText("Software", software);
        else
            image.setText("Software", software);
    }
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
    const QStringList actions{"finish", "copy", "save", "save-as", "print", "share", "external"};
    if (document.image.isNull() || !actions.contains(action))
        return false;
    if (!recordId.isEmpty() && library_.record(recordId).id.isEmpty())
        recordId.clear();
    const bool frame = settings_.value("output/frame", false).toBool();
    const QImage image = document.render(frame);
    QStringList targets;
    if (action == "finish") {
        if (settings_.value("output/copy", true).toBool())
            targets.append("copy");
        if (settings_.value("output/save", false).toBool())
            targets.append("save");
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
        recordId = library_.add(document, frame ? QImage() : image);
    else if (!library_.updateDocument(recordId, document, frame ? QImage() : image))
        return false;
    if (recordId.isEmpty())
        return false;
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
    if (ignored != inputIgnored_) {
        inputIgnored_ = ignored;
        input_.enableSuperDrag(settings_.value("capture/superDrag", true).toBool() && !ignored,
                              settings_.value("capture/superModifiers", "Alt+Shift").toString());
    }
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
        command(settings_.action("corners/" + corner, "none"));
    }
}
void Application::quit() {
    if (quitting_)
        return;
    quitting_ = true;
    hotkeyRetry_.stop();
    input_.clear();
    input_.enableSuperDrag(false);
    tray_->hide();
    settings_.sync();
    QApplication::quit();
}
