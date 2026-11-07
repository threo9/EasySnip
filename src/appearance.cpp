#include "appearance.h"
#include "settings.h"
#include <QtWidgets>
#include <functional>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
QColor blend(const QColor &color, double amount) {
    return QColor(qRound(255 + (color.red() - 255) * amount),
                  qRound(255 + (color.green() - 255) * amount),
                  qRound(255 + (color.blue() - 255) * amount));
}

// Keep native title bars light even when Windows itself uses a dark theme.
class LightWindowFrames final : public QObject {
  public:
    explicit LightWindowFrames(QObject *parent) : QObject(parent) {}
    bool eventFilter(QObject *object, QEvent *event) override {
#ifdef Q_OS_WIN
        if (event->type() != QEvent::Show && event->type() != QEvent::WinIdChange)
            return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (widget && widget->isWindow() && !(widget->windowFlags() & Qt::FramelessWindowHint) &&
            (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) &&
            widget->internalWinId() && QGuiApplication::platformName() == "windows") {
            const HWND window = reinterpret_cast<HWND>(widget->internalWinId());
            const BOOL dark = FALSE;
            DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
            const DWORD corners = 2; // DWMWCP_ROUND, ignored on older Windows versions.
            DwmSetWindowAttribute(window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
            const COLORREF caption = RGB(243, 243, 243), text = RGB(26, 26, 26);
            DwmSetWindowAttribute(window, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
            DwmSetWindowAttribute(window, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
        }
#else
        Q_UNUSED(object)
        Q_UNUSED(event)
#endif
        return false;
    }
};

class IconCache final : public QObject {
  public:
    using QObject::QObject;
    QHash<QString, QIcon> icons;
    QRgb accent = 0;
    int pixels = 0;
    QString font;
};

QIcon paintedIcon(const QString &key, const std::function<void(QPainter &, const QColor &)> &draw) {
    // Icons are created on the GUI thread and released with the application.
    static QPointer<IconCache> cache;
    if (!cache)
        cache = new IconCache(qApp);
    const QRgb accent = Ui::accent().rgba();
    const int pixels = uiPixels(24) * 2;
    const QString font = qApp->font().key();
    if (cache->accent != accent || cache->pixels != pixels || cache->font != font) {
        cache->icons.clear();
        cache->accent = accent;
        cache->pixels = pixels;
        cache->font = font;
    }
    const auto existing = cache->icons.constFind(key);
    if (existing != cache->icons.cend())
        return existing.value();
    QIcon icon;
    for (auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected}) {
        for (auto state : {QIcon::Off, QIcon::On}) {
            const QColor color = mode == QIcon::Disabled ? QColor("#a0a0a0")
                                : state == QIcon::On || mode == QIcon::Selected ? Ui::accent()
                                                                              : QColor("#303030");
            QPixmap pixmap(pixels, pixels);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(pixmap.width() / 24.0, pixmap.height() / 24.0);
            painter.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            draw(painter, color);
            painter.end();
            pixmap.setDevicePixelRatio(2);
            icon.addPixmap(pixmap, mode, state);
        }
    }
    cache->icons.insert(key, icon);
    return icon;
}
} // namespace

QColor Ui::accent() {
    return qApp->palette().color(QPalette::Active, QPalette::Highlight);
}

QString Ui::scaledStyle(const QString &style) {
    QString result;
    int offset = 0;
    auto matches = QRegularExpression("(\\d+)px").globalMatch(style);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += style.mid(offset, match.capturedStart() - offset);
        result += QString::number(uiPixels(match.captured(1).toInt())) + "px";
        offset = match.capturedEnd();
    }
    return result + style.mid(offset);
}

QIcon Ui::toolIcon(Tool tool) {
    return paintedIcon("tool/" + toolKey(tool), [tool](QPainter &painter, const QColor &color) {
        switch (tool) {
        case Tool::Select:
            painter.drawPolygon(QPolygonF({{5, 3}, {6, 20}, {10, 15}, {14, 21}, {17, 19}, {13, 13}, {20, 12}}));
            break;
        case Tool::Rectangle: painter.drawRect(QRectF(3, 5, 18, 14)); break;
        case Tool::Ellipse: painter.drawEllipse(QRectF(3, 5, 18, 14)); break;
        case Tool::RoundedRectangle: painter.drawRoundedRect(QRectF(3, 5, 18, 14), 4, 4); break;
        case Tool::Line:
        case Tool::Arrow:
            painter.drawLine(4, 20, 20, 4);
            if (tool == Tool::Arrow)
                painter.drawPolyline(QPolygonF({{10, 4}, {20, 4}, {20, 14}}));
            break;
        case Tool::Polyline: painter.drawPolyline(QPolygonF({{3, 18}, {8, 8}, {14, 14}, {21, 4}})); break;
        case Tool::Pen:
            painter.drawPolygon(QPolygonF({{4, 16}, {16, 4}, {20, 8}, {8, 20}, {3, 21}}));
            painter.drawLine(13, 7, 17, 11);
            break;
        case Tool::Mosaic:
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    painter.fillRect(3 + x * 6, 3 + y * 6, 5, 5, (x + y) % 2 ? color : blend(color, 0.35));
            break;
        case Tool::Blur:
            for (int i = 0; i < 4; ++i) {
                QColor faded = color;
                faded.setAlpha(70 + i * 50);
                painter.setPen(QPen(faded, 2, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(4, 5 + i * 5, 20, 5 + i * 5);
            }
            break;
        case Tool::Eraser:
            painter.drawPolygon(QPolygonF({{3, 15}, {13, 4}, {21, 11}, {12, 21}, {9, 21}}));
            painter.drawLine(8, 10, 16, 17);
            break;
        case Tool::Text:
            painter.drawLine(4, 4, 20, 4);
            painter.drawLine(12, 4, 12, 21);
            painter.drawLine(8, 21, 16, 21);
            break;
        case Tool::Number: {
            painter.drawEllipse(QRectF(2, 2, 20, 20));
            QFont font = painter.font();
            font.setPixelSize(15);
            painter.setFont(font);
            painter.drawText(QRect(2, 2, 20, 20), Qt::AlignCenter, "1");
            break;
        }
        case Tool::Magnify:
            painter.drawEllipse(QRectF(3, 3, 13, 13));
            painter.drawLine(15, 15, 22, 22);
            painter.drawLine(6, 9, 13, 9);
            painter.drawLine(9, 6, 9, 13);
            break;
        }
    });
}

QIcon Ui::lineStyleIcon(bool dashed) {
    return paintedIcon("line/" + QString::number(dashed), [dashed](QPainter &painter, const QColor &color) {
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::FlatCap));
        if (dashed) {
            painter.drawLine(QPointF(3, 12), QPointF(10, 12));
            painter.drawLine(QPointF(14, 12), QPointF(21, 12));
        } else {
            painter.drawLine(QPointF(3, 12), QPointF(21, 12));
        }
    });
}

QIcon Ui::fillModeIcon(bool filled) {
    return paintedIcon("fill/" + QString::number(filled), [filled](QPainter &painter, const QColor &color) {
        const QRectF bounds(3, 5, 18, 14);
        if (filled) {
            painter.save();
            painter.setClipRect(bounds.adjusted(1, 1, -1, -1));
            painter.setPen(QPen(color, 0.7, Qt::SolidLine, Qt::FlatCap));
            for (int offset = -12; offset < 32; offset += 3) {
                painter.drawLine(QPointF(bounds.left() + offset, bounds.top()),
                                 QPointF(bounds.left() + offset + bounds.height(), bounds.bottom()));
                painter.drawLine(QPointF(bounds.left() + offset, bounds.bottom()),
                                 QPointF(bounds.left() + offset + bounds.height(), bounds.top()));
            }
            painter.restore();
        }
        painter.drawRect(bounds);
    });
}

QIcon Ui::commandIcon(const QString &command) {
    return paintedIcon("command/" + command, [command](QPainter &painter, const QColor &color) {
        if (command == "copy") {
            painter.drawRoundedRect(QRectF(7, 7, 14, 14), 2, 2);
            painter.drawPolyline(QPolygonF({{16, 3}, {3, 3}, {3, 16}}));
        } else if (command == "save-as" || command == "save") {
            painter.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
            painter.drawRect(QRectF(7, 3, 9, 6));
            painter.drawRect(QRectF(7, 14, 10, 7));
        } else if (command == "finish") {
            painter.setPen(QPen(Ui::accent(), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(QPolygonF({{4, 12}, {10, 18}, {21, 5}}));
        } else if (command == "cancel" || command == "clear") {
            painter.drawLine(5, 5, 19, 19);
            painter.drawLine(5, 19, 19, 5);
        } else if (command == "region" || command == "capture") {
            painter.drawPolyline(QPolygonF({{9, 3}, {3, 3}, {3, 9}}));
            painter.drawPolyline(QPolygonF({{15, 3}, {21, 3}, {21, 9}}));
            painter.drawPolyline(QPolygonF({{3, 15}, {3, 21}, {9, 21}}));
            painter.drawPolyline(QPolygonF({{15, 21}, {21, 21}, {21, 15}}));
            painter.setPen(QPen(color, 1, Qt::DashLine));
            painter.drawRect(QRectF(7, 7, 10, 10));
        } else if (command == "open" || command == "folder") {
            painter.drawPolyline(QPolygonF({{3, 18}, {3, 5}, {10, 5}, {12, 8}, {20, 8}, {20, 10}}));
            painter.drawPolygon(QPolygonF({{3, 20}, {6, 11}, {22, 11}, {19, 20}}));
        } else if (command == "history") {
            painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
            painter.drawLine(3, 9, 21, 9);
            painter.drawLine(9, 9, 9, 20);
        } else if (command == "settings") {
            painter.drawEllipse(QRectF(5, 5, 14, 14));
            painter.drawEllipse(QRectF(9, 9, 6, 6));
            for (int i = 0; i < 8; ++i) {
                painter.save();
                painter.translate(12, 12);
                painter.rotate(i * 45);
                painter.drawLine(0, -7, 0, -10);
                painter.restore();
            }
        } else if (command == "search") {
            painter.drawEllipse(QRectF(4, 3, 13, 13));
            painter.drawLine(15, 15, 21, 21);
        } else if (command == "keyboard") {
            painter.drawRoundedRect(QRectF(2, 5, 20, 14), 2, 2);
            for (int y = 9; y <= 12; y += 3)
                for (int x = 6; x <= 18; x += 4)
                    painter.drawPoint(x, y);
            painter.drawLine(8, 16, 16, 16);
        } else if (command == "more") {
            painter.setBrush(color);
            for (int x : {5, 12, 19})
                painter.drawEllipse(QPointF(x, 12), 1, 1);
        } else {
            if (command == "redo") {
                painter.translate(24, 0);
                painter.scale(-1, 1);
            }
            painter.drawPolyline(QPolygonF({{9, 4}, {3, 10}, {9, 16}}));
            QPainterPath path(QPointF(3, 10));
            path.cubicTo(17, 7, 23, 12, 20, 21);
            painter.drawPath(path);
        }
    });
}

void Ui::addFloatingShadow(QWidget *widget) {
    auto *shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setBlurRadius(uiPixels(18));
    shadow->setOffset(0, uiPixels(3));
    shadow->setColor(QColor(0, 0, 0, 38));
    widget->setGraphicsEffect(shadow);
}

void Ui::applyLightAppearance(const Settings &settings) {
    const auto *screen = QGuiApplication::primaryScreen();
    const double systemScale = screen ? screen->logicalDotsPerInch() / 96.0 : 1.0;
    qApp->setProperty("EasySnipUiScale", qBound(1.0, settings.value("appearance/uiScale", systemScale).toDouble(), 3.0));
    if (!qApp->property("EasySnipLightFrames").toBool()) {
        qApp->setStyle("Fusion");
        qApp->installEventFilter(new LightWindowFrames(qApp));
        qApp->setProperty("EasySnipLightFrames", true);
    }
    QColor accent(settings.value("appearance/accent", "#0067c0").toString());
    if (settings.value("appearance/followSystem", false).toBool()) {
        QSettings windows("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM", QSettings::NativeFormat);
        accent = QColor::fromRgb(windows.value("ColorizationColor", 0xff0067c0).toUInt() & 0x00ffffff);
    }
    if (!accent.isValid())
        accent = QColor("#0067c0");
    accent.setAlpha(255);
    auto linear = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * linear(accent.redF()) + 0.7152 * linear(accent.greenF()) + 0.0722 * linear(accent.blueF());
    const QString foreground = luminance > 0.179 ? "#1a1a1a" : "#ffffff";
    QPalette palette;
    const QMap<QPalette::ColorRole, QColor> colors{
        {QPalette::Window, QColor("#f3f3f3")}, {QPalette::WindowText, QColor("#1a1a1a")},
        {QPalette::Base, Qt::white}, {QPalette::AlternateBase, QColor("#f9f9f9")},
        {QPalette::Text, QColor("#1a1a1a")}, {QPalette::Button, QColor("#ffffff")},
        {QPalette::ButtonText, QColor("#1a1a1a")}, {QPalette::Highlight, accent},
        {QPalette::HighlightedText, QColor(foreground)}, {QPalette::Light, Qt::white},
        {QPalette::Midlight, QColor("#eeeeee")}, {QPalette::Mid, QColor("#d1d1d1")},
        {QPalette::Dark, QColor("#8a8a8a")}, {QPalette::Shadow, QColor("#b5b5b5")},
        {QPalette::ToolTipBase, QColor("#ffffff")}, {QPalette::ToolTipText, QColor("#1a1a1a")},
        {QPalette::Link, accent}, {QPalette::PlaceholderText, QColor("#707070")}};
    for (auto it = colors.cbegin(); it != colors.cend(); ++it)
        palette.setColor(it.key(), it.value());
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, QColor("#9b9b9b"));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#dedede"));
    if (qApp->palette() != palette)
        qApp->setPalette(palette);
    QFont font(settings.value("appearance/font", "Microsoft YaHei UI").toString());
    font.setPixelSize(uiPixels(13));
    if (qApp->font() != font)
        qApp->setFont(font);
    const int doubleClickMs = qBound(100, settings.value("input/doubleClickMs", 400).toInt(), 1000);
    if (qApp->doubleClickInterval() != doubleClickMs)
        qApp->setDoubleClickInterval(doubleClickMs);
    const QVariantList styleKey{accent.rgba(), qApp->property("EasySnipUiScale")};
    if (qApp->property("EasySnipStyleKey").toList() == styleKey &&
        qApp->property("EasySnipStyleSheet").toString() == qApp->styleSheet())
        return;
    const QString style = QStringLiteral(
        "QWidget { color:#1a1a1a; font-size:13px; }"
        "QMainWindow,QDialog { background:#f3f3f3; }"
        "QLabel { background:transparent; border:0; }"
        "QLabel#heading { font-size:28px; font-weight:600; }"
        "QLabel#sectionHeading { font-size:18px; font-weight:600; }"
        "QLabel#pageHeading { font-size:22px; font-weight:600; }"
        "QLabel#muted { color:#646464; }"
        "QLabel#brand { font-size:23px; font-weight:600; }"
        "QFrame#sidebar { background:#ededed; border:0; border-right:1px solid #dfdfdf; }"
        "QPushButton,QToolButton { background:#ffffff; border:1px solid #d6d6d6; border-bottom-color:#bfbfbf; "
        "border-radius:5px; padding:7px 12px; }"
        "QPushButton:hover,QToolButton:hover { background:#f9f9f9; border-color:#c6c6c6; }"
        "QPushButton:pressed,QToolButton:pressed { background:#f0f0f0; color:#606060; border-color:#d6d6d6; }"
        "QPushButton:checked,QToolButton:checked { background:%2; color:%1; border-color:%3; }"
        "QPushButton:focus,QToolButton:focus { border-color:%1; }"
        "QPushButton#primary { background:%1; color:%4; border-color:%1; font-weight:600; }"
        "QPushButton#primary:hover { background:%5; border-color:%5; }"
        "QPushButton#primary:pressed { background:%6; border-color:%6; }"
        "QPushButton:disabled,QToolButton:disabled { color:#9b9b9b; background:#f5f5f5; border-color:#e3e3e3; }"
        "QPushButton[navItem=true] { text-align:left; background:transparent; border:1px solid transparent; padding:10px 12px; }"
        "QPushButton[navItem=true]:hover { background:#e4e4e4; }"
        "QPushButton[navItem=true]:checked { background:#ffffff; border-color:#e1e1e1; color:%1; }"
        "QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QKeySequenceEdit,QPlainTextEdit,QTextEdit { "
        "background:#ffffff; border:1px solid #d4d4d4; border-bottom-color:#929292; border-radius:5px; "
        "padding:6px 9px; selection-background-color:%1; selection-color:%4; }"
        "QLineEdit:hover,QComboBox:hover,QSpinBox:hover,QDoubleSpinBox:hover { background:#fcfcfc; }"
        "QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus,QKeySequenceEdit:focus,"
        "QPlainTextEdit:focus,QTextEdit:focus { border-bottom:2px solid %1; }"
        "QLineEdit:disabled,QComboBox:disabled,QSpinBox:disabled { background:#f5f5f5; color:#9b9b9b; border-color:#e3e3e3; }"
        "QComboBox { padding-right:28px; }"
        "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }"
        "QComboBox::down-arrow { image:url(:/theme/chevron-down.svg); width:12px; height:12px; }"
        "QComboBox QAbstractItemView { background:#ffffff; border:1px solid #d6d6d6; padding:4px; "
        "selection-background-color:%2; selection-color:#1a1a1a; outline:0; }"
        "QAbstractSpinBox { padding-right:23px; }"
        "QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; "
        "width:21px; border:0; margin-top:2px; margin-right:2px; }"
        "QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; "
        "width:21px; border:0; margin-bottom:2px; margin-right:2px; }"
        "QSpinBox::up-arrow,QDoubleSpinBox::up-arrow { image:url(:/theme/chevron-up.svg); width:10px; height:10px; }"
        "QSpinBox::down-arrow,QDoubleSpinBox::down-arrow { image:url(:/theme/chevron-down.svg); width:10px; height:10px; }"
        "QSpinBox::up-button:hover,QSpinBox::down-button:hover,QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover { background:#eeeeee; }"
        "QListWidget,QTreeWidget,QScrollArea { background:transparent; border:0; outline:0; }"
        "QListWidget::item { border:1px solid transparent; border-radius:5px; padding:6px; }"
        "QListWidget::item:hover { background:#ebebeb; }"
        "QListWidget::item:selected { background:%2; border-color:%3; color:#1a1a1a; }"
        "QListWidget#settingsNavigation::item { padding:11px 12px; margin:2px 0; }"
        "QListWidget#settingsNavigation::item:selected { background:#ffffff; border-color:#e1e1e1; color:%1; }"
        "QFrame#settingsPanel,QWidget#inspectorPanel { background:#ffffff; border:1px solid #e0e0e0; border-radius:8px; }"
        "QWidget#settingsPage { background:#ffffff; }"
        "QScrollArea#canvasArea { background:#e8e8e8; border:1px solid #d6d6d6; border-radius:8px; }"
        "QFrame#editorToolStrip { background:#ffffff; border:1px solid #dfdfdf; border-radius:8px; }"
        "QFrame#captureControls,QFrame#captureEditorToolbar,QFrame#captureToolOptions { "
        "background:#fbfbfb; border:1px solid #cecece; border-radius:8px; }"
        "QToolBar { background:transparent; border:0; spacing:3px; padding:0; }"
        "QToolBar QToolButton,QFrame#editorToolStrip QToolButton { background:transparent; border:1px solid transparent; "
        "padding:6px; border-radius:5px; }"
        "QToolBar QToolButton:hover,QFrame#editorToolStrip QToolButton:hover { background:#ededed; border-color:#e4e4e4; }"
        "QToolBar QToolButton:checked,QFrame#editorToolStrip QToolButton:checked { background:%2; border-color:%3; }"
        "QToolBar::separator { background:#dedede; width:1px; margin:5px; }"
        "QToolButton::menu-button { border:0; width:14px; }"
        "QPushButton::menu-indicator,QToolButton::menu-indicator { image:url(:/theme/chevron-down.svg); width:10px; height:10px; "
        "subcontrol-position:right center; right:5px; }"
        "QPushButton[hasMenu=true] { padding-right:24px; }"
        "QToolButton#captureMore::menu-indicator { image:none; width:0; }"
        "QMenu { background:#fcfcfc; border:1px solid #d1d1d1; border-radius:8px; padding:5px; }"
        "QMenu::item { padding:8px 30px 8px 12px; border-radius:4px; border:1px solid transparent; }"
        "QMenu::item:selected { background:#eeeeee; border-color:#e8e8e8; }"
        "QMenu::item:disabled { color:#9b9b9b; }"
        "QMenu::separator { height:1px; background:#e2e2e2; margin:4px 8px; }"
        "QToolTip { color:#1a1a1a; background:#ffffff; border:1px solid #d1d1d1; border-radius:4px; padding:7px 10px; }"
        "QCheckBox { spacing:10px; padding:5px 0; background:transparent; }"
        "QCheckBox::indicator { width:18px; height:18px; border:1px solid #858585; border-radius:4px; background:#ffffff; }"
        "QCheckBox::indicator:hover { background:#f3f3f3; border-color:#5c5c5c; }"
        "QCheckBox::indicator:checked { background:%1; border-color:%1; image:url(:/theme/%7); }"
        "QCheckBox::indicator:checked:hover { background:%5; }"
        "QCheckBox::indicator:disabled { background:#f0f0f0; border-color:#c8c8c8; }"
        "QSlider::groove:horizontal { height:4px; background:#d6d6d6; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:%1; border-radius:2px; }"
        "QSlider::handle:horizontal { width:12px; margin:-8px 0; background:%1; "
        "border:4px solid #ffffff; border-radius:10px; }"
        "QSlider::handle:horizontal:hover { background:%5; border-color:#f8f8f8; }"
        "QSlider::sub-page:horizontal:disabled { background:#bcbcbc; }"
        "QSlider::handle:horizontal:disabled { background:#bcbcbc; border-color:#f8f8f8; }"
        "QLabel#valueReadout { color:#565656; font-size:12px; }"
        "QStatusBar { background:#f3f3f3; color:#646464; border-top:1px solid #dfdfdf; }"
        "QStatusBar::item { border:0; }"
        "QSplitter::handle { background:#e5e5e5; }"
        "QScrollBar:vertical { background:transparent; width:10px; margin:2px; }"
        "QScrollBar::handle:vertical { background:#b6b6b6; min-height:28px; border-radius:3px; }"
        "QScrollBar::handle:vertical:hover { background:#8f8f8f; }"
        "QScrollBar:horizontal { background:transparent; height:10px; margin:2px; }"
        "QScrollBar::handle:horizontal { background:#b6b6b6; min-width:28px; border-radius:3px; }"
        "QScrollBar::handle:horizontal:hover { background:#8f8f8f; }"
        "QScrollBar::add-line,QScrollBar::sub-line { width:0; height:0; background:transparent; border:0; }"
        "QScrollBar::add-page,QScrollBar::sub-page { background:transparent; }"
        "QListWidget#annotationPalette { background:transparent; }"
        "QListWidget#annotationPalette::item { padding:2px; border-radius:4px; }"
    ).arg(accent.name(), blend(accent, 0.09).name(), blend(accent, 0.3).name(), foreground,
          accent.lighter(108).name(), accent.lighter(118).name(),
          foreground == "#ffffff" ? "check.svg" : "check-dark.svg");
    const QString scaled = scaledStyle(style);
    if (qApp->styleSheet() != scaled)
        qApp->setStyleSheet(scaled);
    qApp->setProperty("EasySnipStyleKey", styleKey);
    qApp->setProperty("EasySnipStyleSheet", scaled);
}
