#include "capture.h"
#include "editor.h"
#include <QtWidgets>
#include <QtConcurrent>

namespace {
QIcon toolIcon(Tool tool) {
    QPixmap pixmap(uiPixels(24) * 2, uiPixels(24) * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pixmap.width() / 24.0, pixmap.height() / 24.0);
    painter.setPen(QPen(QColor("#e8edf5"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (tool) {
    case Tool::Select:
        painter.drawPolygon(QPolygonF({{5, 3}, {6, 20}, {10, 15}, {14, 21}, {17, 19}, {13, 13}, {20, 12}}));
        break;
    case Tool::Rectangle:
        painter.drawRect(QRectF(3, 5, 18, 14));
        break;
    case Tool::Ellipse:
        painter.drawEllipse(QRectF(3, 5, 18, 14));
        break;
    case Tool::RoundedRectangle:
        painter.drawRoundedRect(QRectF(3, 5, 18, 14), 4, 4);
        break;
    case Tool::Line:
    case Tool::Arrow:
        painter.drawLine(4, 20, 20, 4);
        if (tool == Tool::Arrow)
            painter.drawPolyline(QPolygonF({{10, 4}, {20, 4}, {20, 14}}));
        break;
    case Tool::Polyline:
        painter.drawPolyline(QPolygonF({{3, 18}, {8, 8}, {14, 14}, {21, 4}}));
        break;
    case Tool::Pen:
    case Tool::Highlighter:
        painter.drawPolygon(QPolygonF({{4, 16}, {16, 4}, {20, 8}, {8, 20}, {3, 21}}));
        painter.drawLine(13, 7, 17, 11);
        if (tool == Tool::Highlighter) {
            painter.setPen(QPen(QColor("#ffbd59"), 3));
            painter.drawLine(3, 22, 21, 22);
        }
        break;
    case Tool::Mosaic:
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                painter.fillRect(3 + x * 6, 3 + y * 6, 6, 6,
                                 (x + y) % 2 ? QColor("#e8edf5") : QColor("#718198"));
        break;
    case Tool::Blur:
        for (int i = 0; i < 4; ++i) {
            painter.setPen(QPen(QColor(232, 237, 245, 70 + i * 50), 2));
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
    painter.end();
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}

QIcon outputIcon(const QString &name) {
    QPixmap pixmap(uiPixels(24) * 2, uiPixels(24) * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pixmap.width() / 24.0, pixmap.height() / 24.0);
    painter.setPen(QPen(QColor("#e8edf5"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "copy") {
        painter.drawRect(QRectF(7, 7, 14, 14));
        painter.drawPolyline(QPolygonF({{16, 4}, {3, 4}, {3, 17}}));
    } else if (name == "save-as") {
        painter.drawRect(QRectF(3, 3, 18, 18));
        painter.drawRect(QRectF(7, 3, 9, 6));
        painter.drawRect(QRectF(7, 14, 10, 7));
    } else if (name == "finish") {
        painter.setPen(QPen(QColor("#54d6b0"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF({{4, 12}, {10, 18}, {21, 5}}));
    } else if (name == "cancel") {
        painter.drawLine(5, 5, 19, 19);
        painter.drawLine(5, 19, 19, 5);
    } else if (name == "region") {
        painter.setPen(QPen(QColor("#e8edf5"), 1.5, Qt::DashLine));
        painter.drawRect(QRectF(3, 3, 18, 18));
    } else {
        if (name == "redo") {
            painter.translate(24, 0);
            painter.scale(-1, 1);
        }
        painter.drawPolyline(QPolygonF({{9, 4}, {3, 10}, {9, 16}}));
        QPainterPath path(QPointF(3, 10));
        path.cubicTo(17, 7, 23, 12, 20, 21);
        painter.drawPath(path);
    }
    painter.end();
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}
} // namespace

CaptureOverlay::CaptureOverlay(Native::Desktop desktop, Settings &settings, const QRect &initial)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      desktop_(std::move(desktop)), settings_(settings) {
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setGeometry(desktop_.geometry);
    cursor_ = settings_.value("capture/cursor", false).toBool();
    elements_ = settings_.value("capture/elements", false).toBool();
    selection_ = initial.intersected(desktop_.geometry).translated(-desktop_.geometry.topLeft());
    if (initial.isEmpty())
        selection_ = {};
    mouse_ = QCursor::pos() - desktop_.geometry.topLeft();
    for (const auto &window : desktop_.windows) {
        detectedElements_.append(window.geometry);
        for (const auto &element : window.elements)
            if (!detectedElements_.contains(element))
                detectedElements_.append(element);
    }
    detectionAnimation_.setDuration(100);
    detectionAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&detectionAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        animatedHover_ = value.toRect();
        update();
    });
    toolbar_ = new QFrame(this);
    toolbar_->setStyleSheet("QFrame { background:#172131; border:1px solid #41516a; border-radius:8px; } "
                            "QPushButton { padding:7px 10px; }");
    auto *layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(8, 8, 8, 8);
    auto button = [&](const QString &label, const QString &tip, const std::function<void()> &callback) {
        auto *widget = new QPushButton(label);
        widget->setToolTip(tip);
        widget->setFocusPolicy(Qt::NoFocus);
        connect(widget, &QPushButton::clicked, this, callback);
        layout->addWidget(widget);
        return widget;
    };
    button(QStringLiteral("全屏"), "F", [this] {
        selection_ = rect();
        syncSelection();
    });
    button(QStringLiteral("当前屏"), "Shift+S", [this] {
        for (const auto &screen : desktop_.screens)
            if (screen.contains(QCursor::pos()))
                selection_ = screen.translated(-desktop_.geometry.topLeft());
        syncSelection();
    });
    button(QStringLiteral("尺寸 / 比例"), "Ctrl+D", [this] { setDimensions(); });
    auto *element =
        button(QStringLiteral("元素"), QStringLiteral("Tab 切换窗口 / 元素检测，滚轮选择层级"), [this] {
            elements_ = !elements_;
            settings_.set("capture/elements", elements_);
            invalidateDetection();
            updateDetection();
        });
    element->setCheckable(true);
    element->setChecked(elements_);
    element->setObjectName("elementToggle");
    auto *pointer = button(QStringLiteral("指针"), "I", [this] {
        cursor_ = !cursor_;
        syncSelection();
    });
    pointer->setCheckable(true);
    pointer->setChecked(cursor_);
    pointer->setObjectName("pointerToggle");
    button(QStringLiteral("刷新"), QStringLiteral("R 刷新，Shift+R 连续刷新 / 定格"), [this] { refresh(); });
    button(QStringLiteral("取消"), "Esc", [this] { cancel(); });
    auto *accept = button(QStringLiteral("完成"), QStringLiteral("Enter 或双击选区"),
                          [this] { requestOutput(); });
    accept->setObjectName("primary");
    toolbar_->adjustSize();
    createEditorToolbar();
    syncSelection();
    detectionTimer_.setInterval(75);
    connect(&detectionTimer_, &QTimer::timeout, this, &CaptureOverlay::updateDetection);
    detectionTimer_.start();
    connect(&refreshTimer_, &QTimer::timeout, this, &CaptureOverlay::refresh);
    connect(&elementsWatcher_, &QFutureWatcher<QVector<QRect>>::finished, this, [this] {
        if (done_ || refreshing_ || !elements_ || historyIndex_ >= 0 || pressed_ ||
            (!selection_.isEmpty() && selecting_) ||
            queryGeneration_ != detectionGeneration_ || QCursor::pos() != elementPoint_)
            return;
        applyDetection(elementsWatcher_.result());
    });
}

void CaptureOverlay::createEditorToolbar() {
    editorToolbar_ = new QFrame(this);
    editorToolbar_->setObjectName("captureEditorToolbar");
    editorToolbar_->setStyleSheet(
        "QFrame#captureEditorToolbar { background:#172131; border:1px solid #41516a; border-radius:6px; }"
        "QToolBar { background:transparent; border:0; spacing:2px; }"
        "QToolButton { padding:5px; border:1px solid transparent; background:transparent; }"
        "QToolButton:hover { background:#2f3d54; border-color:#54d6b0; }"
        "QToolButton:checked { background:#335c59; border-color:#54d6b0; }"
        "QToolButton::menu-button { background:transparent; border:0; border-left:1px solid #41516a; width:12px; }"
        "QToolBar::separator { background:#41516a; width:1px; margin:5px; }");
    auto *layout = new QVBoxLayout(editorToolbar_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    toolbarActions_ = new QBoxLayout(QBoxLayout::LeftToRight);
    toolbarActions_->setSpacing(0);
    annotationTools_ = new QToolBar;
    outputTools_ = new QToolBar;
    auto *bar = annotationTools_;
    bar->setIconSize(QSize(uiPixels(22), uiPixels(22)));
    bar->setMovable(false);
    outputTools_->setIconSize(bar->iconSize());
    outputTools_->setMovable(false);
    toolbarActions_->addWidget(bar);
    toolbarActions_->addWidget(outputTools_);
    layout->addLayout(toolbarActions_);
    tools_ = new QActionGroup(this);
    adjustSelection_ = bar->addAction(outputIcon("region"), QStringLiteral("调整截图区域"));
    adjustSelection_->setObjectName("adjustCaptureRegion");
    adjustSelection_->setData(-1);
    adjustSelection_->setCheckable(true);
    adjustSelection_->setChecked(true);
    tools_->addAction(adjustSelection_);
    const QVector<QVector<Tool>> groups{{Tool::Select},
                                        {Tool::Rectangle, Tool::Ellipse, Tool::RoundedRectangle},
                                        {Tool::Arrow, Tool::Line, Tool::Polyline},
                                        {Tool::Pen}, {Tool::Highlighter}, {Tool::Mosaic, Tool::Blur},
                                        {Tool::Text, Tool::Number}, {Tool::Eraser}, {Tool::Magnify}};
    for (const auto &group : groups) {
        auto *button = new QToolButton;
        button->setIconSize(bar->iconSize());
        button->setFocusPolicy(Qt::NoFocus);
        QMenu *menu = group.size() > 1 ? new QMenu(button) : nullptr;
        for (Tool tool : group) {
            auto *action = new QAction(toolIcon(tool), toolName(tool), tools_);
            action->setObjectName("captureTool" + toolKey(tool));
            action->setData(int(tool));
            action->setCheckable(true);
            tools_->addAction(action);
            if (menu)
                menu->addAction(action);
            if (tool == group.first())
                button->setDefaultAction(action);
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        }
        if (menu) {
            button->setStyleSheet("QToolButton { padding-right:16px; }");
            button->setMenu(menu);
            button->setPopupMode(QToolButton::MenuButtonPopup);
        }
        bar->addWidget(button);
    }
    connect(tools_, &QActionGroup::triggered, this, &CaptureOverlay::selectTool);
    bar->addSeparator();
    undo_ = bar->addAction(outputIcon("undo"), QStringLiteral("撤销 · Ctrl+Z"), this, [this] {
        if (canvas_)
            canvas_->undo();
    });
    redo_ = bar->addAction(outputIcon("redo"), QStringLiteral("重做 · Ctrl+Y"), this, [this] {
        if (canvas_)
            canvas_->redo();
    });
    bar = outputTools_;
    bar->addAction(outputIcon("cancel"), QStringLiteral("取消 · Esc"), this, &CaptureOverlay::cancel);
    auto output = [this, bar](const QString &label, const QString &name, const QString &key) {
        auto *action = bar->addAction(outputIcon(name), label, this, [this, name] {
            const bool quick = name == "save-as" && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
            requestOutput(quick ? "save" : name);
        });
        action->setObjectName("captureOutput-" + name);
        action->setShortcut(QKeySequence(key));
        action->setShortcutContext(Qt::WindowShortcut);
        return action;
    };
    output(QStringLiteral("另存为"), "save-as", settings_.value("keys/save", "Ctrl+S").toString());
    auto *copy = output(QStringLiteral("复制图像"), "copy", {});
    auto *copyKey = new QShortcut(QKeySequence(settings_.value("keys/copy", "Ctrl+C").toString()), this);
    connect(copyKey, &QShortcut::activated, copy, &QAction::trigger);
    auto *quickSaveKey = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
    connect(quickSaveKey, &QShortcut::activated, this, [this] { requestOutput("save"); });
    output(QStringLiteral("完成 · Enter"), "finish", "Ctrl+Return");
    auto *more = new QToolButton;
    more->setText(QStringLiteral("更多"));
    more->setToolTip(QStringLiteral("选区设置与更多输出"));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setFocusPolicy(Qt::NoFocus);
    auto *menu = new QMenu(more);
    menu->addAction(QStringLiteral("尺寸 / 比例 · Ctrl+D"), this, &CaptureOverlay::setDimensions);
    menu->addAction(QStringLiteral("刷新背景 · R"), this, &CaptureOverlay::refresh);
    menu->addAction(QStringLiteral("清除标注"), this, [this] {
        if (canvas_)
            canvas_->clear();
    });
    if (settings_.value("output/menuVisible", true).toBool()) {
        menu->addSeparator();
        for (const auto &pair : QVector<QPair<QString, QString>>{{QStringLiteral("快捷保存"), "save"},
                {QStringLiteral("打印"), "print"}, {QStringLiteral("Windows 系统分享"), "share"},
                {QStringLiteral("发送到外部程序"), "external"}})
            menu->addAction(pair.first, this, [this, pair] { requestOutput(pair.second); });
        menu->addAction(QStringLiteral("识别二维码 / 条形码"), this, [this] {
            const QString result = decodeCodes(selectionDocument().render());
            QInputDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("识别结果"));
            dialog.setLabelText(QStringLiteral("可选中并复制识别内容"));
            dialog.setOption(QInputDialog::UsePlainTextEditForTextInput);
            dialog.setTextValue(result.isEmpty() ? QStringLiteral("未检测到可读取的二维码或条形码。") : result);
            dialog.resize(600, 360);
            dialog.exec();
        });
    }
    more->setMenu(menu);
    bar->addWidget(more);
    for (auto *button : editorToolbar_->findChildren<QToolButton *>())
        button->setFocusPolicy(Qt::NoFocus);

    styleBar_ = new QWidget;
    auto *styleLayout = new QVBoxLayout(styleBar_);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    auto *style = new QHBoxLayout;
    styleLayout->addLayout(style);
    style->setContentsMargins(4, 0, 4, 0);
    color_ = new QPushButton;
    color_->setObjectName("annotationColor");
    color_->installEventFilter(this);
    color_->setToolTip(QStringLiteral("标注颜色与透明度"));
    style->addWidget(color_);
    connect(color_, &QPushButton::clicked, this, [this] {
        if (!canvas_)
            return;
        auto next = canvas_->style();
        const QColor color = QColorDialog::getColor(next.color, this, QStringLiteral("标注颜色"),
                                                     QColorDialog::ShowAlphaChannel);
        if (color.isValid()) {
            next.color = color;
            canvas_->setStyle(next);
            syncStyle();
        }
    });
    style->addWidget(new QLabel(QStringLiteral("粗细")));
    strokeWidth_ = new QDoubleSpinBox;
    strokeWidth_->setRange(1, 100);
    strokeWidth_->setFixedWidth(uiPixels(74));
    style->addWidget(strokeWidth_);
    style->addWidget(new QLabel(QStringLiteral("字号")));
    fontSize_ = new QSpinBox;
    fontSize_->setRange(6, 300);
    fontSize_->setFixedWidth(uiPixels(74));
    style->addWidget(fontSize_);
    auto *options = new QPushButton(QStringLiteral("样式…"));
    style->addWidget(options);
    style->addStretch();
    connect(options, &QPushButton::clicked, this, &CaptureOverlay::editStyle);
    connect(strokeWidth_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double width) {
        if (canvas_) {
            auto next = canvas_->style();
            next.width = width;
            canvas_->setStyle(next);
        }
    });
    connect(fontSize_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int size) {
        if (canvas_) {
            auto next = canvas_->style();
            next.fontSize = size;
            canvas_->setStyle(next);
        }
    });
    layout->addWidget(styleBar_);
    styleBar_->hide();
    editorToolbar_->hide();
}

void CaptureOverlay::selectTool(QAction *action) {
    if (!canvas_ || outputPending_)
        return;
    canvas_->finishDrawing();
    selecting_ = action == adjustSelection_;
    canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, selecting_);
    if (selecting_)
        setFocus();
    else
        canvas_->setTool(Tool(action->data().toInt()));
    canvas_->setVisible(!selecting_ || !canvas_->document().annotations.isEmpty());
    styleBar_->setVisible(!selecting_);
    syncStyle();
    placeToolbar();
    update();
}

void CaptureOverlay::syncStyle() {
    undo_->setEnabled(canvas_ && canvas_->canUndo());
    redo_->setEnabled(canvas_ && canvas_->canRedo());
    if (!canvas_)
        return;
    const auto style = canvas_->style();
    const QSignalBlocker widthBlock(strokeWidth_), fontBlock(fontSize_);
    strokeWidth_->setValue(style.width);
    fontSize_->setValue(qRound(style.fontSize));
    color_->setText(style.color.name().toUpper());
    color_->setStyleSheet(QString("background:%1;color:%2").arg(
        style.color.name(), style.color.lightness() > 140 ? "#111827" : "white"));
    if (!selecting_)
        for (auto *action : tools_->actions())
            if (action->data().toInt() == int(canvas_->tool()))
                action->setChecked(true);
}

void CaptureOverlay::editStyle() {
    if (!canvas_)
        return;
    auto next = canvas_->style();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("标注样式"));
    QFormLayout form(&dialog);
    auto colorButton = [&](const QString &label, QColor &value) {
        auto *button = new QPushButton(value.name(QColor::HexArgb), &dialog);
        form.addRow(label, button);
        connect(button, &QPushButton::clicked, &dialog, [button, &value, &dialog] {
            const QColor color = QColorDialog::getColor(value, &dialog, QStringLiteral("颜色与透明度"),
                                                         QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                value = color;
                button->setText(color.name(QColor::HexArgb));
            }
        });
    };
    colorButton(QStringLiteral("颜色"), next.color);
    colorButton(QStringLiteral("填充"), next.fill);
    colorButton(QStringLiteral("文字描边"), next.outline);
    QSpinBox outlineWidth;
    outlineWidth.setRange(0, 20);
    outlineWidth.setValue(qRound(next.outlineWidth));
    form.addRow(QStringLiteral("描边宽度"), &outlineWidth);
    QDoubleSpinBox rotation;
    rotation.setRange(-360, 360);
    rotation.setValue(next.rotation);
    rotation.setEnabled(!isLineTool(canvas_->selectedIndex() >= 0 ? next.tool : canvas_->tool()));
    form.addRow(QStringLiteral("旋转"), &rotation);
    QComboBox line, shape, arrow;
    line.addItems({QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线")});
    line.setCurrentIndex(next.lineStyle - 1);
    form.addRow(QStringLiteral("线型"), &line);
    shape.addItems({QStringLiteral("矩形区域"), QStringLiteral("椭圆区域"), QStringLiteral("圆形区域"),
                    QStringLiteral("画笔轨迹")});
    shape.setCurrentIndex(next.shape);
    form.addRow(QStringLiteral("效果区域"), &shape);
    arrow.addItems({QStringLiteral("单向开放"), QStringLiteral("双向"), QStringLiteral("反向"),
                    QStringLiteral("实心箭头")});
    arrow.setCurrentIndex(next.arrowStyle);
    form.addRow(QStringLiteral("箭头"), &arrow);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        next.outlineWidth = outlineWidth.value();
        next.rotation = rotation.value();
        next.lineStyle = line.currentIndex() + 1;
        next.shape = shape.currentIndex();
        next.arrowStyle = arrow.currentIndex();
        canvas_->setStyle(next);
        syncStyle();
    }
}

QRect CaptureOverlay::selectedGlobal() const {
    if (historyIndex_ >= 0 && canvas_)
        return canvas_->document().sourceRect;
    return selection_.translated(desktop_.geometry.topLeft());
}
QImage CaptureOverlay::backgroundImage() const {
    QImage image = desktop_.image;
    if (cursor_ && !desktop_.cursorImage.isNull()) {
        QPainter painter(&image);
        painter.drawImage(desktop_.cursorPosition - desktop_.geometry.topLeft() - desktop_.cursorHotspot,
                          desktop_.cursorImage);
    }
    return image;
}
Document CaptureOverlay::selectionBackground() const {
    if (historyIndex_ >= 0 && canvas_)
        return canvas_->document();
    Document document;
    if (!selection_.isEmpty())
        document.image = backgroundImage().copy(selection_.intersected(desktop_.image.rect()));
    document.cornerRadius = radius_;
    document.sourceRect = selectedGlobal();
    if (elements_)
        for (const auto &element : detectedElements_) {
            const QRect clipped = element.intersected(selectedGlobal());
            if (!clipped.isEmpty())
                document.detectedElements.append(clipped.translated(-selectedGlobal().topLeft()));
        }
    return document;
}
Document CaptureOverlay::selectionDocument() const {
    return canvas_ ? canvas_->document() : selectionBackground();
}
void CaptureOverlay::syncSelection() {
    if (selection_.isEmpty()) {
        toolbar_->show();
        editorToolbar_->hide();
        placeToolbar();
        update();
        return;
    }
    if (!canvas_) {
        canvas_ = new AnnotationCanvas(selectionBackground(), settings_, this);
        canvas_->setObjectName("captureCanvas");
        canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, selecting_);
        canvas_->installEventFilter(this);
        connect(canvas_, &AnnotationCanvas::changed, this, &CaptureOverlay::syncStyle);
        connect(canvas_, &AnnotationCanvas::selectionChanged, this, &CaptureOverlay::syncStyle);
        connect(canvas_, &AnnotationCanvas::imageCopyRequested, this, [this] { requestOutput("copy"); });
        connect(canvas_, &AnnotationCanvas::toolChanged, this, [this](Tool tool) {
            auto *action = findChild<QAction *>("captureTool" + toolKey(tool));
            if (action) {
                action->setChecked(true);
                for (auto *button : annotationTools_->findChildren<QToolButton *>())
                    if (button->menu() && button->menu()->actions().contains(action))
                        button->setDefaultAction(action);
            }
        });
        presets_ = new QWidget(styleBar_);
        auto *row = new QHBoxLayout(presets_);
        row->setContentsMargins(4, 0, 4, 0);
        row->addWidget(new BrushTips(canvas_, settings_));
        auto *palette = new PaletteWidget(canvas_, settings_);
        palette->setFixedHeight(uiPixels(34));
        palette->setWrapping(false);
        row->addWidget(palette, 1);
        auto *editPalette = new QPushButton(QStringLiteral("色板…"));
        row->addWidget(editPalette);
        connect(editPalette, &QPushButton::clicked, palette, &PaletteWidget::edit);
        styleBar_->layout()->addWidget(presets_);
    } else if (historyIndex_ < 0)
        canvas_->setCaptureBackground(selectionBackground());
    const double zoom = historyIndex_ < 0 ? 1 :
        qMin(double(selection_.width()) / canvas_->document().image.width(),
             double(selection_.height()) / canvas_->document().image.height());
    canvas_->setZoom(zoom);
    canvas_->move(selection_.topLeft());
    if (radius_ > 0) {
        QPainterPath shape;
        shape.addRoundedRect(canvas_->rect(), radius_ * zoom, radius_ * zoom);
        canvas_->setMask(QRegion(shape.toFillPolygon().toPolygon()));
    } else
        canvas_->clearMask();
    canvas_->setVisible(!selecting_ || !canvas_->document().annotations.isEmpty());
    toolbar_->hide();
    editorToolbar_->show();
    editorToolbar_->raise();
    syncStyle();
    placeToolbar();
    update();
}
void CaptureOverlay::clearSelection() {
    delete presets_;
    presets_ = nullptr;
    delete canvas_;
    canvas_ = nullptr;
    selection_ = {};
    selecting_ = true;
    adjustSelection_->setChecked(true);
    styleBar_->hide();
    setFocus();
    syncSelection();
}
void CaptureOverlay::setHistory(int count, std::function<Document(int)> loader) {
    historyCount_ = count;
    historyLoader_ = std::move(loader);
}
void CaptureOverlay::replayHistory(int direction) {
    if (!historyLoader_ || pressed_ || outputPending_ || refreshing_)
        return;
    int next = historyIndex_ + direction;
    if (next < -1 || next >= historyCount_)
        return;
    CaptureState state;
    while (next >= 0 && next < historyCount_) {
        if (historyStates_.contains(next)) {
            state = historyStates_.value(next);
            state.document.image = historyLoader_(next).image;
        } else {
            state.document = historyLoader_(next);
            state.selecting = false;
            QRect screen = desktop_.screens.value(0, desktop_.geometry).translated(-desktop_.geometry.topLeft());
            for (const auto &candidate : desktop_.screens)
                if (candidate.contains(QCursor::pos()))
                    screen = candidate.translated(-desktop_.geometry.topLeft());
            const QSize size = state.document.image.size().scaled(
                QSize(qMax(1, screen.width() - 32), qMax(1, screen.height() - uiPixels(170))), Qt::KeepAspectRatio);
            const QSize preview = size.boundedTo(state.document.image.size());
            state.selection = QRect(screen.center() - QPoint(preview.width() / 2, preview.height() / 2), preview);
        }
        if (!state.document.image.isNull())
            break;
        next += direction;
    }
    if (next >= historyCount_)
        return;
    if (canvas_)
        canvas_->finishDrawing();
    historyStates_[historyIndex_] = {selectionDocument(), selection_, selecting_,
                                     canvas_ ? canvas_->tool() : Tool::Select, property("recordId").toString()};
    if (historyIndex_ >= 0)
        historyStates_[historyIndex_].document.image = {}; // Retain edits while releasing decoded history pixels.
    if (next < 0)
        state = historyStates_.value(-1);
    // Loading is lazy. Each visited image keeps its in-session edits, including the live capture.
    clearSelection();
    historyIndex_ = next;
    selection_ = state.selection;
    radius_ = state.document.cornerRadius;
    selecting_ = state.selecting;
    adjustSelection_->setVisible(next < 0);
    refreshTimer_.stop();
    setProperty("recordId", state.recordId);
    if (!selection_.isEmpty()) {
        // Create the shared controls with a valid background, then restore the original image and objects.
        const int index = historyIndex_;
        historyIndex_ = -1;
        syncSelection();
        historyIndex_ = index;
        canvas_->setDocument(state.document);
        canvas_->setTool(state.tool);
        canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, selecting_);
        if (selecting_)
            adjustSelection_->setChecked(true);
        styleBar_->setVisible(!selecting_);
    }
    syncSelection();
    if (selecting_)
        setFocus();
}
void CaptureOverlay::requestOutput(const QString &action) {
    if (done_ || outputPending_ || refreshing_)
        return;
    if (selection_.isEmpty())
        selection_ = hover_;
    if (selection_.width() < 1 || selection_.height() < 1)
        return;
    syncSelection();
    canvas_->finishDrawing();
    outputPending_ = true;
    resumeRefresh_ = refreshTimer_.isActive();
    refreshTimer_.stop();
    emit outputRequested(selectionDocument(), selectedGlobal(), action,
                         QApplication::keyboardModifiers().testFlag(Qt::AltModifier));
}
void CaptureOverlay::completeOutput(bool successful, bool closeCapture) {
    outputPending_ = false;
    if (successful && closeCapture) {
        done_ = true;
        refreshTimer_.stop();
        detectionTimer_.stop();
        if (historyIndex_ >= 0 && historyStates_.value(-1).recordId.isEmpty())
            emit draftAbandoned(historyStates_.value(-1).document);
        close();
    } else if (resumeRefresh_ && !findChild<QObject *>("EasySnipNativeShare"))
        refreshTimer_.start(settings_.value("capture/refreshMs", 1000).toInt());
}
void CaptureOverlay::cancel() {
    if (done_ || outputPending_)
        return;
    done_ = true;
    refreshTimer_.stop();
    detectionTimer_.stop();
    hide();
    if (historyIndex_ >= 0)
        setProperty("recordId", historyStates_.value(-1).recordId);
    emit canceled(historyIndex_ >= 0 ? historyStates_.value(-1).document : selectionDocument());
    close();
}
void CaptureOverlay::placeToolbar() {
    QRect screen = rect();
    int largestArea = 0;
    for (const auto &item : desktop_.screens)
        if (selection_.isEmpty()) {
            if (item.contains(QCursor::pos()))
                screen = item.translated(-desktop_.geometry.topLeft());
        } else {
            const QRect local = item.translated(-desktop_.geometry.topLeft());
            const QRect overlap = local.intersected(selection_);
            const int area = overlap.width() * overlap.height();
            if (area > largestArea) {
                largestArea = area;
                screen = local;
            }
        }
    if (selection_.isEmpty()) {
        toolbar_->move(qMax(screen.left(), screen.center().x() - toolbar_->width() / 2),
                       screen.bottom() - toolbar_->height() - 28);
        return;
    }
    const int gap = uiPixels(8);
    const int horizontalWidth = annotationTools_->sizeHint().width() + outputTools_->sizeHint().width() + 12;
    toolbarActions_->setDirection(horizontalWidth > screen.width() ? QBoxLayout::TopToBottom
                                                                  : QBoxLayout::LeftToRight);
    editorToolbar_->layout()->activate();
    const QSize hint = editorToolbar_->sizeHint();
    editorToolbar_->resize(qMin(hint.width(), screen.width()), hint.height());
    const int x = qBound(screen.left(), selection_.right() - editorToolbar_->width() + 1,
                         qMax(screen.left(), screen.right() - editorToolbar_->width() + 1));
    int y = selection_.bottom() + gap;
    if (y + editorToolbar_->height() > screen.bottom() + 1) {
        y = selection_.top() - gap - editorToolbar_->height();
        if (y < screen.top())
            y = screen.bottom() - selection_.bottom() >= selection_.top() - screen.top()
                    ? screen.bottom() - editorToolbar_->height() + 1 : screen.top();
    }
    y = qBound(screen.top(), y, qMax(screen.top(), screen.bottom() - editorToolbar_->height() + 1));
    editorToolbar_->move(x, y);
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.drawImage(0, 0, backgroundImage());
    const QRect target = selection_.isEmpty() ? animatedHover_ : selection_;
    QPainterPath mask;
    mask.addRect(rect());
    QPainterPath hole;
    if (!target.isEmpty())
        hole.addRoundedRect(target, radius_, radius_);
    painter.fillPath(mask.subtracted(hole), QColor(4, 10, 20, 140));
    painter.setPen(QPen(QColor("#54d6b0"), 1));
    painter.setBrush(Qt::NoBrush);
    if (!target.isEmpty()) {
        painter.drawRoundedRect(target.adjusted(-1, -1, 1, 1), radius_, radius_);
        const int threshold = settings_.value("capture/anchorThreshold", 20).toInt();
        if (selecting_ && !selection_.isEmpty() && target.width() > threshold && target.height() > threshold) {
            painter.setBrush(QColor("#54d6b0"));
            for (const QPoint &point :
                 {target.topLeft(), target.topRight(), target.bottomLeft(), target.bottomRight(),
                  QPoint(target.center().x(), target.top()), QPoint(target.center().x(), target.bottom()),
                  QPoint(target.left(), target.center().y()), QPoint(target.right(), target.center().y())})
                painter.drawRect(QRect(point - QPoint(3, 3), QSize(6, 6)));
        }
        const QString text = QStringLiteral("%1 × %2 px   (%3, %4)%5")
                                 .arg(target.width())
                                 .arg(target.height())
                                 .arg(target.x() + desktop_.geometry.x())
                                 .arg(target.y() + desktop_.geometry.y())
                                 .arg(typed_.isEmpty() ? "" : "   " + typed_);
        const QRect label(target.left(), qMax(5, target.top() - 33),
                          qMax(240, painter.fontMetrics().horizontalAdvance(text) + 20), 27);
        painter.fillRect(label, QColor("#172131"));
        painter.setPen(Qt::white);
        painter.drawText(label.adjusted(8, 0, -6, 0), Qt::AlignVCenter, text);
    }
    QRect currentScreen = rect();
    for (const auto &screen : desktop_.screens)
        if (screen.contains(QCursor::pos()))
            currentScreen = screen.translated(-desktop_.geometry.topLeft());
    const QRect help(currentScreen.left() + 20, currentScreen.top() + 20,
                     qMin(1000, currentScreen.width() - 40), 36);
    painter.fillRect(help, QColor(16, 25, 39, 225));
    painter.setPen(Qt::white);
    const QString helpText = selecting_
        ? QStringLiteral("拖动选区 · Ctrl+D 尺寸 · WASD 移动鼠标 · 方向键微调 · Enter 复制 · , . 历史 · Esc 取消")
        : QStringLiteral("Shift 约束 · 1/2 粗细 · 右键结束编辑 / 匹配元素 · Enter 复制 · , . 历史 · Esc 取消");
    painter.drawText(help.adjusted(12, 0, -10, 0), Qt::AlignVCenter,
                     helpText + (refreshTimer_.isActive() ? QStringLiteral("  [连续刷新]") : ""));
    if (historyIndex_ >= 0) {
        painter.drawText(help.adjusted(12, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("历史 %1 / %2").arg(historyIndex_ + 1).arg(historyCount_));
    }
    if (magnifierVisible()) {
        const int zoom = settings_.value("magnifier/zoom", 8).toInt();
        const int side = 17;
        QPoint position = mouse_ + QPoint(28, 32);
        const QSize box(side * zoom, side * zoom + 50);
        position.setX(qBound(currentScreen.left(), position.x(),
                             qMax(currentScreen.left(), currentScreen.right() - box.width())));
        position.setY(qBound(currentScreen.top(), position.y(),
                             qMax(currentScreen.top(), currentScreen.bottom() - box.height())));
        const QRect lens(position, QSize(side * zoom, side * zoom));
        const QImage patch =
            desktop_.image.copy(QRect(mouse_ - QPoint(side / 2, side / 2), QSize(side, side)));
        painter.fillRect(QRect(position, box), QColor("#172131"));
        painter.save();
        if (settings_.value("magnifier/style", "square").toString() == "round") {
            QPainterPath circle;
            circle.addEllipse(lens);
            painter.setClipPath(circle);
        }
        painter.drawImage(lens, patch);
        if (settings_.value("magnifier/grid", true).toBool()) {
            painter.setPen(QColor(0, 0, 0, 90));
            for (int i = 1; i < side; ++i) {
                painter.drawLine(lens.left() + i * zoom, lens.top(), lens.left() + i * zoom, lens.bottom());
                painter.drawLine(lens.left(), lens.top() + i * zoom, lens.right(), lens.top() + i * zoom);
            }
        }
        painter.setPen(QPen(QColor("#ff536b"), 2));
        painter.drawRect(QRect(position + QPoint(side / 2 * zoom, side / 2 * zoom), QSize(zoom, zoom)));
        painter.restore();
        painter.setPen(Qt::white);
        const QString value =
            colorText(desktop_.image.pixelColor(mouse_), settings_.value("color/format", "HEX").toString(),
                      settings_.value("color/uppercase", true).toBool());
        painter.drawText(QRect(position + QPoint(4, lens.height()), QSize(lens.width() - 8, 48)),
                         Qt::AlignCenter, value + QStringLiteral("\nC 复制 · Shift 格式 · Alt 开关"));
    }
}
int CaptureOverlay::handleAt(const QPoint &point) const {
    if (selection_.isEmpty())
        return -1;
    const QVector<QPoint> handles{
        selection_.topLeft(),     QPoint(selection_.center().x(), selection_.top()),
        selection_.topRight(),    QPoint(selection_.right(), selection_.center().y()),
        selection_.bottomRight(), QPoint(selection_.center().x(), selection_.bottom()),
        selection_.bottomLeft(),  QPoint(selection_.left(), selection_.center().y())};
    for (int i = 0; i < handles.size(); ++i)
        if ((handles[i] - point).manhattanLength() < 12)
            return i;
    return -1;
}
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->modifiers().testFlag(Qt::AltModifier))
        altUsed_ = true;
    if (outputPending_ || refreshing_)
        return;
    if (event->button() == Qt::RightButton) {
        if (!selecting_)
            adjustSelection_->trigger();
        else if (!selection_.isEmpty())
            clearSelection();
        else
            cancel();
        return;
    }
    if (event->button() != Qt::LeftButton || !selecting_)
        return;
    pressed_ = true;
    mouse_ = event->pos();
    anchor_ = previous_ = event->pos();
    handle_ = handleAt(event->pos());
    moving_ = handle_ < 0 && selection_.contains(event->pos());
    if (!moving_ && handle_ < 0)
        clearSelection();
    if (canvas_)
        canvas_->hide();
    editorToolbar_->hide();
}
void CaptureOverlay::constrainSelection() {
    selection_ = selection_.normalized().intersected(rect());
    if (ratio_ > 0 && !selection_.isEmpty()) {
        int width = selection_.width();
        int height = qMax(1, qRound(width / ratio_));
        if (height > selection_.height()) {
            height = selection_.height();
            width = qMax(1, qRound(height * ratio_));
        }
        selection_.setSize(QSize(width, height));
    }
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *event) {
    mouse_ = event->pos();
    if (pressed_) {
        const QPoint delta = event->pos() - previous_;
        if (space_ || moving_) {
            selection_.translate(delta);
            anchor_ += delta;
            selection_.moveLeft(qBound(0, selection_.left(), qMax(0, width() - selection_.width())));
            selection_.moveTop(qBound(0, selection_.top(), qMax(0, height() - selection_.height())));
        } else if (handle_ >= 0) {
            QRect next = selection_;
            if (handle_ == 0 || handle_ == 6 || handle_ == 7)
                next.setLeft(event->x());
            if (handle_ == 0 || handle_ == 1 || handle_ == 2)
                next.setTop(event->y());
            if (handle_ == 2 || handle_ == 3 || handle_ == 4)
                next.setRight(event->x());
            if (handle_ == 4 || handle_ == 5 || handle_ == 6)
                next.setBottom(event->y());
            selection_ = next.normalized();
            if (ratio_ > 0)
                selection_.setHeight(qMax(1, qRound(selection_.width() / ratio_)));
            constrainSelection();
        } else {
            QPoint end = event->pos();
            const double ratio = event->modifiers().testFlag(Qt::ShiftModifier) ? 1.0 : ratio_;
            if (ratio > 0)
                end.setY(anchor_.y() +
                         (end.y() >= anchor_.y() ? 1 : -1) * qRound(qAbs(end.x() - anchor_.x()) / ratio));
            selection_ = QRect(anchor_, end).normalized();
            constrainSelection();
        }
        previous_ = event->pos();
    } else {
        setCursor(selection_.contains(mouse_) ? Qt::SizeAllCursor : Qt::CrossCursor);
        if (selection_.isEmpty())
            placeToolbar();
    }
    update();
}
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !pressed_)
        return;
    pressed_ = false;
    if (!moving_ && handle_ < 0 && (event->pos() - anchor_).manhattanLength() < 4)
        selection_ = hover_;
    constrainSelection();
    syncSelection();
}
void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && selecting_ && selection_.contains(event->pos()))
        requestOutput("copy");
}
bool CaptureOverlay::magnifierVisible() const {
    const bool automatic = selection_.isEmpty() || pressed_ || handleAt(mouse_) >= 0;
    const bool enabled = magnifierOverride_ < 0 ? automatic && settings_.value("magnifier/enabled", true).toBool()
                                               : magnifierOverride_ != 0;
    return enabled && selecting_ && historyIndex_ < 0 && desktop_.image.rect().contains(mouse_) &&
           !(toolbar_->isVisible() && toolbar_->geometry().contains(mouse_)) &&
           !(editorToolbar_->isVisible() && editorToolbar_->geometry().contains(mouse_));
}
void CaptureOverlay::moveCursor(const QPoint &step, Qt::KeyboardModifiers modifiers) {
    const QPoint next(qBound(0, mouse_.x() + step.x(), width() - 1),
                      qBound(0, mouse_.y() + step.y(), height() - 1));
    QCursor::setPos(mapToGlobal(next));
    QMouseEvent move(QEvent::MouseMove, QPointF(next), Qt::NoButton,
                     pressed_ ? Qt::LeftButton : Qt::NoButton, modifiers);
    mouseMoveEvent(&move);
}
void CaptureOverlay::keyPressEvent(QKeyEvent *event) {
    if (outputPending_ || refreshing_)
        return;
    const int key = event->key();
    const auto modifiers = event->modifiers();
    if (key == Qt::Key_Alt) {
        if (!event->isAutoRepeat())
            altUsed_ = false;
        return;
    }
    if (modifiers.testFlag(Qt::AltModifier))
        altUsed_ = true;
    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if ((key == Qt::Key_Comma || key == Qt::Key_Period) && modifiers == Qt::NoModifier) {
        replayHistory(key == Qt::Key_Comma ? 1 : -1);
        return;
    }
    if (key == Qt::Key_D && modifiers == Qt::ControlModifier) {
        setDimensions();
        return;
    }
    if (canvas_ && event->matches(QKeySequence::Undo)) {
        canvas_->undo();
        return;
    }
    if (canvas_ && event->matches(QKeySequence::Redo)) {
        canvas_->redo();
        return;
    }
    if (!selecting_) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (key == Qt::Key_Space) {
        space_ = true;
        return;
    }
    if (key == Qt::Key_Shift && !event->isAutoRepeat() && magnifierVisible()) {
        settings_.set("color/format", settings_.value("color/format", "HEX") == "RGB" ? "HEX" : "RGB");
        update();
        return;
    }
    if (key == Qt::Key_C && modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && canvas_) {
        canvas_->copyObject();
        return;
    }
    if (key == Qt::Key_V && modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && canvas_) {
        findChild<QAction *>("captureTool0")->trigger();
        canvas_->pasteObject();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!typed_.isEmpty()) {
            const auto dimensions = typed_.split('x');
            if (dimensions.size() == 2) {
                if (selection_.isEmpty())
                    selection_.moveTopLeft(mouse_);
                selection_.setSize(QSize(qBound(1, dimensions[0].toInt(), width()),
                                         qBound(1, dimensions[1].toInt(), height())));
                constrainSelection();
            }
            typed_.clear();
            syncSelection();
        } else
            requestOutput("copy");
        return;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9 && modifiers == Qt::NoModifier && typed_.size() < 13) {
        typed_ += QChar('0' + key - Qt::Key_0);
        update();
        return;
    }
    if (key == Qt::Key_X && !typed_.contains('x')) {
        typed_ += 'x';
        update();
        return;
    }
    if (key == Qt::Key_Backspace) {
        typed_.chop(1);
        update();
        return;
    }
    const bool wasd = settings_.value("input/wasd", true).toBool() && modifiers == Qt::NoModifier;
    QPoint cursorStep;
    if (wasd && key == Qt::Key_W) cursorStep.setY(-1);
    if (wasd && key == Qt::Key_A) cursorStep.setX(-1);
    if (wasd && key == Qt::Key_S) cursorStep.setY(1);
    if (wasd && key == Qt::Key_D) cursorStep.setX(1);
    if (!cursorStep.isNull()) {
        moveCursor(cursorStep, modifiers);
        return;
    }
    if (key == Qt::Key_F && modifiers == Qt::NoModifier)
        selection_ = rect();
    if (key == Qt::Key_S && (!wasd || modifiers == Qt::ShiftModifier))
        for (const auto &screen : desktop_.screens)
            if (screen.contains(QCursor::pos()))
                selection_ = screen.translated(-desktop_.geometry.topLeft());
    if (key == Qt::Key_Tab) {
        elements_ = !elements_;
        settings_.set("capture/elements", elements_);
        findChild<QPushButton *>("elementToggle")->setChecked(elements_);
        invalidateDetection();
        updateDetection();
    }
    if (key == Qt::Key_I && modifiers == Qt::NoModifier) {
        cursor_ = !cursor_;
        findChild<QPushButton *>("pointerToggle")->setChecked(cursor_);
    }
    if (key == Qt::Key_D && !wasd)
        setDimensions();
    if (key == Qt::Key_R) {
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            if (refreshTimer_.isActive())
                refreshTimer_.stop();
            else
                refreshTimer_.start(settings_.value("capture/refreshMs", 1000).toInt());
        } else
            refresh();
    }
    if (key == Qt::Key_V && modifiers == Qt::NoModifier) {
        const QStringList formats{"HEX", "RGB", "HSV"};
        settings_.set("color/format",
                      formats[(formats.indexOf(settings_.value("color/format", "HEX").toString()) + 1) % 3]);
    }
    if ((key == Qt::Key_P || (key == Qt::Key_C && magnifierVisible())) &&
        modifiers == Qt::NoModifier && desktop_.image.rect().contains(mouse_))
        QApplication::clipboard()->setText(colorText(desktop_.image.pixelColor(mouse_),
                                                     settings_.value("color/format", "HEX").toString(),
                                                     settings_.value("color/uppercase", true).toBool()));
    QPoint step;
    if (key == Qt::Key_Left) step.setX(-1);
    if (key == Qt::Key_Right) step.setX(1);
    if (key == Qt::Key_Up) step.setY(-1);
    if (key == Qt::Key_Down) step.setY(1);
    if (!step.isNull()) {
        if (selection_.isEmpty() || modifiers.testFlag(Qt::AltModifier)) {
            moveCursor(step, modifiers);
            return;
        }
        if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier)) {
            const int direction = modifiers.testFlag(Qt::ControlModifier) ? 1 : -1;
            if (step.x() < 0)
                selection_.setLeft(qBound(0, selection_.left() - direction, selection_.right()));
            if (step.x() > 0)
                selection_.setRight(qBound(selection_.left(), selection_.right() + direction, width() - 1));
            if (step.y() < 0)
                selection_.setTop(qBound(0, selection_.top() - direction, selection_.bottom()));
            if (step.y() > 0)
                selection_.setBottom(qBound(selection_.top(), selection_.bottom() + direction, height() - 1));
        } else {
            selection_.translate(step);
            selection_.moveLeft(qBound(0, selection_.left(), qMax(0, width() - selection_.width())));
            selection_.moveTop(qBound(0, selection_.top(), qMax(0, height() - selection_.height())));
        }
        constrainSelection();
    }
    syncSelection();
}
void CaptureOverlay::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space)
        space_ = false;
    if (event->key() == Qt::Key_Alt && !event->isAutoRepeat() && !altUsed_) {
        const bool enabled = magnifierOverride_ < 0 ? settings_.value("magnifier/enabled", true).toBool()
                                                   : magnifierOverride_ != 0;
        magnifierOverride_ = !enabled;
        update();
    }
}
void CaptureOverlay::wheelEvent(QWheelEvent *event) {
    if (elements_ && selection_.isEmpty() && !elementRects_.isEmpty()) {
        elementIndex_ =
            qBound(0, elementIndex_ + (event->angleDelta().y() > 0 ? 1 : -1), elementRects_.size() - 1);
        setHover(elementRects_[elementIndex_].translated(-desktop_.geometry.topLeft()).intersected(rect()));
    } else if (event->modifiers().testFlag(Qt::ControlModifier))
        settings_.set(
            "magnifier/zoom",
            qBound(2, settings_.value("magnifier/zoom", 8).toInt() + (event->angleDelta().y() > 0 ? 1 : -1),
                   24));
    update();
}
bool CaptureOverlay::event(QEvent *event) {
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::WindowDeactivate && !refreshing_ && !done_ && !outputPending_ &&
        !QApplication::activeModalWidget() && !QApplication::activePopupWidget() &&
        settings_.value("capture/cancelOnDeactivate", false).toBool())
        QTimer::singleShot(0, this, [this] {
            if (!refreshing_ && !outputPending_ && !isActiveWindow() &&
                !QApplication::activeModalWidget() && !QApplication::activePopupWidget() &&
                !findChild<QObject *>("EasySnipNativeShare"))
                cancel();
        });
    return QWidget::event(event);
}
bool CaptureOverlay::eventFilter(QObject *object, QEvent *event) {
    if (object == color_ && event->type() == QEvent::Wheel && canvas_) {
        const int delta = static_cast<QWheelEvent *>(event)->angleDelta().y();
        if (delta) {
            auto next = canvas_->style();
            next.color.setAlpha(qBound(0, next.color.alpha() + (delta > 0 ? 8 : -8), 255));
            canvas_->setStyle(next);
        }
        return true;
    }
    if (object == canvas_) {
        if (event->type() == QEvent::Wheel &&
            static_cast<QWheelEvent *>(event)->modifiers().testFlag(Qt::ControlModifier))
            return true;
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Comma || key->key() == Qt::Key_Period) {
                keyPressEvent(key);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                !canvas_->drawing()) {
                requestOutput("copy");
                return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}
void CaptureOverlay::closeEvent(QCloseEvent *event) {
    if (outputPending_) {
        event->ignore();
        return;
    }
    refreshTimer_.stop();
    detectionTimer_.stop();
    if (!done_) {
        done_ = true;
        if (historyIndex_ >= 0)
            setProperty("recordId", historyStates_.value(-1).recordId);
        emit canceled(historyIndex_ >= 0 ? historyStates_.value(-1).document : selectionDocument());
    }
    QWidget::closeEvent(event);
}
void CaptureOverlay::updateDetection() {
    if (done_ || refreshing_ || historyIndex_ >= 0 || pressed_ ||
        (!selection_.isEmpty() && (!elements_ || selecting_)))
        return;
    const QPoint point = QCursor::pos();
    if ((toolbar_->isVisible() && toolbar_->geometry().contains(mapFromGlobal(point))) ||
        (editorToolbar_->isVisible() && editorToolbar_->geometry().contains(mapFromGlobal(point))))
        return;
    const QStringList excluded =
        settings_.value("capture/excluded").toString().split(';', Qt::SkipEmptyParts);
    if (elements_) {
        const auto nativePrograms =
            settings_.value("capture/nativeElementsFor").toString().split(';', Qt::SkipEmptyParts);
        const auto cached = Native::cachedElementsAt(point, desktop_.windows, excluded, nativePrograms);
        if (!cached.isEmpty()) {
            applyDetection(cached);
            return;
        }
        if (elementRects_.isEmpty() || !elementRects_.first().contains(point)) {
            elementRects_.clear();
            if (selection_.isEmpty())
                setHover(Native::windowAt(point, excluded, desktop_.windows)
                             .translated(-desktop_.geometry.topLeft()).intersected(rect()));
        }
        if (!elementsWatcher_.isRunning() &&
            (elementRects_.isEmpty() || point != elementPoint_)) {
            elementPoint_ = point;
            queryGeneration_ = detectionGeneration_;
            const auto windows = desktop_.windows;
            elementsWatcher_.setFuture(QtConcurrent::run([point, excluded, nativePrograms, windows] {
                return Native::elementsAt(point, excluded, nativePrograms, windows);
            }));
        }
    } else
        setHover(
            Native::windowAt(point, excluded, desktop_.windows).translated(-desktop_.geometry.topLeft()).intersected(rect()));
    update();
}
void CaptureOverlay::invalidateDetection() {
    ++detectionGeneration_;
    elementRects_.clear();
    elementIndex_ = 0;
}
void CaptureOverlay::applyDetection(const QVector<QRect> &rectangles) {
    if (elementRects_ != rectangles) {
        elementRects_ = rectangles;
        elementIndex_ = 0;
    }
    for (const auto &element : elementRects_)
        if (!detectedElements_.contains(element))
            detectedElements_.append(element);
    if (selection_.isEmpty())
        setHover(elementRects_.isEmpty() ? QRect() :
                 elementRects_[elementIndex_].translated(-desktop_.geometry.topLeft()).intersected(rect()));
    if (canvas_)
        canvas_->setDetectedElements(selectionBackground().detectedElements);
    update();
}
void CaptureOverlay::setHover(const QRect &rectangle) {
    if (rectangle == hover_)
        return;
    hover_ = rectangle;
    detectionAnimation_.stop();
    if (settings_.value("capture/animateDetection", true).toBool() && !animatedHover_.isEmpty() &&
        !hover_.isEmpty()) {
        detectionAnimation_.setStartValue(animatedHover_);
        detectionAnimation_.setEndValue(hover_);
        detectionAnimation_.start();
    } else
        animatedHover_ = hover_;
    update();
}
void CaptureOverlay::refresh() {
    if (historyIndex_ >= 0 || refreshing_ || done_ || outputPending_ || pressed_ || QApplication::activeModalWidget() ||
        QApplication::activePopupWidget())
        return;
    refreshing_ = true;
    invalidateDetection();
    hide();
    QTimer::singleShot(90, this, [this] {
        if (done_)
            return;
        auto *watcher = new QFutureWatcher<Native::Desktop>(this);
        connect(watcher, &QFutureWatcher<Native::Desktop>::finished, this, [this, watcher] {
        auto fresh = watcher->result();
        watcher->deleteLater();
        if (done_)
            return;
        if (!fresh.image.isNull()) {
            desktop_ = std::move(fresh);
            detectedElements_.clear();
            for (const auto &window : desktop_.windows) {
                detectedElements_.append(window.geometry);
                detectedElements_ += window.elements;
            }
            setGeometry(desktop_.geometry);
            constrainSelection();
        }
        show();
        raise();
        activateWindow();
        refreshing_ = false;
        syncSelection();
        });
        watcher->setFuture(QtConcurrent::run([] { return Native::captureDesktop(true); }));
    });
}
void CaptureOverlay::setDimensions() {
    if (historyIndex_ >= 0)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("精确选区"));
    QFormLayout form(&dialog);
    QSpinBox x, y, width, height, radius;
    x.setRange(desktop_.geometry.left(), desktop_.geometry.right());
    y.setRange(desktop_.geometry.top(), desktop_.geometry.bottom());
    width.setRange(1, this->width());
    height.setRange(1, this->height());
    radius.setRange(0, 500);
    const QRect target = selection_.isEmpty() ? QRect(mouse_, QSize(640, 480)) : selection_;
    x.setValue(target.x() + desktop_.geometry.x());
    y.setValue(target.y() + desktop_.geometry.y());
    width.setValue(target.width());
    height.setValue(target.height());
    radius.setValue(radius_);
    QComboBox ratio;
    ratio.addItems({QStringLiteral("自由"), "1:1", "4:3", "16:9", "9:16", QStringLiteral("按输入尺寸固定")});
    form.addRow("X", &x);
    form.addRow("Y", &y);
    form.addRow(QStringLiteral("宽 / px"), &width);
    form.addRow(QStringLiteral("高 / px"), &height);
    form.addRow(QStringLiteral("圆角 / px"), &radius);
    form.addRow(QStringLiteral("比例"), &ratio);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const double ratios[]{0, 1, 4.0 / 3, 16.0 / 9, 9.0 / 16};
    ratio_ = ratio.currentIndex() < 5 ? ratios[ratio.currentIndex()] : double(width.value()) / height.value();
    selection_ = QRect(x.value() - desktop_.geometry.x(), y.value() - desktop_.geometry.y(), width.value(),
                       height.value());
    if (ratio_ > 0)
        selection_.setHeight(qRound(selection_.width() / ratio_));
    radius_ = radius.value();
    constrainSelection();
    syncSelection();
}
