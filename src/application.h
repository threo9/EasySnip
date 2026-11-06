#pragma once
#include "capture.h"
#include "editor.h"
#include "library.h"
#include "native.h"
#include <QPointer>
#include <QSystemTrayIcon>
#include <QElapsedTimer>

class QMenu;

class Application : public QObject {
    Q_OBJECT
  public:
    explicit Application(Settings &settings, QObject *parent = nullptr);
    ~Application() override;
    void start(const QStringList &arguments);
    void command(const QString &command);
    void processArguments(const QStringList &arguments);

  private:
    void configureInput();
    void capture(const QString &mode = "region", const QRect &preset = {}, int delay = -1);
    void showCapture(Native::Desktop desktop, const QRect &initial);
    void capturePreset();
    void edit(Document document, const QRect &sourceRect = {}, const QString &recordId = {});
    void openImage(const QString &path);
    void recordAction(const QString &id, const QString &action);
    bool output(const Document &document, const QString &action, QWidget *owner, QString &recordId,
                bool skipAutoSave);
    bool saveImage(const QImage &image, const QString &path);
    QString quickSave(const QImage &image, bool cache = false);
    void notify(const QString &message, bool error = false);
    void checkCorners();
    void quit();
    Settings &settings_;
    Library library_;
    LibraryWindow *window_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    GlobalInput input_;
    QVector<QPointer<EditorWindow>> editors_;
    QPointer<CaptureOverlay> capture_;
    QTimer corners_;
    QTimer themeTimer_;
    QTimer hotkeyRetry_;
    QMap<int, QKeySequence> pendingHotkeys_;
    QString corner_;
    QElapsedTimer cornerTime_;
    bool cornerTriggered_ = false;
    bool capturePending_ = false;
    bool inputIgnored_ = false;
    bool quitting_ = false;
};
