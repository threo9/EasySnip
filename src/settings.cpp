#include "settings.h"
#include "native.h"
#include <QtWidgets>
#include <functional>

Settings::Settings(const QString &directory)
    : directory_(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                     : directory),
      settings_(directory_ + "/settings.ini", QSettings::IniFormat) {
    QDir().mkpath(directory_);
    settings_.setIniCodec("UTF-8");
}
QVariant Settings::value(const QString &key, const QVariant &fallback) const {
    return settings_.value(key, fallback);
}
void Settings::set(const QString &key, const QVariant &value) {
    settings_.setValue(key, value);
}
QString Settings::dataDirectory() const {
    return directory_;
}
QString Settings::historyDirectory() const {
    return value("storage/historyDirectory", directory_ + "/library").toString();
}
QString Settings::outputDirectory() const {
    return value("output/directory",
                 QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/EasySnip")
        .toString();
}
QString Settings::fileName() const {
    return settings_.fileName();
}
void Settings::sync() {
    settings_.sync();
}
QStringList Settings::palette() const {
    return value("annotation/palette",
                 QStringList{"#ff536b", "#ffbd59", "#54d6b0", "#58a6ff", "#b595ff", "#ffffff", "#161c27"})
        .toStringList();
}
QColor Settings::toolColor(const QString &tool) const {
    return QColor(value("colors/" + tool, "#ff536b").toString());
}
void Settings::setToolColor(const QString &tool, const QColor &color) {
    set("colors/" + tool, color.name(QColor::HexArgb));
}

int uiPixels(int pixels) {
    const double scale = qApp->property("EasySnipUiScale").toDouble();
    return qRound(pixels * (scale > 0 ? scale : 1.0));
}

void applyAppearance(const Settings &settings) {
    const double systemScale = QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0;
    const double scale = qBound(1.0, settings.value("appearance/uiScale", systemScale).toDouble(), 3.0);
    qApp->setProperty("EasySnipUiScale", scale);
    qApp->setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#10151e"));
    palette.setColor(QPalette::WindowText, QColor("#e8edf5"));
    palette.setColor(QPalette::Base, QColor("#1a2332"));
    palette.setColor(QPalette::AlternateBase, QColor("#232d3e"));
    palette.setColor(QPalette::Text, QColor("#e8edf5"));
    palette.setColor(QPalette::Button, QColor("#232d3e"));
    palette.setColor(QPalette::ButtonText, QColor("#e8edf5"));
    palette.setColor(QPalette::Highlight, QColor("#366f70"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(palette);
    qApp->setDoubleClickInterval(qBound(100, settings.value("input/doubleClickMs", 400).toInt(), 1000));
    QColor accent(settings.value("appearance/accent", "#54d6b0").toString());
    if (settings.value("appearance/followSystem", false).toBool()) {
        QSettings windows("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM", QSettings::NativeFormat);
        const quint32 color = windows.value("ColorizationColor", 0xff54d6b0).toUInt();
        accent = QColor::fromRgb(color & 0x00ffffff);
    }
    qApp->setFont(QFont(settings.value("appearance/font", "Microsoft YaHei UI").toString(), 10));
    const QString style =
        QStringLiteral(
            "QWidget { color:#e8edf5; font-size:13px; }"
            "QMainWindow,QDialog { background:#10151e; }"
            "QToolTip { color:#e8edf5; background:#253043; border:1px solid #41506a; padding:6px; }"
            "QLabel#heading { font-size:30px; font-weight:600; }"
            "QLabel#muted { color:#92a0b5; }"
            "QLabel#brand { font-size:23px; font-weight:600; color:%1; }"
            "QFrame#sidebar { background:#171e2a; border-right:1px solid #283344; }"
            "QPushButton,QToolButton { background:#232d3e; border:1px solid #354158; border-radius:6px; "
            "padding:7px 12px; }"
            "QPushButton:hover,QToolButton:hover { background:#2f3d54; border-color:%1; }"
            "QPushButton:checked,QToolButton:checked,QPushButton#primary { background:%1; color:#10151e; "
            "border-color:%1; font-weight:600; }"
            "QPushButton:disabled,QToolButton:disabled { color:#657188; background:#1d2532; }"
            "QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QKeySequenceEdit,QPlainTextEdit { "
            "background:#1a2332; border:1px solid #364359; border-radius:5px; padding:5px; "
            "selection-background-color:#366f70; }"
            "QComboBox QAbstractItemView { background:#1a2332; selection-background-color:#366f70; }"
            "QListWidget,QTreeWidget,QScrollArea { background:#10151e; border:0; }"
            "QListWidget::item { border-radius:7px; padding:8px; }"
            "QListWidget::item:selected { background:#263f4a; border:1px solid %1; }"
            "QMenu { background:#1b2433; border:1px solid #354158; padding:6px; }"
            "QMenu::item { padding:7px 24px; border-radius:4px; }"
            "QMenu::item:selected { background:#35455c; }"
            "QMenu::separator { height:1px; background:#354158; margin:5px; }"
            "QTabWidget::pane { border:1px solid #2c374a; }"
            "QTabBar::tab { background:#1c2635; padding:10px 16px; }"
            "QTabBar::tab:selected { background:#314458; }"
            "QCheckBox { spacing:9px; padding:4px; }"
            "QCheckBox::indicator { width:16px; height:16px; }"
            "QStatusBar { background:#171e2a; color:#9aaac0; }"
            "QSplitter::handle { background:#283344; }"
            "QScrollBar:vertical { background:#141c28; width:10px; }"
            "QScrollBar::handle:vertical { background:#3c4c65; min-height:24px; border-radius:5px; }")
            .arg(accent.name());
    QString scaledStyle;
    int offset = 0;
    auto matches = QRegularExpression("(\\d+)px").globalMatch(style);
    while (matches.hasNext()) {
        const auto match = matches.next();
        scaledStyle += style.mid(offset, match.capturedStart() - offset);
        scaledStyle += QString::number(uiPixels(match.captured(1).toInt())) + "px";
        offset = match.capturedEnd();
    }
    scaledStyle += style.mid(offset);
    qApp->setStyleSheet(scaledStyle);
}

bool showSettings(Settings &settings, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("EasySnip · 设置"));
    dialog.resize(QSize(uiPixels(760), uiPixels(720))
                      .boundedTo(QGuiApplication::primaryScreen()->availableGeometry().size() * 0.92));
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    QVector<std::function<void()>> save;
    auto page = [&](const QString &title) {
        auto *widget = new QWidget;
        auto *form = new QFormLayout(widget);
        form->setContentsMargins(22, 20, 22, 20);
        form->setSpacing(12);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(widget);
        tabs->addTab(scroll, title);
        return form;
    };
    auto check = [&](QFormLayout *form, const QString &text, const QString &key, bool fallback) {
        auto *box = new QCheckBox(text);
        box->setChecked(settings.value(key, fallback).toBool());
        form->addRow(box);
        save.append([&, box, key] { settings.set(key, box->isChecked()); });
    };
    auto number = [&](QFormLayout *form, const QString &label, const QString &key, int fallback, int minimum,
                      int maximum) {
        auto *spin = new QSpinBox;
        spin->setRange(minimum, maximum);
        spin->setValue(settings.value(key, fallback).toInt());
        form->addRow(label, spin);
        save.append([&, spin, key] { settings.set(key, spin->value()); });
    };
    auto text = [&](QFormLayout *form, const QString &label, const QString &key, const QString &fallback) {
        auto *edit = new QLineEdit(settings.value(key, fallback).toString());
        form->addRow(label, edit);
        save.append([&, edit, key] { settings.set(key, edit->text().trimmed()); });
        return edit;
    };
    auto choices = [&](QFormLayout *form, const QString &label, const QString &key, const QStringList &values,
                       const QStringList &labels, const QString &fallback) {
        auto *combo = new QComboBox;
        for (int i = 0; i < values.size(); ++i)
            combo->addItem(labels.value(i, values[i]), values[i]);
        combo->setCurrentIndex(qMax(0, values.indexOf(settings.value(key, fallback).toString())));
        form->addRow(label, combo);
        save.append([&, combo, key] { settings.set(key, combo->currentData()); });
    };

    auto *general = page(QStringLiteral("常规"));
    check(general, QStringLiteral("开机启动"), "general/autostart", false);
    check(general, QStringLiteral("启动时恢复贴图"), "pins/restore", true);
    check(general, QStringLiteral("显示托盘图标"), "general/tray", true);
    check(general, QStringLiteral("显示操作通知"), "general/notifications", true);
    check(general, QStringLiteral("跟随系统强调色"), "appearance/followSystem", false);
    text(general, QStringLiteral("主题色 HEX"), "appearance/accent", "#54d6b0");
    text(general, QStringLiteral("界面字体"), "appearance/font", "Microsoft YaHei UI");
    text(general, QStringLiteral("自定义托盘图标路径"), "appearance/icon", "");
    const QStringList actions{"capture", "clipboard", "toggle-pins", "history", "settings", "menu", "none"};
    const QStringList labels{QStringLiteral("截图"),          QStringLiteral("剪贴板贴图"),
                             QStringLiteral("显示/隐藏贴图"), QStringLiteral("历史与贴图"),
                             QStringLiteral("设置"),          QStringLiteral("菜单"),
                             QStringLiteral("无操作")};
    choices(general, QStringLiteral("托盘左键"), "tray/left", actions, labels, "capture");
    choices(general, QStringLiteral("托盘中键"), "tray/middle", actions, labels, "clipboard");
    choices(general, QStringLiteral("托盘右键"), "tray/right", actions, labels, "menu");

    auto *capture = page(QStringLiteral("截图"));
    number(capture, QStringLiteral("默认延迟 / 秒"), "capture/delay", 0, 0, 120);
    check(capture, QStringLiteral("捕获鼠标指针"), "capture/cursor", false);
    check(capture, QStringLiteral("其他窗口激活时取消截图"), "capture/cancelOnDeactivate", false);
    check(capture, QStringLiteral("保存取消或未输出的选区到历史"), "history/includeCanceled", false);
    check(capture, QStringLiteral("Alt + Shift + 鼠标拖动进行超级截屏"), "capture/superDrag", true);
    check(capture, QStringLiteral("显示辅助放大镜"), "magnifier/enabled", true);
    check(capture, QStringLiteral("放大镜显示像素网格"), "magnifier/grid", true);
    choices(capture, QStringLiteral("放大镜样式"), "magnifier/style", {"square", "round"},
            {QStringLiteral("方形"), QStringLiteral("圆形")}, "square");
    number(capture, QStringLiteral("放大倍率"), "magnifier/zoom", 8, 2, 24);
    number(capture, QStringLiteral("连续刷新间隔 / 毫秒"), "capture/refreshMs", 1000, 250, 10000);
    choices(capture, QStringLiteral("取色格式"), "color/format", {"HEX", "RGB", "HSV"}, {}, "HEX");
    check(capture, QStringLiteral("HEX 使用大写字母"), "color/uppercase", true);
    text(capture, QStringLiteral("检测排除程序（分号分隔）"), "capture/excluded", "");
    check(capture, QStringLiteral("WASD 作为方向键"), "input/wasd", false);

    auto *shortcuts = page(QStringLiteral("快捷键"));
    check(shortcuts, QStringLiteral("启用全局快捷键"), "hotkeys/enabled", true);
    check(shortcuts, QStringLiteral("允许字母或数字单键快捷键"), "hotkeys/allowSimple", false);
    const QStringList keys{"capture", "clipboard", "toggle", "last", "active", "history", "command"};
    const QStringList names{QStringLiteral("区域截图"),      QStringLiteral("剪贴板贴图"),
                            QStringLiteral("显示/隐藏贴图"), QStringLiteral("复用上次区域"),
                            QStringLiteral("当前活动窗口"),  QStringLiteral("历史"),
                            QStringLiteral("自定义命令")};
    const QStringList defaults{"F1", "F3", "Ctrl+Shift+H", "F4", "Ctrl+Shift+F1", "Ctrl+Shift+F3", ""};
    for (int i = 0; i < keys.size(); ++i) {
        const QString key = "hotkeys/" + keys[i];
        auto *edit = new QKeySequenceEdit(QKeySequence(settings.value(key, defaults[i]).toString()));
        shortcuts->addRow(names[i], edit);
        save.append(
            [&, key, edit] { settings.set(key, edit->keySequence().toString(QKeySequence::PortableText)); });
    }
    text(shortcuts, QStringLiteral("忽略程序（名称/路径，支持 *）"), "hotkeys/ignored", "");
    text(shortcuts, QStringLiteral("自定义命令行"), "hotkeys/commandLine", "");
    text(shortcuts, QStringLiteral("编辑器复制快捷键"), "keys/copy", "Ctrl+C");
    text(shortcuts, QStringLiteral("编辑器保存快捷键"), "keys/save", "Ctrl+S");
    text(shortcuts, QStringLiteral("编辑器贴图快捷键"), "keys/pin", "Ctrl+P");
    check(shortcuts, QStringLiteral("启用屏幕触发角（停留 600 毫秒）"), "corners/enabled", false);
    choices(shortcuts, QStringLiteral("左上角"), "corners/topLeft", actions, labels, "capture");
    choices(shortcuts, QStringLiteral("右上角"), "corners/topRight", actions, labels, "clipboard");
    choices(shortcuts, QStringLiteral("左下角"), "corners/bottomLeft", actions, labels, "none");
    choices(shortcuts, QStringLiteral("右下角"), "corners/bottomRight", actions, labels, "toggle-pins");

    auto *output = page(QStringLiteral("输出与存储"));
    auto *directory =
        text(output, QStringLiteral("快捷保存目录"), "output/directory", settings.outputDirectory());
    auto *browse = new QPushButton(QStringLiteral("选择保存目录"));
    output->addRow(browse);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString path =
            QFileDialog::getExistingDirectory(&dialog, QStringLiteral("选择保存目录"), directory->text());
        if (!path.isEmpty())
            directory->setText(path);
    });
    check(output, QStringLiteral("完成截图时自动保存"), "output/autoSave", false);
    check(output, QStringLiteral("复制时同时提供图片文件"), "output/copyFile", true);
    check(output, QStringLiteral("完成按钮 · 复制到剪贴板"), "output/copy", true);
    check(output, QStringLiteral("完成按钮 · 快捷保存"), "output/save", false);
    check(output, QStringLiteral("完成按钮 · 贴到屏幕"), "output/pin", false);
    check(output, QStringLiteral("导出包含边框和阴影"), "output/frame", false);
    check(output, QStringLiteral("写入软件元数据"), "output/metadata", true);
    check(output, QStringLiteral("移除全不透明图像的 Alpha 通道"), "output/removeOpaqueAlpha", true);
    choices(output, QStringLiteral("快捷保存格式"), "output/format", {"png", "jpg", "bmp"}, {}, "png");
    text(output, QStringLiteral("外部程序路径"), "output/program", "");
    text(output, QStringLiteral("外部程序参数，每行一个，{file} 为图片路径"), "output/arguments", "{file}");
    number(output, QStringLiteral("截图历史保留数量"), "history/limit", 100, 0, 10000);
    number(output, QStringLiteral("关闭贴图可恢复数量"), "pins/recoverLimit", 20, 0, 1000);
    number(output, QStringLiteral("贴图吸附距离 / 像素"), "pins/snapDistance", 12, 0, 100);
    check(output, QStringLiteral("贴图允许边缘缩放"), "pins/edgeResize", true);
    check(output, QStringLiteral("新建标注后自动选中"), "annotation/selectNew", true);
    auto *note =
        new QLabel(QStringLiteral("高级配置位于 %1\n历史目录修改后重启生效，原目录中的记录仍可保留。")
                       .arg(settings.fileName()));
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    output->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存设置"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    for (const auto &write : save)
        write();
    settings.sync();
    if (!Native::setAutostart(settings.value("general/autostart", false).toBool()))
        QMessageBox::warning(parent, QStringLiteral("开机启动"),
                             QStringLiteral("无法写入当前用户的启动项。"));
    applyAppearance(settings);
    return true;
}
