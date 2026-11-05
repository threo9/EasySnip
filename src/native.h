#pragma once
#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QVector>
#include <QUrl>

class QWidget;

namespace Native {
struct Desktop {
    QImage image;
    QImage cursorImage;
    QRect geometry;
    QVector<QRect> screens;
    QPoint cursorPosition;
    QPoint cursorHotspot;
};
void enableDpiAwareness();
Desktop captureDesktop();
QRect foregroundWindow();
QRect windowAt(const QPoint &point, const QStringList &excluded = {});
QVector<QRect> elementsAt(const QPoint &point, const QStringList &excluded = {},
                          const QStringList &nativePrograms = {});
QVector<QRect> windowElements(const QRect &region);
QString foregroundExecutable();
bool matchesProcess(const QString &path, const QStringList &patterns);
bool setAutostart(bool enabled);
void setPriority(const QString &priority);
bool shareImage(QWidget *owner, const QString &file, QString *error, bool showPanel = true);
QPair<QByteArray, QString> downloadImage(const QUrl &url, qint64 maximumBytes);
} // namespace Native

class GlobalInput : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalInput(QObject *parent = nullptr);
    ~GlobalInput() override;
    bool registerShortcut(int id, const QKeySequence &sequence, QString *error);
    void clear();
    void enableSuperDrag(bool enabled);
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
  signals:
    void activated(int id);
    void superDragged(QRect rectangle);

  private:
    QVector<int> registered_;
};
