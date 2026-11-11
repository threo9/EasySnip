#pragma once
#include "document.h"
#include "native.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <QPointer>
#include <memory>

class QLabel;
class QPushButton;
class QPainter;

// Retains disjoint original-pixel strips and the current viewport. Only the
// preview is scaled. A full-sized output image is allocated once on completion.
class ScrollStitcher {
  public:
    enum class Status { FirstFrame, Appended, Unchanged, Retracted, Uncertain, Invalid, LimitReached };
    struct Result {
        Status status = Status::Invalid;
        int displacement = 0;
    };
    explicit ScrollStitcher(int maxHeight = 30000, qint64 maxPixels = 32 * 1024 * 1024);
    void setViewport(const QRect &viewport);
    QRect viewport() const { return viewport_; }
    Result append(const QImage &frame);
    static bool sameFrame(const QImage &first, const QImage &second, const QRect &viewport = {});
    QImage image() const;
    QImage preview(const QSize &bounds) const;
    QSize size() const;
    int frameCount() const { return frames_; }

  private:
    bool recoverPosition(const QImage &previous, const QImage &current, int &shift, int &tailStart) const;
    void paintFixedAreas(QPainter &painter) const;
    QVector<QImage> strips_;
    QImage last_;
    QImage reference_;
    // Reuse deterministic checks until a new frame is committed. A changed
    // screenshot is always checked against the current reference.
    QImage checkedFrame_;
    Result checkedResult_;
    QImage first_;
    QImage leftFill_;
    QImage rightFill_;
    QRect viewport_;
    int tailStart_ = 0;
    int height_ = 0;
    int frames_ = 0;
    qint64 position_ = 0;
    int maxHeight_;
    qint64 maxPixels_;
};

class ScrollCaptureWindow : public QWidget, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit ScrollCaptureWindow(const QRect &region);
    ~ScrollCaptureWindow() override;
    void completeCopy(bool successful);
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
  signals:
    void completed(Document document);
    void canceled();
    void copyRequested(Document document);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    friend int runSelfTests(const QString &directory, bool native);
    struct Sample {
        QImage frame;
        QImage preview;
        ScrollStitcher::Result result;
        bool stable = false;
        bool processed = false;
    };
    void sample();
    void receiveSample();
    void scheduleSample();
    void pause(const QString &message);
    void finish();
    void copy();
    void requestFinish(bool copy);
    void deliverResult();
    void tryCopy();
    void stopPresentation();
    void updateProgress(const QImage &preview);
    void placePanel();
    QRect region_;
    Native::ScrollTarget target_;
    std::shared_ptr<ScrollStitcher> stitcher_;
    QFutureWatcher<Sample> watcher_;
    QTimer timer_;
    QElapsedTimer sampleTime_;
    QElapsedTimer uncertainTime_;
    QElapsedTimer finishTime_;
    QElapsedTimer clipboardTime_;
    QImage candidate_;
    Document pendingCopy_;
    QLabel *preview_ = nullptr;
    QLabel *progress_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *finishButton_ = nullptr;
    QPointer<QWidget> mask_;
    bool busy_ = false;
    bool paused_ = false;
    bool closed_ = false;
    bool finishPending_ = false;
    bool copyPending_ = false;
    bool copyHotkeyRegistered_ = false;
    bool captureExcluded_ = false;
    bool viewportResolved_ = false;
};
