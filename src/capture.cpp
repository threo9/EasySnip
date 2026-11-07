#include "capture.h"
#include "appearance.h"
#include "editor.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <limits>

namespace {
QVector<QPoint> captureHandles(const QRect &selection) {
    return {selection.topLeft(), QPoint(selection.center().x(), selection.top()), selection.topRight(),
        QPoint(selection.right(), selection.center().y()), selection.bottomRight(),
        QPoint(selection.center().x(), selection.bottom()), selection.bottomLeft(),
        QPoint(selection.left(), selection.center().y())};
}

Qt::CursorShape resizeCursor(int handle) {
    if (handle == 0 || handle == 4)
        return Qt::SizeFDiagCursor;
    if (handle == 2 || handle == 6)
        return Qt::SizeBDiagCursor;
    if (handle == 1 || handle == 5)
        return Qt::SizeVerCursor;
    return Qt::SizeHorCursor;
}

class CaptureBorder final : public QWidget {
  public:
    explicit CaptureBorder(QWidget *parent) : QWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    void setSelection(const QRect &selection, int radius) {
        const int hit = uiPixels(8), edge = uiPixels(4);
        setGeometry(selection.adjusted(-hit, -hit, hit, hit).intersected(parentWidget()->rect()));
        frame_ = selection.translated(-geometry().topLeft());
        radius_ = radius;
        QRegion ring(frame_.adjusted(-edge, -edge, edge, edge));
        const QRect inner = frame_.adjusted(edge, edge, -edge, -edge);
        if (!inner.isEmpty())
            ring -= QRegion(inner);
        for (const auto &point : captureHandles(frame_))
            ring += QRegion(QRect(point - QPoint(hit, hit), QSize(2 * hit + 1, 2 * hit + 1)));
        setMask(ring.intersected(QRegion(rect())));
        update();
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(QPen(Ui::accent(), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame_.adjusted(-1, -1, 1, 1), radius_, radius_);
        painter.setBrush(Ui::accent());
        const int size = uiPixels(3);
        for (const auto &point : captureHandles(frame_))
            painter.drawRect(QRect(point - QPoint(size, size), QSize(size * 2, size * 2)));
    }
  private:
    QRect frame_;
    int radius_ = 0;
};

const QVector<QVector<Tool>> &annotationToolGroups() {
    static const QVector<QVector<Tool>> groups{{Tool::Rectangle, Tool::Ellipse, Tool::RoundedRectangle},
        {Tool::Arrow, Tool::Line}, {Tool::Polyline}, {Tool::Pen},
        {Tool::Mosaic, Tool::Blur}, {Tool::Text, Tool::Number}, {Tool::Eraser}, {Tool::Magnify}};
    return groups;
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
    toolbar_->setObjectName("captureControls");
    Ui::addFloatingShadow(toolbar_);
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
    Ui::addFloatingShadow(editorToolbar_);
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
    resizeBorder_ = new CaptureBorder(this);
    resizeBorder_->setObjectName("captureResizeBorder");
    resizeBorder_->installEventFilter(this);
    resizeBorder_->hide();
    for (const auto &group : annotationToolGroups()) {
        auto *button = new QToolButton;
        button->setIconSize(bar->iconSize());
        button->setFocusPolicy(Qt::NoFocus);
        QVariantList variants;
        for (Tool tool : group)
            variants.append(int(tool));
        button->setProperty("captureToolGroup", variants);
        if (group.first() == Tool::Arrow)
            button->setObjectName("captureLineToolButton");
        for (Tool tool : group) {
            auto *action = new QAction(Ui::toolIcon(tool), toolName(tool), tools_);
            if (tool == Tool::Line || tool == Tool::Arrow)
                action->setToolTip(toolName(tool) + QStringLiteral(" · 拖动两端调整 · Tab 切换"));
            action->setObjectName("captureTool" + toolKey(tool));
            action->setData(int(tool));
            action->setCheckable(true);
            tools_->addAction(action);
            if (tool == group.first())
                button->setDefaultAction(action);
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        }
        bar->addWidget(button);
    }
    connect(tools_, &QActionGroup::triggered, this, &CaptureOverlay::selectTool);
    bar->addSeparator();
    undo_ = bar->addAction(Ui::commandIcon("undo"), QStringLiteral("撤销 · Ctrl+Z"), this, [this] {
        if (canvas_)
            canvas_->undo();
    });
    redo_ = bar->addAction(Ui::commandIcon("redo"), QStringLiteral("重做 · Ctrl+Y"), this, [this] {
        if (canvas_)
            canvas_->redo();
    });
    bar = outputTools_;
    bar->addAction(Ui::commandIcon("cancel"), QStringLiteral("取消 · Esc"), this, &CaptureOverlay::cancel);
    auto output = [this, bar](const QString &label, const QString &name, const QString &key) {
        auto *action = bar->addAction(Ui::commandIcon(name), label, this, [this, name] {
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
    more->setObjectName("captureMore");
    more->setIcon(Ui::commandIcon("more"));
    more->setIconSize(bar->iconSize());
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

    styleBar_ = new QFrame(this);
    styleBar_->setObjectName("captureToolOptions");
    Ui::addFloatingShadow(styleBar_);
    auto *styleLayout = new QVBoxLayout(styleBar_);
    styleLayout->setContentsMargins(6, 6, 6, 6);
    styleLayout->setSpacing(4);
    auto *style = new QBoxLayout(QBoxLayout::LeftToRight);
    variantLayout_ = style;
    styleLayout->addLayout(style);
    style->setContentsMargins(4, 0, 4, 0);
    toolVariant_ = new OptionStrip;
    toolVariant_->setObjectName("captureToolVariant");
    toolVariant_->setFocusPolicy(Qt::NoFocus);
    toolVariant_->setToolTip(QStringLiteral("具体形状 / 线条样式"));
    style->addWidget(toolVariant_);
    auto changeVariant = [this](Tool tool) {
        if (canvas_ && canvas_->selectedIndex() >= 0) {
            auto next = canvas_->style();
            next.tool = tool;
            canvas_->setStyle(next);
        } else if (canvas_ && tool != canvas_->tool())
            findChild<QAction *>("captureTool" + toolKey(tool))->trigger();
        if (canvas_)
            canvas_->setFocus();
    };
    connect(toolVariant_, &OptionStrip::activated, this, [this, changeVariant](int index) {
        changeVariant(Tool(toolVariant_->itemData(index).toInt()));
    });
    lineType_ = new LineTypeButton;
    lineType_->setObjectName("captureLineTypeButton");
    style->addWidget(lineType_);
    connect(lineType_, &LineTypeButton::toolClicked, this, changeVariant);
    effectShape_ = new OptionStrip;
    effectShape_->setObjectName("captureEffectShape");
    effectShape_->setFocusPolicy(Qt::NoFocus);
    effectShape_->setToolTip(QStringLiteral("作用形状"));
    effectShape_->addItem(Ui::toolIcon(Tool::Rectangle), QStringLiteral("矩形区域"), 0);
    effectShape_->addItem(Ui::toolIcon(Tool::Ellipse), QStringLiteral("椭圆区域"), 1);
    QPixmap circleIcon(uiPixels(20) * 2, uiPixels(20) * 2);
    circleIcon.fill(Qt::transparent);
    QPainter circlePainter(&circleIcon);
    circlePainter.setRenderHint(QPainter::Antialiasing);
    circlePainter.scale(circleIcon.width() / 20.0, circleIcon.height() / 20.0);
    circlePainter.setPen(QPen(QColor("#303030"), 1.5));
    circlePainter.drawEllipse(QRectF(3, 3, 14, 14));
    circlePainter.end();
    circleIcon.setDevicePixelRatio(2);
    effectShape_->addItem(QIcon(circleIcon), QStringLiteral("圆形区域"), 2);
    effectShape_->addItem(Ui::toolIcon(Tool::Pen), QStringLiteral("画笔轨迹"), 3);
    style->addWidget(effectShape_);
    connect(effectShape_, &OptionStrip::activated, this, [this](int index) {
        if (canvas_) {
            auto next = canvas_->style();
            next.shape = index;
            canvas_->setStyle(next);
            canvas_->setFocus();
        }
    });
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
    style->addStretch();
    auto choiceRow = [&](const QString &label, const QString &name, const QStringList &labels, OptionStrip *&strip) {
        auto *group = new QWidget;
        auto *row = new QHBoxLayout(group);
        row->setContentsMargins(4, 0, 4, 0);
        row->setSpacing(uiPixels(8));
        row->addWidget(new QLabel(label));
        strip = new OptionStrip;
        strip->setObjectName(name);
        strip->addItems(labels);
        row->addWidget(strip);
        row->addStretch();
        styleLayout->addWidget(group);
        return group;
    };
    lineOptions_ = choiceRow(QStringLiteral("线型"), "captureLineStyle", {}, lineStyle_);
    lineStyle_->addItem(Ui::lineStyleIcon(false), QStringLiteral("实线"), int(Qt::SolidLine));
    lineStyle_->addItem(Ui::lineStyleIcon(true), QStringLiteral("虚线"), int(Qt::DashLine));
    styleLayout->removeWidget(lineOptions_);
    style->insertWidget(style->indexOf(color_), lineOptions_);
    arrowOptions_ = choiceRow(QStringLiteral("箭头"), "captureArrowStyle",
        {QStringLiteral("单向"), QStringLiteral("双向"), QStringLiteral("反向"), QStringLiteral("实心")}, arrowStyle_);
    fillOptions_ = choiceRow(QStringLiteral("填充"), "captureFillMode", {}, fillMode_);
    fillMode_->addItem(Ui::fillModeIcon(false), QStringLiteral("空心"), 0);
    fillMode_->addItem(Ui::fillModeIcon(true), QStringLiteral("填充"), 1);
    fillColor_ = new QPushButton(QStringLiteral("填充颜色…"));
    fillColor_->setObjectName("captureFillColor");
    static_cast<QHBoxLayout *>(fillOptions_->layout())->insertWidget(2, fillColor_);
    auto changeChoice = [this](OptionStrip *strip, int Annotation::*member, int offset) {
        connect(strip, &OptionStrip::activated, this, [this, member, offset](int index) {
            if (!canvas_)
                return;
            auto next = canvas_->style();
            next.*member = index + offset;
            canvas_->setStyle(next);
            canvas_->setFocus();
        });
    };
    changeChoice(lineStyle_, &Annotation::lineStyle, 1);
    changeChoice(arrowStyle_, &Annotation::arrowStyle, 0);
    connect(fillMode_, &OptionStrip::activated, this, [this](int index) {
        if (!canvas_)
            return;
        auto next = canvas_->style();
        if (index == 0)
            next.fill = Qt::transparent;
        else if (next.fill.alpha() == 0)
            next.fill = next.color;
        canvas_->setStyle(next);
        canvas_->setFocus();
    });
    auto pickColor = [this](QPushButton *button, QColor Annotation::*member) {
        connect(button, &QPushButton::clicked, this, [this, member] {
            if (!canvas_)
                return;
            auto next = canvas_->style();
            const QColor chosen = QColorDialog::getColor(next.*member, this, QStringLiteral("颜色与透明度"),
                                                         QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                next.*member = chosen;
                canvas_->setStyle(next);
            }
        });
    };
    pickColor(fillColor_, &Annotation::fill);
    strengthLayout_ = new QBoxLayout(QBoxLayout::LeftToRight);
    strengthLayout_->setContentsMargins(4, 0, 4, 0);
    strengthLayout_->setSpacing(uiPixels(12));
    styleLayout->addLayout(strengthLayout_);
    auto strength = [&](const QString &label, const QString &name, double minimum, double maximum,
                        double step, int decimals, const QString &suffix, ValueSlider *&slider) {
        auto *group = new QWidget;
        auto *row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(uiPixels(4));
        row->addWidget(new QLabel(label));
        slider = new ValueSlider(minimum, maximum, step, decimals, suffix);
        slider->setObjectName(name);
        slider->setFixedWidth(uiPixels(130));
        row->addWidget(slider);
        strengthLayout_->addWidget(group);
        connect(slider, &ValueSlider::editingStarted, this, [this] { if (canvas_) canvas_->beginStyleChange(); });
        connect(slider, &ValueSlider::editingFinished, this, [this] {
            if (canvas_) { canvas_->endStyleChange(); canvas_->setFocus(); }
        });
        return group;
    };
    strokeOptions_ = strength(QStringLiteral("粗细"), "captureStrokeWidth", 1, 100, 0.1, 1, {}, strokeWidth_);
    fontOptions_ = strength(QStringLiteral("字号"), "captureFontSize", 6, 300, 1, 0, {}, fontSize_);
    opacityOptions_ = strength(QStringLiteral("不透明度"), "captureOpacity", 0, 100, 100.0 / 255, 0, "%", opacity_);
    connect(strokeWidth_, &ValueSlider::valueChanged, this, [this](double width) {
        if (canvas_) {
            auto next = canvas_->style();
            next.width = width;
            canvas_->setStyle(next);
        }
    });
    connect(fontSize_, &ValueSlider::valueChanged, this, [this](double size) {
        if (canvas_) {
            auto next = canvas_->style();
            next.fontSize = size;
            canvas_->setStyle(next);
        }
    });
    connect(opacity_, &ValueSlider::valueChanged, this, [this](double opacity) {
        if (canvas_) {
            auto next = canvas_->style();
            next.color.setAlpha(qRound(opacity * 255 / 100));
            canvas_->setStyle(next);
        }
    });
    auto detailSlider = [&](const QString &label, const QString &name, int minimum, int maximum,
                            double Annotation::*member, ValueSlider *&slider) {
        auto *group = new QWidget;
        auto *row = new QHBoxLayout(group);
        row->setContentsMargins(4, 0, 4, 0);
        row->addWidget(new QLabel(label));
        slider = new ValueSlider(minimum, maximum);
        slider->setObjectName(name);
        slider->setFixedWidth(uiPixels(160));
        row->addWidget(slider);
        row->addStretch();
        styleLayout->addWidget(group);
        connect(slider, &ValueSlider::valueChanged, this, [this, member](double value) {
            if (!canvas_)
                return;
            auto next = canvas_->style();
            next.*member = value;
            canvas_->setStyle(next);
        });
        connect(slider, &ValueSlider::editingStarted, this, [this] { if (canvas_) canvas_->beginStyleChange(); });
        connect(slider, &ValueSlider::editingFinished, this, [this] { if (canvas_) canvas_->endStyleChange(); });
        return group;
    };
    outlineOptions_ = detailSlider(QStringLiteral("描边"), "captureOutlineWidth", 0, 20,
                                  &Annotation::outlineWidth, outlineWidth_);
    outlineColor_ = new QPushButton(QStringLiteral("描边颜色…"));
    outlineColor_->setObjectName("captureOutlineColor");
    static_cast<QHBoxLayout *>(outlineOptions_->layout())->insertWidget(2, outlineColor_);
    pickColor(outlineColor_, &Annotation::outline);
    rotationOptions_ = detailSlider(QStringLiteral("旋转"), "captureRotation", -360, 360,
                                   &Annotation::rotation, rotation_);
    styleBar_->hide();
    editorToolbar_->hide();
}

void CaptureOverlay::selectTool(QAction *action) {
    if (!canvas_ || outputPending_)
        return;
    canvas_->finishDrawing();
    const Tool requested = Tool(action->data().toInt());
    if (!selecting_ && canvas_->tool() == requested) {
        deactivateTool();
        return;
    }
    selecting_ = false;
    canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    canvas_->setTool(requested);
    canvas_->setVisible(!selecting_ || !canvas_->document().annotations.isEmpty());
    syncStyle();
    placeToolbar();
    update();
}

void CaptureOverlay::deactivateTool() {
    selecting_ = true;
    if (canvas_) {
        canvas_->finishDrawing();
        canvas_->setTool(Tool::Select);
        canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        canvas_->show();
    }
    syncStyle();
    setFocus();
    update();
}

void CaptureOverlay::updateResizeBorder() {
    const bool visible = !selection_.isEmpty() && historyIndex_ < 0;
    if (visible) {
        static_cast<CaptureBorder *>(resizeBorder_)->setSelection(selection_, radius_);
        resizeBorder_->raise();
    }
    resizeBorder_->setVisible(visible);
    styleBar_->raise();
    editorToolbar_->raise();
}

void CaptureOverlay::syncStyle() {
    undo_->setEnabled(canvas_ && canvas_->canUndo());
    redo_->setEnabled(canvas_ && canvas_->canRedo());
    if (!canvas_) {
        styleBar_->hide();
        return;
    }
    const auto style = canvas_->style();
    const Tool context = canvas_->selectedIndex() >= 0 ? style.tool : canvas_->tool();
    const bool selected = canvas_->selectedIndex() >= 0;
    const bool active = (selected || canvas_->tool() != Tool::Select) && !selection_.isEmpty();
    const bool color = context != Tool::Mosaic && context != Tool::Blur &&
                       context != Tool::Eraser && context != Tool::Magnify;
    const bool font = context == Tool::Text || context == Tool::Number;
    const bool width = context != Tool::Text && context != Tool::Magnify;
    const bool lineType = context == Tool::Line || context == Tool::Arrow;
    lineType_->setVisible(lineType);
    lineType_->setTool(context);
    const QSignalBlocker widthBlock(strokeWidth_), fontBlock(fontSize_), opacityBlock(opacity_), variantBlock(toolVariant_),
        shapeBlock(effectShape_), lineBlock(lineStyle_), arrowBlock(arrowStyle_), fillBlock(fillMode_),
        outlineBlock(outlineWidth_), rotationBlock(rotation_);
    for (const auto &group : annotationToolGroups())
        if (group.contains(context)) {
            if (toolVariant_->count() != group.size() ||
                toolVariant_->itemData(0).toInt() != int(group.first())) {
                toolVariant_->clear();
                for (Tool tool : group)
                    toolVariant_->addItem(Ui::toolIcon(tool), toolName(tool), int(tool));
            }
            toolVariant_->setCurrentIndex(toolVariant_->findData(int(context)));
            toolVariant_->setVisible(group.size() > 1 && !lineType);
            break;
        }
    const bool shape = context == Tool::Rectangle || context == Tool::Ellipse || context == Tool::RoundedRectangle;
    lineOptions_->setVisible(shape || isLineTool(context) || context == Tool::Pen);
    arrowOptions_->setVisible(context == Tool::Arrow);
    fillOptions_->setVisible(shape || context == Tool::Text);
    outlineOptions_->setVisible(context == Tool::Text);
    rotationOptions_->setVisible(selected && !isLineTool(context) && context != Tool::Eraser);
    lineStyle_->setCurrentIndex(lineStyle_->findData(style.lineStyle));
    arrowStyle_->setCurrentIndex(style.arrowStyle);
    fillMode_->setCurrentIndex(style.fill.alpha() == 0 ? 0 : 1);
    outlineWidth_->setValue(style.outlineWidth);
    rotation_->setValue(style.rotation);
    effectShape_->setVisible(isEffectTool(context));
    effectShape_->setCurrentIndex(style.shape);
    color_->setVisible(color);
    strokeOptions_->setVisible(width);
    fontOptions_->setVisible(font);
    opacityOptions_->setVisible(color);
    if (presets_) {
        const bool brush = context == Tool::Pen || (isEffectTool(context) && style.shape == 3);
        brushTips_->setVisible(brush);
        colorPresets_->setVisible(color);
        presets_->setVisible(color || brush);
    }
    styleBar_->setVisible(active);
    strokeWidth_->setValue(style.width);
    fontSize_->setValue(qRound(style.fontSize));
    opacity_->setValue(style.color.alphaF() * 100);
    color_->setText(style.color.name().toUpper());
    color_->setStyleSheet(QString("background:%1;color:%2").arg(
        style.color.name(), style.color.lightness() > 140 ? "#111827" : "white"));
    for (auto *action : tools_->actions())
        action->setChecked(!selecting_ && action->data().toInt() == int(canvas_->tool()));
    if (editorToolbar_->isVisible())
        placeToolbar();
}

QRect CaptureOverlay::selectedGlobal() const {
    if (historyIndex_ >= 0 && canvas_)
        return canvas_->document().sourceRect;
    return selection_.translated(desktop_.geometry.topLeft());
}
QImage CaptureOverlay::backgroundImage() const {
    if (!cursor_ || desktop_.cursorImage.isNull()) {
        backgroundCache_ = {};
        return desktop_.image;
    }
    const QPoint position = desktop_.cursorPosition - desktop_.geometry.topLeft() - desktop_.cursorHotspot;
    if (backgroundCache_.desktopKey != desktop_.image.cacheKey() ||
        backgroundCache_.cursorKey != desktop_.cursorImage.cacheKey() ||
        backgroundCache_.cursorPosition != position || backgroundCache_.image.isNull()) {
        backgroundCache_.image = desktop_.image;
        QPainter painter(&backgroundCache_.image);
        painter.drawImage(position, desktop_.cursorImage);
        painter.end();
        backgroundCache_.desktopKey = desktop_.image.cacheKey();
        backgroundCache_.cursorKey = desktop_.cursorImage.cacheKey();
        backgroundCache_.cursorPosition = position;
    }
    return backgroundCache_.image;
}
Document CaptureOverlay::selectionBackground() const {
    if (historyIndex_ >= 0 && canvas_)
        return canvas_->document();
    Document document;
    if (!selection_.isEmpty()) {
        const QImage background = backgroundImage();
        const QRect region = selection_.intersected(desktop_.image.rect());
        if (selectionBackgroundKey_ != background.cacheKey() || selectionImageRect_ != region) {
            selectionImage_ = background.copy(region);
            selectionBackgroundKey_ = background.cacheKey();
            selectionImageRect_ = region;
        }
        document.image = selectionImage_;
    }
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
        styleBar_->hide();
        resizeBorder_->hide();
        placeToolbar();
        update();
        return;
    }
    if (!canvas_) {
        canvas_ = new AnnotationCanvas(selectionBackground(), settings_, this);
        canvas_->setObjectName("captureCanvas");
        canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        canvas_->installEventFilter(this);
        connect(canvas_, &AnnotationCanvas::changed, this, &CaptureOverlay::syncStyle);
        connect(canvas_, &AnnotationCanvas::selectionChanged, this, &CaptureOverlay::syncStyle);
        connect(canvas_, &AnnotationCanvas::imageCopyRequested, this, [this] { requestOutput("copy"); });
        connect(canvas_, &AnnotationCanvas::toolChanged, this, [this](Tool tool) {
            selecting_ = tool == Tool::Select;
            auto *action = findChild<QAction *>("captureTool" + toolKey(tool));
            if (action) {
                action->setChecked(true);
                for (auto *button : annotationTools_->findChildren<QToolButton *>())
                    if (button->property("captureToolGroup").toList().contains(int(tool)))
                        button->setDefaultAction(action);
            }
            syncStyle();
        });
        presets_ = new QWidget(styleBar_);
        auto *row = new QHBoxLayout(presets_);
        row->setContentsMargins(4, 0, 4, 0);
        brushTips_ = new BrushTips(canvas_, settings_);
        brushTips_->setObjectName("captureBrushTips");
        row->addWidget(brushTips_);
        colorPresets_ = new QWidget(presets_);
        colorPresets_->setObjectName("captureColorPresets");
        auto *colors = new QHBoxLayout(colorPresets_);
        colors->setContentsMargins(0, 0, 0, 0);
        colors->setSpacing(4);
        auto *palette = new PaletteWidget(canvas_, settings_, colorPresets_);
        palette->setFixedHeight(uiPixels(34));
        palette->setWrapping(false);
        colors->addWidget(palette, 1);
        auto *editPalette = new QPushButton(QStringLiteral("色板…"));
        colors->addWidget(editPalette);
        row->addWidget(colorPresets_, 1);
        connect(editPalette, &QPushButton::clicked, palette, &PaletteWidget::edit);
        styleBar_->layout()->addWidget(presets_);
    } else if (historyIndex_ < 0) {
        const QPointF offset = regionResizing_ ? canvas_->document().sourceRect.topLeft() - selectedGlobal().topLeft()
                                               : QPointF();
        canvas_->setCaptureBackground(selectionBackground(), offset);
    }
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
    canvas_->show();
    toolbar_->hide();
    editorToolbar_->show();
    editorToolbar_->raise();
    syncStyle();
    placeToolbar();
    updateResizeBorder();
    update();
}
void CaptureOverlay::clearSelection() {
    const bool releaseCapture = movingFromCanvas_;
    backgroundMovePending_ = movingFromCanvas_ = false;
    if (releaseCapture && QWidget::mouseGrabber() == this)
        releaseMouse();
    delete presets_;
    presets_ = nullptr;
    colorPresets_ = brushTips_ = nullptr;
    delete canvas_;
    canvas_ = nullptr;
    selection_ = {};
    resizePreview_ = {};
    selecting_ = true;
    regionResizing_ = false;
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
        canvas_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
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
    if (styleBar_->isVisible()) {
        variantLayout_->setDirection(QBoxLayout::LeftToRight);
        if (variantLayout_->sizeHint().width() + 20 > screen.width())
            variantLayout_->setDirection(QBoxLayout::TopToBottom);
        strengthLayout_->setDirection(QBoxLayout::LeftToRight);
        if (strengthLayout_->sizeHint().width() + 20 > screen.width())
            strengthLayout_->setDirection(QBoxLayout::TopToBottom);
        styleBar_->layout()->activate();
        const QSize optionsHint = styleBar_->sizeHint();
        styleBar_->resize(qMin(optionsHint.width(), screen.width()), optionsHint.height());
        int anchorX = x;
        for (auto *button : annotationTools_->findChildren<QToolButton *>())
            if (button->defaultAction() == tools_->checkedAction())
                anchorX = button->mapTo(this, QPoint()).x();
        const int optionsX = qBound(screen.left(), anchorX,
                                    qMax(screen.left(), screen.right() - styleBar_->width() + 1));
        const bool below = editorToolbar_->geometry().top() > selection_.bottom();
        const int belowY = editorToolbar_->geometry().bottom() + gap;
        const int aboveY = editorToolbar_->geometry().top() - gap - styleBar_->height();
        const QPoint preferred(optionsX, below ? belowY : aboveY);
        const QVector<QPoint> candidates{preferred, QPoint(optionsX, below ? aboveY : belowY),
            QPoint(editorToolbar_->geometry().left() - gap - styleBar_->width(), y),
            QPoint(editorToolbar_->geometry().right() + gap, y)};
        qint64 bestScore = std::numeric_limits<qint64>::max();
        QPoint best = preferred;
        auto area = [](const QRect &region) -> qint64 {
            return region.isEmpty() ? 0 : qint64(region.width()) * region.height();
        };
        for (const auto &candidate : candidates) {
            const QPoint position(qBound(screen.left(), candidate.x(),
                                          qMax(screen.left(), screen.right() - styleBar_->width() + 1)),
                                   qBound(screen.top(), candidate.y(),
                                          qMax(screen.top(), screen.bottom() - styleBar_->height() + 1)));
            const QRect bounds(position, styleBar_->size());
            const qint64 score = area(bounds.intersected(editorToolbar_->geometry())) * 1000000 +
                                 area(bounds.intersected(selection_)) * 10 +
                                 (position - preferred).manhattanLength();
            if (score < bestScore) {
                bestScore = score;
                best = position;
            }
        }
        styleBar_->move(best);
        styleBar_->raise();
        editorToolbar_->raise();
    }
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.drawImage(0, 0, backgroundImage());
    if (pressed_ && (regionResizing_ || moving_) && !resizePreview_.isNull()) {
        painter.save();
        painter.setClipRect(selection_);
        painter.drawImage(resizePreviewOrigin_, resizePreview_);
        painter.restore();
    }
    const QRect target = selection_.isEmpty() ? animatedHover_ : selection_;
    QPainterPath mask;
    mask.addRect(rect());
    QPainterPath hole;
    if (!target.isEmpty())
        hole.addRoundedRect(target, radius_, radius_);
    painter.fillPath(mask.subtracted(hole), QColor(4, 10, 20, 140));
    painter.setPen(QPen(Ui::accent(), 1));
    painter.setBrush(Qt::NoBrush);
    if (!target.isEmpty()) {
        painter.drawRoundedRect(target.adjusted(-1, -1, 1, 1), radius_, radius_);
        const QString text = QStringLiteral("%1 × %2 px   (%3, %4)%5")
                                 .arg(target.width())
                                 .arg(target.height())
                                 .arg(target.x() + desktop_.geometry.x())
                                 .arg(target.y() + desktop_.geometry.y())
                                 .arg(typed_.isEmpty() ? "" : "   " + typed_);
        const QRect label(target.left(), qMax(uiPixels(5), target.top() - uiPixels(35)),
                          qMax(uiPixels(240), painter.fontMetrics().horizontalAdvance(text) + uiPixels(20)), uiPixels(28));
        painter.setPen(QPen(QColor("#d6d6d6"), 1));
        painter.setBrush(QColor("#fbfbfb"));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawRoundedRect(label, uiPixels(5), uiPixels(5));
        painter.setPen(QColor("#242424"));
        painter.drawText(label.adjusted(uiPixels(10), 0, -uiPixels(6), 0), Qt::AlignVCenter, text);
    }
    QRect currentScreen = rect();
    for (const auto &screen : desktop_.screens)
        if (screen.contains(QCursor::pos()))
            currentScreen = screen.translated(-desktop_.geometry.topLeft());
    const QRect help(currentScreen.left() + uiPixels(20), currentScreen.top() + uiPixels(20),
                     qMin(uiPixels(1000), currentScreen.width() - uiPixels(40)), uiPixels(38));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#d6d6d6"), 1));
    painter.setBrush(QColor(251, 251, 251, 245));
    painter.drawRoundedRect(help, uiPixels(7), uiPixels(7));
    painter.setPen(QColor("#343434"));
    const QString helpText = selecting_
        ? QStringLiteral("拖动空白处移动选区 · 边框缩放 · Ctrl+D 尺寸 · Enter 复制 · , . 历史 · Esc 取消")
        : QStringLiteral("拖动边框调整范围 · Shift 约束 · 1/2 粗细 · 右键结束编辑 · Enter 复制 · Esc 取消");
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
        painter.setPen(QPen(QColor("#d6d6d6"), 1));
        painter.setBrush(QColor("#fbfbfb"));
        painter.drawRoundedRect(QRect(position, box), uiPixels(6), uiPixels(6));
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
        painter.setPen(QColor("#242424"));
        const QString value =
            colorText(desktop_.image.pixelColor(mouse_), settings_.value("color/format", "HEX").toString(),
                      settings_.value("color/uppercase", true).toBool());
        painter.drawText(QRect(position + QPoint(4, lens.height()), QSize(lens.width() - 8, 48)),
                         Qt::AlignCenter, value + QStringLiteral("\nC 复制 · Shift 格式 · Alt 开关"));
    }
}
int CaptureOverlay::handleAt(const QPoint &point) const {
    if (selection_.isEmpty() || historyIndex_ >= 0)
        return -1;
    const auto handles = captureHandles(selection_);
    int nearest = -1;
    int distance = uiPixels(8) + 1;
    for (int i = 0; i < handles.size(); ++i) {
        const QPoint delta = handles[i] - point;
        const int next = qMax(qAbs(delta.x()), qAbs(delta.y()));
        if (next < distance) { nearest = i; distance = next; }
    }
    if (nearest >= 0)
        return nearest;
    const int edge = uiPixels(4);
    if (point.y() >= selection_.top() - edge && point.y() <= selection_.bottom() + edge) {
        if (qAbs(point.x() - selection_.left()) <= edge) return 7;
        if (qAbs(point.x() - selection_.right()) <= edge) return 3;
    }
    if (point.x() >= selection_.left() - edge && point.x() <= selection_.right() + edge) {
        if (qAbs(point.y() - selection_.top()) <= edge) return 1;
        if (qAbs(point.y() - selection_.bottom()) <= edge) return 5;
    }
    return -1;
}
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->modifiers().testFlag(Qt::AltModifier))
        altUsed_ = true;
    if (outputPending_ || refreshing_)
        return;
    if (event->button() == Qt::RightButton) {
        if (!selecting_)
            deactivateTool();
        else if (!selection_.isEmpty())
            clearSelection();
        else
            cancel();
        return;
    }
    const int edgeHandle = handleAt(event->pos());
    if (event->button() != Qt::LeftButton || (!selecting_ && edgeHandle < 0))
        return;
    if (canvas_)
        canvas_->finishDrawing();
    pressed_ = true;
    mouse_ = event->pos();
    anchor_ = previous_ = event->pos();
    handle_ = edgeHandle;
    regionResizing_ = handle_ >= 0;
    resizeStart_ = selection_;
    moving_ = handle_ < 0 && selection_.contains(event->pos());
    if ((regionResizing_ || moving_) && canvas_) {
        resizePreview_ = canvas_->document().render();
        resizePreviewOrigin_ = canvas_->pos();
    }
    if (!moving_ && handle_ < 0)
        clearSelection();
    if (canvas_)
        canvas_->hide();
    editorToolbar_->hide();
    styleBar_->hide();
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
            if (regionResizing_)
                resizeStart_.translate(delta);
        } else if (handle_ >= 0) {
            QRect next = resizeStart_;
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
        if (moving_ && canvas_ && !canvas_->document().annotations.isEmpty()) {
            Document preview = selectionBackground();
            preview.annotations = canvas_->document().annotations;
            resizePreview_ = preview.render();
            resizePreviewOrigin_ = selection_.topLeft();
        } else if (moving_)
            resizePreview_ = {};
        updateResizeBorder();
    } else {
        const int handle = handleAt(mouse_);
        setCursor(handle >= 0 ? resizeCursor(handle) : selection_.contains(mouse_) ? Qt::SizeAllCursor : Qt::CrossCursor);
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
    regionResizing_ = false;
    resizePreview_ = {};
    backgroundMovePending_ = movingFromCanvas_ = false;
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
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
        deactivateTool();
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
bool CaptureOverlay::backgroundDragEvent(QMouseEvent *event) {
    const QPoint point = canvas_->mapTo(this, event->pos());
    if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton &&
        selecting_ && historyIndex_ < 0 && !outputPending_ && !refreshing_ &&
        handleAt(point) < 0 && canvas_->canStartBackgroundDrag(event->pos())) {
        canvas_->setTool(Tool::Select);
        mouse_ = point;
        backgroundDragAnchor_ = point;
        backgroundDragModifiers_ = event->modifiers();
        backgroundMovePending_ = true;
        canvas_->grabMouse();
        return true;
    }
    if (event->type() == QEvent::MouseMove && backgroundMovePending_) {
        if ((point - backgroundDragAnchor_).manhattanLength() < QApplication::startDragDistance())
            return true;
        backgroundMovePending_ = false;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(backgroundDragAnchor_),
                          Qt::LeftButton, Qt::LeftButton, backgroundDragModifiers_);
        mousePressEvent(&press);
        if (pressed_ && moving_) {
            movingFromCanvas_ = true;
            grabMouse();
            setFocus();
            setCursor(Qt::SizeAllCursor);
        } else if (QWidget::mouseGrabber() == canvas_)
            canvas_->releaseMouse();
    }
    if (event->type() == QEvent::MouseMove && movingFromCanvas_) {
        QMouseEvent move(QEvent::MouseMove, QPointF(point), Qt::NoButton, event->buttons(), event->modifiers());
        mouseMoveEvent(&move);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && event->button() == Qt::LeftButton) {
        if (backgroundMovePending_) {
            backgroundMovePending_ = false;
            if (QWidget::mouseGrabber() == canvas_)
                canvas_->releaseMouse();
            return true;
        }
        if (movingFromCanvas_) {
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), Qt::LeftButton,
                                event->buttons(), event->modifiers());
            mouseReleaseEvent(&release);
            return true;
        }
    }
    return false;
}
bool CaptureOverlay::eventFilter(QObject *object, QEvent *event) {
    if (object == resizeBorder_ && (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint point = resizeBorder_->mapTo(this, mouse->pos());
        if (event->type() == QEvent::MouseButtonDblClick)
            return true;
        if (event->type() == QEvent::MouseMove || mouse->button() == Qt::LeftButton) {
            QMouseEvent mapped(event->type(), QPointF(point), mouse->screenPos(), mouse->button(),
                               mouse->buttons(), mouse->modifiers());
            if (event->type() == QEvent::MouseButtonPress) {
                mousePressEvent(&mapped);
                if (pressed_)
                    resizeBorder_->grabMouse();
            } else if (event->type() == QEvent::MouseMove)
                mouseMoveEvent(&mapped);
            else {
                mouseReleaseEvent(&mapped);
                if (QWidget::mouseGrabber() == resizeBorder_)
                    resizeBorder_->releaseMouse();
            }
            const int handle = pressed_ ? handle_ : handleAt(point);
            resizeBorder_->setCursor(handle >= 0 ? resizeCursor(handle) : Qt::ArrowCursor);
            return true;
        }
    }
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
        if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
             event->type() == QEvent::MouseButtonRelease) && backgroundDragEvent(static_cast<QMouseEvent *>(event)))
            return true;
        if (event->type() == QEvent::MouseMove && selecting_ && canvas_->selectedIndex() < 0) {
            mouse_ = canvas_->mapTo(this, static_cast<QMouseEvent *>(event)->pos());
            canvas_->setCursor(historyIndex_ < 0 && canvas_->canStartBackgroundDrag(static_cast<QMouseEvent *>(event)->pos())
                                   ? Qt::SizeAllCursor : Qt::ArrowCursor);
            update();
        }
        if (event->type() == QEvent::Wheel &&
            static_cast<QWheelEvent *>(event)->modifiers().testFlag(Qt::ControlModifier))
            return true;
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (selecting_ && canvas_->selectedIndex() < 0) {
                const int code = key->key();
                const auto modifiers = key->modifiers();
                const bool plain = !modifiers.testFlag(Qt::ControlModifier) &&
                                   !modifiers.testFlag(Qt::MetaModifier) &&
                                   (!modifiers.testFlag(Qt::AltModifier) || code == Qt::Key_Alt);
                const bool arrow = code >= Qt::Key_Left && code <= Qt::Key_Down;
                if ((plain && (arrow || (code >= Qt::Key_0 && code <= Qt::Key_9) ||
                    code == Qt::Key_X || code == Qt::Key_Backspace || code == Qt::Key_Space ||
                    code == Qt::Key_Tab || code == Qt::Key_Alt || code == Qt::Key_Shift ||
                    code == Qt::Key_W || code == Qt::Key_A || code == Qt::Key_S || code == Qt::Key_D ||
                    code == Qt::Key_F || code == Qt::Key_I || code == Qt::Key_R || code == Qt::Key_P ||
                    code == Qt::Key_V || code == Qt::Key_C)) ||
                    (arrow && modifiers.testFlag(Qt::ControlModifier))) {
                    keyPressEvent(key);
                    return true;
                }
            }
            if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Comma || key->key() == Qt::Key_Period) {
                keyPressEvent(key);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                !canvas_->drawing()) {
                if (selecting_ && !typed_.isEmpty())
                    keyPressEvent(key);
                else
                    requestOutput("copy");
                return true;
            }
        }
        if (event->type() == QEvent::KeyRelease && selecting_ && canvas_->selectedIndex() < 0) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Alt || key->key() == Qt::Key_Shift || key->key() == Qt::Key_Space) {
                keyReleaseEvent(key);
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
    updateDetectionAt(QCursor::pos());
}
void CaptureOverlay::updateDetectionAt(const QPoint &point) {
    if (done_ || refreshing_ || historyIndex_ >= 0 || pressed_ ||
        (!selection_.isEmpty() && (!elements_ || selecting_)))
        return;
    if ((toolbar_->isVisible() && toolbar_->geometry().contains(mapFromGlobal(point))) ||
        (editorToolbar_->isVisible() && editorToolbar_->geometry().contains(mapFromGlobal(point))) ||
        (styleBar_->isVisible() && styleBar_->geometry().contains(mapFromGlobal(point))))
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
}
void CaptureOverlay::invalidateDetection() {
    ++detectionGeneration_;
    elementRects_.clear();
    elementIndex_ = 0;
}
void CaptureOverlay::applyDetection(const QVector<QRect> &rectangles) {
    bool changed = elementRects_ != rectangles;
    if (elementRects_ != rectangles) {
        elementRects_ = rectangles;
        elementIndex_ = 0;
    }
    for (const auto &element : elementRects_)
        if (!detectedElements_.contains(element)) {
            detectedElements_.append(element);
            changed = true;
        }
    if (selection_.isEmpty())
        setHover(elementRects_.isEmpty() ? QRect() :
                 elementRects_[elementIndex_].translated(-desktop_.geometry.topLeft()).intersected(rect()));
    if (canvas_)
        canvas_->setDetectedElements(selectionBackground().detectedElements);
    if (changed)
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
    QSpinBox x, y, width, height;
    ValueSlider radius(0, 500);
    x.setRange(desktop_.geometry.left(), desktop_.geometry.right());
    y.setRange(desktop_.geometry.top(), desktop_.geometry.bottom());
    width.setRange(1, this->width());
    height.setRange(1, this->height());
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
    radius_ = qRound(radius.value());
    constrainSelection();
    syncSelection();
}
