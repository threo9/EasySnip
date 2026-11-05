#pragma once
#include <QColor>
#include <QSettings>
#include <QStringList>
#include <QVariant>
#include <QWidget>

class Settings {
  public:
    explicit Settings(const QString &directory = {});
    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void set(const QString &key, const QVariant &value);
    QString dataDirectory() const;
    QString historyDirectory() const;
    QString outputDirectory() const;
    QString fileName() const;
    QStringList palette() const;
    QColor toolColor(const QString &tool) const;
    void setToolColor(const QString &tool, const QColor &color);
    void sync();

  private:
    QString directory_;
    QSettings settings_;
};

bool showSettings(Settings &settings, QWidget *parent);
void applyAppearance(const Settings &settings);
int uiPixels(int pixels);
