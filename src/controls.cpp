#include "controls.h"
#include "settings.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>

OptionStrip::OptionStrip(QWidget *parent) : QWidget(parent), row_(new QHBoxLayout(this)) {
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(uiPixels(3));
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}
void OptionStrip::addItem(const QIcon &icon, const QString &label, int data) {
    auto *button = new QToolButton(this);
    button->setText(label);
    button->setToolTip(label);
    button->setAccessibleName(label);
    button->setProperty("optionData", data);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(icon);
    if (!icon.isNull()) {
        button->setStyleSheet(QString("QToolButton { padding:%1px; }").arg(uiPixels(5)));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(uiPixels(20), uiPixels(20)));
        button->setFixedSize(uiPixels(34), uiPixels(34));
    } else {
        button->setStyleSheet(QString("QToolButton { padding:%1px %2px; }").arg(uiPixels(5)).arg(uiPixels(7)));
    }
    const int index = buttons_.size();
    buttons_.append(button);
    row_->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, index] {
        setCurrentIndex(index);
        emit activated(index);
    });
}
void OptionStrip::addItems(const QStringList &labels) {
    for (const auto &label : labels)
        addItem({}, label, buttons_.size());
}
void OptionStrip::clear() {
    for (auto *button : buttons_)
        delete button;
    buttons_.clear();
    current_ = -1;
}
int OptionStrip::count() const { return buttons_.size(); }
int OptionStrip::findData(int data) const {
    for (int i = 0; i < buttons_.size(); ++i)
        if (itemData(i).toInt() == data)
            return i;
    return -1;
}
QVariant OptionStrip::itemData(int index) const {
    return index >= 0 && index < buttons_.size() ? buttons_[index]->property("optionData") : QVariant();
}
QVariant OptionStrip::currentData() const { return itemData(current_); }
int OptionStrip::currentIndex() const { return current_; }
void OptionStrip::setCurrentIndex(int index) {
    current_ = index;
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setChecked(i == index);
}

namespace {
QIcon lineTypeIcon(Tool tool) {
    const QColor active("#292929"), inactive("#aaaaaa");
    const QColor arrow = tool == Tool::Arrow ? active : inactive;
    const QColor line = tool == Tool::Line ? active : inactive;
    QPixmap pixmap(uiPixels(32) * 2, uiPixels(32) * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pixmap.width() / 32.0, pixmap.height() / 32.0);
    painter.setPen(QPen(arrow, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(6, 11), QPointF(25, 11));
    painter.setPen(Qt::NoPen);
    painter.setBrush(arrow);
    painter.drawPolygon(QPolygonF({{20, 7}, {27, 11}, {20, 15}, {22, 11}}));
    painter.setPen(QPen(line, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(6, 23), QPointF(26, 23));
    painter.end();
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Active);
    return icon;
}
} // namespace

LineTypeButton::LineTypeButton(QWidget *parent) : QToolButton(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setIconSize(QSize(uiPixels(32), uiPixels(32)));
    setFixedSize(uiPixels(36), uiPixels(36));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("QToolButton { padding:0; border-radius:5px; }");
    setTool(Tool::Line);
    connect(this, &QToolButton::clicked, this, [this] {
        setTool(tool_ == Tool::Line ? Tool::Arrow : Tool::Line);
        emit toolClicked(tool_);
    });
}
Tool LineTypeButton::tool() const {
    return tool_;
}
void LineTypeButton::setTool(Tool tool) {
    if (tool != Tool::Line && tool != Tool::Arrow)
        return;
    if (tool_ == tool && !icon().isNull())
        return;
    tool_ = tool;
    setIcon(lineTypeIcon(tool));
    setText(toolName(tool));
    setAccessibleName(toolName(tool) + QStringLiteral("模式"));
    setToolTip(toolName(tool) + QStringLiteral("模式 · 点击切换为") +
               toolName(tool == Tool::Line ? Tool::Arrow : Tool::Line) + QStringLiteral(" · Tab 切换"));
}

ValueSlider::ValueSlider(double minimum, double maximum, double step, int decimals,
                         const QString &suffix, QWidget *parent)
    : QWidget(parent), slider_(new QSlider(Qt::Horizontal, this)), label_(new QLabel(this)),
      minimum_(minimum), maximum_(maximum), step_(step), decimals_(decimals), suffix_(suffix) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(uiPixels(5));
    slider_->setObjectName("valueSlider");
    slider_->setRange(0, qRound((maximum - minimum) / step));
    slider_->setPageStep(qMax(1, slider_->maximum() / 10));
    slider_->setMinimumWidth(uiPixels(54));
    slider_->setMinimumHeight(uiPixels(26));
    row->addWidget(slider_, 1);
    label_->setObjectName("valueReadout");
    label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const auto metrics = label_->fontMetrics();
    label_->setFixedWidth(qMax(metrics.horizontalAdvance(QString::number(minimum, 'f', decimals) + suffix),
                              metrics.horizontalAdvance(QString::number(maximum, 'f', decimals) + suffix)) + uiPixels(4));
    row->addWidget(label_);
    connect(slider_, &QSlider::valueChanged, this, [this] {
        updateLabel();
        emit valueChanged(value());
    });
    connect(slider_, &QSlider::sliderPressed, this, &ValueSlider::editingStarted);
    connect(slider_, &QSlider::sliderReleased, this, &ValueSlider::editingFinished);
    updateLabel();
}
double ValueSlider::value() const {
    return qBound(minimum_, minimum_ + slider_->value() * step_, maximum_);
}
void ValueSlider::setValue(double value) {
    slider_->setValue(qRound((qBound(minimum_, value, maximum_) - minimum_) / step_));
}
QSlider *ValueSlider::slider() const {
    return slider_;
}
void ValueSlider::updateLabel() {
    label_->setText(QString::number(value(), 'f', decimals_) + suffix_);
}
