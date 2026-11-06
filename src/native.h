#pragma once
#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QVector>

class QWidget;

namespace Native {
struct WindowSnapshot {
    quintptr handle = 0;
    QRect geometry;
    QString executable;
    QVector<QRect> elements;
    bool cached = false;
    QVector<QRect> nativeElements;
};
struct Desktop {
    QImage image;
    QImage cursorImage;
    QRect geometry;
    QVector<QRect> screens;
    QPoint cursorPosition;
    QPoint cursorHotspot;
    QVector<WindowSnapshot> windows;
};
void enableDpiAwareness();
Desktop captureDesktop(bool snapshotElements = false);
QRect foregroundWindow();
QRect windowAt(const QPoint &point, const QStringList &excluded = {}, const QVector<WindowSnapshot> &windows = {});
QVector<QRect> elementsAt(const QPoint &point, const QStringList &excluded = {},
                          const QStringList &nativePrograms = {}, const QVector<WindowSnapshot> &windows = {});
QVector<QRect> automationElements(quintptr window, const QPoint *point = nullptr, bool *complete = nullptr);
QVector<QRect> cachedElementsAt(const QPoint &point, const QVector<WindowSnapshot> &windows,
                               const QStringList &excluded = {}, const QStringList &nativePrograms = {});
QString foregroundExecutable();
bool matchesProcess(const QString &path, const QStringList &patterns);
bool setAutostart(bool enabled);
bool isElevated();
QString quoteArguments(const QStringList &arguments);
bool restartElevated(const QStringList &arguments, QString *error);
void setPriority(const QString &priority);
bool shareImage(QWidget *owner, const QString &file, QString *error, bool showPanel = true);
} // namespace Native

class GlobalInput : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalInput(QObject *parent = nullptr);
    ~GlobalInput() override;
    bool registerShortcut(int id, const QKeySequence &sequence, QString *error, bool *alreadyInUse = nullptr);
    void clear();
    void enableSuperDrag(bool enabled, const QString &modifiers = "Alt+Shift");
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
  signals:
    void activated(int id);
    void superDragged(QRect rectangle);

  private:
    struct Shortcut {
        unsigned int modifiers = 0;
        unsigned int key = 0;
    };
    QMap<int, Shortcut> registered_;
};
