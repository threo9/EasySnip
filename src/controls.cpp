#include "controls.h"
#include "settings.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>

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
    slider_->setMinimumHeight(uiPixels(22));
    slider_->setStyleSheet(QString(
        "QSlider::groove:horizontal { height:%1px; background:#35445a; border-radius:%2px; }"
        "QSlider::sub-page:horizontal { background:#54d6b0; border-radius:%2px; }"
        "QSlider::handle:horizontal { width:%3px; margin:-%4px 0; background:#e8edf5; "
        "border:1px solid #54d6b0; border-radius:%5px; }"
        "QSlider::sub-page:horizontal:disabled { background:#41516a; }"
        "QSlider::handle:horizontal:disabled { background:#718198; border-color:#41516a; }")
        .arg(uiPixels(4)).arg(uiPixels(2)).arg(uiPixels(12)).arg(uiPixels(5)).arg(uiPixels(6)));
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
