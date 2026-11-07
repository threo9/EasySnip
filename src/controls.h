#pragma once
#include <QWidget>

class QSlider;
class QLabel;

class ValueSlider : public QWidget {
    Q_OBJECT
  public:
    ValueSlider(double minimum, double maximum, double step = 1, int decimals = 0,
                const QString &suffix = {}, QWidget *parent = nullptr);
    double value() const;
    void setValue(double value);
    QSlider *slider() const;
  signals:
    void valueChanged(double value);
    void editingStarted();
    void editingFinished();
  private:
    void updateLabel();
    QSlider *slider_;
    QLabel *label_;
    double minimum_;
    double maximum_;
    double step_;
    int decimals_;
    QString suffix_;
};
