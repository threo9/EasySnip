#pragma once
#include "document.h"
#include <QIcon>

class Settings;
class QWidget;

namespace Ui {
QColor accent();
QString scaledStyle(const QString &style);
QIcon toolIcon(Tool tool);
QIcon lineStyleIcon(bool dashed);
QIcon fillModeIcon(bool filled);
QIcon commandIcon(const QString &command);
void addFloatingShadow(QWidget *widget);
void applyLightAppearance(const Settings &settings);
} // namespace Ui
