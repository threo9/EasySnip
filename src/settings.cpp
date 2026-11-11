#include "settings.h"
#include "appearance.h"
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

QString Settings::shortcut(const QString &command) const {
    const QMap<QString, QString> defaults{{"capture", "F1"}, {"last", "F4"}, {"active", "Ctrl+Shift+F1"},
        {"history", "Ctrl+Shift+F3"}, {"command", ""}};
    return value("hotkeys/" + command, defaults.value(command)).toString();
}

bool Settings::ignoresShortcut(const QString &command, const QString &executable) const {
    const QString patterns = value("hotkeys/ignored").toString() + ';' +
                             value("hotkeys/ignored/" + command).toString();
    return Native::matchesProcess(executable, patterns.split(';', Qt::SkipEmptyParts));
}

QString Settings::action(const QString &key, const QString &fallback) const {
    const QStringList commands{"capture", "scroll", "screen", "fullscreen", "active", "last", "preset",
        "history", "settings", "menu", "none", "clear-history", "backup", "restore", "custom-command", "quit"};
    const QString selected = value(key, fallback).toString();
    return commands.contains(selected) ? selected : fallback;
}

int uiPixels(int pixels) {
    const double scale = qApp->property("EasySnipUiScale").toDouble();
    return qRound(pixels * (scale > 0 ? scale : 1.0));
}

void applyAppearance(const Settings &settings) {
    Ui::applyLightAppearance(settings);
}

bool showSettings(Settings &settings, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("settingsDialog");
    dialog.setWindowTitle(QStringLiteral("EasySnip · 设置"));
    dialog.resize(QSize(uiPixels(960), uiPixels(760))
                      .boundedTo(QGuiApplication::primaryScreen()->availableGeometry().size() * 0.92));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(uiPixels(24), uiPixels(22), uiPixels(24), uiPixels(18));
    layout->setSpacing(uiPixels(18));
    auto *heading = new QLabel(QStringLiteral("设置"));
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *body = new QHBoxLayout;
    body->setSpacing(uiPixels(20));
    layout->addLayout(body, 1);
    auto *navigation = new QListWidget;
    navigation->setObjectName("settingsNavigation");
    navigation->setFixedWidth(uiPixels(148));
    navigation->setIconSize(QSize(uiPixels(18), uiPixels(18)));
    navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(navigation);
    auto *panel = new QFrame;
    panel->setObjectName("settingsPanel");
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(1, 1, 1, 1);
    auto *pages = new QStackedWidget;
    panelLayout->addWidget(pages);
    body->addWidget(panel, 1);
    QObject::connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    QVector<std::function<void()>> save;
    auto page = [&](const QString &title, const QString &icon, const QString &description) {
        auto *widget = new QWidget;
        widget->setObjectName("settingsPage");
        auto *form = new QFormLayout(widget);
        form->setContentsMargins(uiPixels(24), uiPixels(22), uiPixels(24), uiPixels(24));
        form->setHorizontalSpacing(uiPixels(20));
        form->setVerticalSpacing(uiPixels(15));
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *titleLabel = new QLabel(title);
        titleLabel->setObjectName("pageHeading");
        form->addRow(titleLabel);
        auto *note = new QLabel(description);
        note->setObjectName("muted");
        note->setWordWrap(true);
        form->addRow(note);
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(widget);
        pages->addWidget(scroll);
        new QListWidgetItem(Ui::commandIcon(icon), title, navigation);
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
        const int index = values.indexOf(settings.value(key, fallback).toString());
        combo->setCurrentIndex(index >= 0 ? index : values.indexOf(fallback));
        form->addRow(label, combo);
        save.append([&, combo, key] { settings.set(key, combo->currentData()); });
    };

    auto *general = page(QStringLiteral("常规"), "settings", QStringLiteral("启动方式、界面外观与托盘操作。"));
    check(general, QStringLiteral("开机启动"), "general/autostart", false);
    check(general, QStringLiteral("以管理员权限启动（下次启动时请求 UAC 确认）"), "runtime/administrator", false);
    check(general, QStringLiteral("显示托盘图标"), "general/tray", true);
    check(general, QStringLiteral("显示操作通知"), "general/notifications", true);
    check(general, QStringLiteral("跟随系统强调色"), "appearance/followSystem", false);
    text(general, QStringLiteral("强调色 HEX"), "appearance/accent", "#0067c0");
    text(general, QStringLiteral("界面字体"), "appearance/font", "Microsoft YaHei UI");
    text(general, QStringLiteral("自定义托盘图标路径"), "appearance/icon", "");
    const QStringList actions{"capture", "scroll", "history", "settings", "menu", "none"};
    const QStringList labels{QStringLiteral("截图"), QStringLiteral("滚动长截屏"), QStringLiteral("截图历史"),
                             QStringLiteral("设置"),          QStringLiteral("菜单"),
                             QStringLiteral("无操作")};
    choices(general, QStringLiteral("托盘左键"), "tray/left", actions, labels, "capture");
    choices(general, QStringLiteral("托盘中键"), "tray/middle", actions, labels, "none");
    choices(general, QStringLiteral("托盘右键"), "tray/right", actions, labels, "menu");

    auto *capture = page(QStringLiteral("截图"), "capture", QStringLiteral("调整选区、元素检测与取色体验。"));
    number(capture, QStringLiteral("默认延迟 / 秒"), "capture/delay", 0, 0, 120);
    check(capture, QStringLiteral("捕获鼠标指针"), "capture/cursor", false);
    check(capture, QStringLiteral("自动检测界面元素"), "capture/elements", false);
    check(capture, QStringLiteral("其他窗口激活时取消截图"), "capture/cancelOnDeactivate", false);
    check(capture, QStringLiteral("保存取消或未输出的选区到历史"), "history/includeCanceled", false);
    check(capture, QStringLiteral("启用修饰键 + 鼠标拖动超级截屏"), "capture/superDrag", true);
    choices(capture, QStringLiteral("超级截屏修饰键"), "capture/superModifiers", {"Alt+Shift", "Win", "Ctrl+Alt"},
            {}, "Alt+Shift");
    check(capture, QStringLiteral("显示辅助放大镜"), "magnifier/enabled", true);
    check(capture, QStringLiteral("放大镜显示像素网格"), "magnifier/grid", true);
    choices(capture, QStringLiteral("放大镜样式"), "magnifier/style", {"square", "round"},
            {QStringLiteral("方形"), QStringLiteral("圆形")}, "square");
    number(capture, QStringLiteral("放大倍率"), "magnifier/zoom", 8, 2, 24);
    number(capture, QStringLiteral("连续刷新间隔 / 毫秒"), "capture/refreshMs", 1000, 250, 10000);
    choices(capture, QStringLiteral("取色格式"), "color/format", {"HEX", "RGB", "HSV"}, {}, "HEX");
    check(capture, QStringLiteral("HEX 使用大写字母"), "color/uppercase", true);
    text(capture, QStringLiteral("检测排除程序（分号分隔）"), "capture/excluded", "");
    check(capture, QStringLiteral("WASD 逐像素移动鼠标（含拖拽中）"), "input/wasd", true);

    auto *shortcuts = page(QStringLiteral("快捷键"), "keyboard", QStringLiteral("配置全局按键、编辑器操作和屏幕触发角。"));
    check(shortcuts, QStringLiteral("启用全局快捷键"), "hotkeys/enabled", true);
    check(shortcuts, QStringLiteral("允许字母或数字单键快捷键"), "hotkeys/allowSimple", false);
    const QStringList keys{"capture", "last", "active", "history", "command"};
    const QStringList names{QStringLiteral("区域截图"), QStringLiteral("复用上次区域"),
                            QStringLiteral("当前活动窗口"),  QStringLiteral("历史"),
                            QStringLiteral("自定义命令")};
    for (int i = 0; i < keys.size(); ++i) {
        const QString key = "hotkeys/" + keys[i];
        auto *edit = new QKeySequenceEdit(QKeySequence(settings.shortcut(keys[i])));
        shortcuts->addRow(names[i], edit);
        save.append(
            [&, key, edit] { settings.set(key, edit->keySequence().toString(QKeySequence::PortableText)); });
        text(shortcuts, names[i] + QStringLiteral("忽略程序"), "hotkeys/ignored/" + keys[i], "");
    }
    text(shortcuts, QStringLiteral("忽略程序（名称/路径，支持 *）"), "hotkeys/ignored", "");
    text(shortcuts, QStringLiteral("自定义命令行"), "hotkeys/commandLine", "");
    text(shortcuts, QStringLiteral("编辑器复制快捷键"), "keys/copy", "Ctrl+C");
    text(shortcuts, QStringLiteral("编辑器保存快捷键"), "keys/save", "Ctrl+S");
    check(shortcuts, QStringLiteral("启用屏幕触发角（停留 600 毫秒）"), "corners/enabled", false);
    choices(shortcuts, QStringLiteral("左上角"), "corners/topLeft", actions, labels, "capture");
    choices(shortcuts, QStringLiteral("右上角"), "corners/topRight", actions, labels, "none");
    choices(shortcuts, QStringLiteral("左下角"), "corners/bottomLeft", actions, labels, "none");
    choices(shortcuts, QStringLiteral("右下角"), "corners/bottomRight", actions, labels, "none");

    auto *output = page(QStringLiteral("输出与存储"), "folder", QStringLiteral("选择保存位置、输出格式和历史保留方式。"));
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
    check(output, QStringLiteral("导出包含边框和阴影"), "output/frame", false);
    check(output, QStringLiteral("写入软件元数据"), "output/metadata", true);
    check(output, QStringLiteral("移除全不透明图像的 Alpha 通道"), "output/removeOpaqueAlpha", true);
    choices(output, QStringLiteral("快捷保存格式"), "output/format", {"png", "jpg", "bmp"}, {}, "png");
    text(output, QStringLiteral("外部程序路径"), "output/program", "");
    text(output, QStringLiteral("外部程序参数，每行一个，{file} 为图片路径"), "output/arguments", "{file}");
    number(output, QStringLiteral("截图历史保留数量"), "history/limit", 100, 0, 10000);
    check(output, QStringLiteral("新建标注后自动选中"), "annotation/selectNew", true);
    auto *note =
        new QLabel(QStringLiteral("高级配置位于 %1\n历史目录修改后重启生效，原目录中的记录仍可保留。")
                       .arg(settings.fileName()));
    note->setWordWrap(true);
    note->setObjectName("muted");
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    output->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存设置"));
    buttons->button(QDialogButtonBox::Save)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    navigation->setCurrentRow(0);
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
