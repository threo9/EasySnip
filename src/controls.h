#pragma once
#include "document.h"
#include <QToolButton>
#include <QWidget>

class QSlider;
class QLabel;
class QHBoxLayout;

// Exclusive choices that remain visible in the tool's expanded panel.
class OptionStrip : public QWidget {
    Q_OBJECT
  public:
    explicit OptionStrip(QWidget *parent = nullptr);
    void addItem(const QIcon &icon, const QString &label, int data);
    void addItems(const QStringList &labels);
    void clear();
    int count() const;
    int findData(int data) const;
    QVariant itemData(int index) const;
    QVariant currentData() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
  signals:
    void activated(int index);
  private:
    QHBoxLayout *row_;
    QVector<QToolButton *> buttons_;
    int current_ = -1;
};

class LineTypeButton : public QToolButton {
    Q_OBJECT
  public:
    explicit LineTypeButton(QWidget *parent = nullptr);
    Tool tool() const;
    void setTool(Tool tool);
  signals:
    void toolClicked(Tool tool);
  private:
    Tool tool_ = Tool::Line;
};

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
