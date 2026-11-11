#include "scrollcapture.h"
#include "appearance.h"
#include "settings.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <windows.h>

namespace {
constexpr int copyHotkeyId = 0x5e71;

class ScrollCaptureMask final : public QWidget {
  public:
    explicit ScrollCaptureMask(const QRect &selection)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus), selection_(selection) {
        setObjectName("scrollCaptureMask");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        updateScreens();
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            connect(screen, &QScreen::geometryChanged, this, [this] { updateScreens(); });
            updateScreens();
        });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this] { updateScreens(); });
        for (auto *screen : QGuiApplication::screens())
            connect(screen, &QScreen::geometryChanged, this, [this] { updateScreens(); });
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 120));
        painter.setPen(QPen(Ui::accent(), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selection_.translated(-geometry().topLeft()).adjusted(-1, -1, 1, 1));
    }

  private:
    void updateScreens() {
        QRect desktop;
        for (auto *screen : QGuiApplication::screens())
            desktop = desktop.united(screen->geometry());
        setGeometry(desktop);
        // A native window-region hole leaves the captured pixels and the
        // target's mouse input completely exposed, including on older Windows.
        setMask(QRegion(rect()) - QRegion(selection_.translated(-desktop.topLeft())));
        update();
    }
    QRect selection_;
};
} // namespace

ScrollCaptureWindow::ScrollCaptureWindow(const QRect &region)
    : QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint), region_(region),
      stitcher_(std::make_shared<ScrollStitcher>()) {
    setObjectName("scrollCaptureWindow");
    setWindowTitle(QStringLiteral("滚动长截屏"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(uiPixels(16), uiPixels(16), uiPixels(16), uiPixels(16));
    layout->setSpacing(uiPixels(10));
    progress_ = new QLabel(QStringLiteral("正在准备首帧…"));
    progress_->setObjectName("scrollProgress");
    layout->addWidget(progress_);
    preview_ = new QLabel;
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFixedHeight(uiPixels(200));
    preview_->setObjectName("scrollPreview");
    layout->addWidget(preview_);
    status_ = new QLabel(QStringLiteral("下滚延长，上滚缩短。Ctrl+C 复制当前范围。"));
    status_->setWordWrap(true);
    status_->setMinimumHeight(uiPixels(65));
    layout->addWidget(status_);
    auto *pauseButton = new QPushButton(QStringLiteral("暂停采集"));
    pauseButton->setObjectName("scrollPause");
    layout->addWidget(pauseButton);
    connect(pauseButton, &QPushButton::clicked, this, [this, pauseButton] {
        if (paused_) {
            paused_ = false;
            candidate_ = {};
            uncertainTime_.invalidate();
            status_->setText(QStringLiteral("已恢复采集，可上下滚动调整范围。"));
            pauseButton->setText(QStringLiteral("暂停采集"));
            timer_.start(60);
        } else {
            pause(QStringLiteral("已暂停。恢复后将从最后成功拼接的位置继续。"));
        }
    });
    copyButton_ = new QPushButton(Ui::commandIcon("copy"), QStringLiteral("复制 · Ctrl+C"));
    copyButton_->setObjectName("primary");
    copyButton_->setEnabled(false);
    copyButton_->setToolTip(QStringLiteral("复制当前滚动范围并结束截屏"));
    layout->addWidget(copyButton_);
    connect(copyButton_, &QPushButton::clicked, this, &ScrollCaptureWindow::copy);
    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(QStringLiteral("取消"));
    cancel->setObjectName("scrollCancel");
    finishButton_ = new QPushButton(QStringLiteral("完成并编辑"));
    finishButton_->setObjectName("scrollFinish");
    finishButton_->setEnabled(false);
    buttons->addWidget(cancel);
    buttons->addWidget(finishButton_);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QWidget::close);
    connect(finishButton_, &QPushButton::clicked, this, &ScrollCaptureWindow::finish);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
    auto *enter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(enter, &QShortcut::activated, this, &ScrollCaptureWindow::finish);
    auto *copyKey = new QShortcut(QKeySequence::Copy, this);
    copyKey->setContext(Qt::ApplicationShortcut);
    connect(copyKey, &QShortcut::activated, this, &ScrollCaptureWindow::copy);
    setFixedWidth(uiPixels(300));
    adjustSize();
    placePanel();
    mask_ = new ScrollCaptureMask(region_);
    if (QGuiApplication::platformName() == "windows")
        captureExcluded_ = Native::excludeFromCapture(quintptr(winId()));
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &ScrollCaptureWindow::sample);
    connect(&watcher_, &QFutureWatcher<Sample>::finished, this, &ScrollCaptureWindow::receiveSample);
    timer_.start(180);
}

ScrollCaptureWindow::~ScrollCaptureWindow() {
    // The worker owns its shared stitcher until the in-flight operation ends.
    // No worker captures this window or accesses Qt widgets.
    timer_.stop();
    stopPresentation();
    delete mask_.data();
}

void ScrollCaptureWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (closed_ || testAttribute(Qt::WA_DontShowOnScreen))
        return;
    mask_->show();
    mask_->raise();
    raise();
    if (QGuiApplication::platformName() == "windows" && !copyHotkeyRegistered_) {
        copyHotkeyRegistered_ = RegisterHotKey(nullptr, copyHotkeyId, MOD_CONTROL | MOD_NOREPEAT, 'C');
        if (copyHotkeyRegistered_)
            qApp->installNativeEventFilter(this);
        else
            copyButton_->setToolTip(QStringLiteral("全局 Ctrl+C 已被占用，可点击复制或在面板内按 Ctrl+C。"));
    }
}

bool ScrollCaptureWindow::nativeEventFilter(const QByteArray &, void *message, long *result) {
    const auto *event = static_cast<MSG *>(message);
    if (!copyHotkeyRegistered_ || closed_ || event->message != WM_HOTKEY || int(event->wParam) != copyHotkeyId)
        return false;
    if (result)
        *result = 0;
    QTimer::singleShot(0, this, &ScrollCaptureWindow::copy);
    return true;
}

void ScrollCaptureWindow::stopPresentation() {
    if (mask_)
        mask_->hide();
    if (copyHotkeyRegistered_) {
        UnregisterHotKey(nullptr, copyHotkeyId);
        qApp->removeNativeEventFilter(this);
        copyHotkeyRegistered_ = false;
    }
}

void ScrollCaptureWindow::placePanel() {
    QScreen *screen = QGuiApplication::screenAt(region_.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();
    const int gap = uiPixels(16);
    const QVector<QPoint> positions{
        QPoint(region_.right() + gap, region_.top()),
        QPoint(region_.left() - width() - gap, region_.top()),
        QPoint(region_.left(), region_.bottom() + gap),
        QPoint(region_.left(), region_.top() - height() - gap)};
    for (const auto &position : positions) {
        const QRect candidate(position, size());
        if (available.adjusted(gap, gap, -gap, -gap).contains(candidate)) {
            move(position);
            return;
        }
    }
    move(available.right() - width() - gap, available.top() + gap);
}

void ScrollCaptureWindow::sample() {
    if (closed_ || paused_ || busy_)
        return;
    busy_ = true;
    sampleTime_.start();
    // Include the native frame and its shadow when deciding whether to hide.
    const bool overlaps = (!captureExcluded_ || !target_.window) &&
        frameGeometry().adjusted(-16, -16, 16, 16).intersects(region_);
    if (overlaps)
        hide();
    QTimer::singleShot(overlaps ? 100 : 0, this, [this] {
        if (closed_)
            return;
        if (!target_.window)
            target_ = Native::scrollTarget(region_);
        const quintptr ignoredWindow = captureExcluded_ ? quintptr(winId()) : 0;
        if (!Native::scrollTargetValid(target_, region_, ignoredWindow)) {
            busy_ = false;
            pause(QStringLiteral("目标窗口被遮挡、移动或关闭。恢复窗口后继续，或完成已采集部分。"));
            if (finishPending_)
                deliverResult();
            return;
        }
        const QRect region = region_;
        const auto target = target_;
        const QImage candidate = candidate_;
        const auto stitcher = stitcher_;
        const bool resolveViewport = !viewportResolved_;
        viewportResolved_ = true;
        const QSize previewSize(uiPixels(260), uiPixels(200));
        watcher_.setFuture(QtConcurrent::run([region, target, ignoredWindow, candidate, stitcher, previewSize, resolveViewport] {
            Sample sample;
            if (!Native::scrollTargetValid(target, region, ignoredWindow))
                return sample;
            if (resolveViewport) {
                const QRect viewport = Native::scrollingViewport(target.root, target.point).intersected(region);
                if (viewport.width() >= 80 && viewport.height() >= 100)
                    stitcher->setViewport(viewport.translated(-region.topLeft()));
            }
            sample.frame = Native::captureRegion(region);
            if (!Native::scrollTargetValid(target, region, ignoredWindow)) {
                sample.frame = {};
                return sample;
            }
            sample.stable = ScrollStitcher::sameFrame(candidate, sample.frame, stitcher->viewport());
            // After the initial stable reference, a coherent moving frame can
            // be committed by geometric overlap. Waiting for scrolling to stop
            // would discard intermediate frames and accumulate a capture gap.
            if (sample.stable || stitcher->frameCount() > 0) {
                sample.processed = true;
                sample.result = stitcher->append(sample.frame);
                if (sample.result.status == ScrollStitcher::Status::FirstFrame ||
                    sample.result.status == ScrollStitcher::Status::Appended ||
                    sample.result.status == ScrollStitcher::Status::Retracted)
                    sample.preview = stitcher->preview(previewSize);
            }
            return sample;
        }));
    });
}

void ScrollCaptureWindow::receiveSample() {
    busy_ = false;
    if (closed_)
        return;
    const Sample result = watcher_.result();
    candidate_ = result.frame;
    show();
    if (mask_ && mask_->isVisible()) {
        mask_->raise();
        raise();
    }
    if (!result.preview.isNull())
        updateProgress(result.preview);
    if (finishPending_) {
        if (result.stable || (result.processed && (result.result.status == ScrollStitcher::Status::Appended ||
                                                 result.result.status == ScrollStitcher::Status::Retracted)) ||
            result.frame.isNull() || paused_ || finishTime_.elapsed() >= 1500)
            deliverResult();
        else
            scheduleSample();
        return;
    }
    if (paused_)
        return;
    if (result.frame.isNull()) {
        pause(QStringLiteral("无法读取截图区域。可完成已采集部分，或恢复后重试。"));
        return;
    }
    if (!result.processed) {
        scheduleSample();
        return;
    }
    using Status = ScrollStitcher::Status;
    switch (result.result.status) {
    case Status::FirstFrame:
    case Status::Appended:
        uncertainTime_.invalidate();
        status_->setText(QStringLiteral("下滚延长，上滚缩短。保持约三分之一内容重叠，Ctrl+C 复制当前范围。"));
        break;
    case Status::Retracted:
        uncertainTime_.invalidate();
        status_->setText(stitcher_->size().height() == region_.height()
            ? QStringLiteral("已回缩到首屏范围。向下滚动可继续延长，Ctrl+C 复制。")
            : QStringLiteral("已随上滚缩短范围。可继续调整，Ctrl+C 复制当前范围。"));
        break;
    case Status::Unchanged:
        if (uncertainTime_.isValid())
            status_->setText(QStringLiteral("已回到上次采集位置，可继续向下滚动。"));
        uncertainTime_.invalidate();
        break;
    case Status::Uncertain:
        if (!uncertainTime_.isValid())
            uncertainTime_.start();
        if (result.stable && uncertainTime_.elapsed() >= 400)
            status_->setText(QStringLiteral("当前画面尚未对齐。向上回到已采集内容后会自动重新定位，并缩短截图范围。"));
        else
            status_->setText(QStringLiteral("正在校验滚动画面…"));
        break;
    case Status::LimitReached:
        if (stitcher_->frameCount() == 0) {
            pause(QStringLiteral("选区超过图像上限，请重新选择较小的区域。"));
            return;
        }
        status_->setText(QStringLiteral("已达到长图上限。向上滚动可缩短范围，Ctrl+C 复制已采集部分。"));
        break;
    case Status::Invalid:
        pause(QStringLiteral("截图尺寸发生变化或选区过小，请重新选择滚动区域。"));
        return;
    }
    scheduleSample();
}

void ScrollCaptureWindow::scheduleSample() {
    // Target a 60 ms start-to-start interval, including capture and matching
    // work. Slow frames never create an unbounded queue of stale screenshots.
    timer_.start(qMax(1, 60 - int(sampleTime_.elapsed())));
}

void ScrollCaptureWindow::updateProgress(const QImage &preview) {
    const QSize size = stitcher_->size();
    progress_->setText(QStringLiteral("%1 × %2 像素 · %3 段")
        .arg(size.width()).arg(size.height()).arg(stitcher_->frameCount()));
    preview_->setPixmap(QPixmap::fromImage(preview));
    finishButton_->setEnabled(!finishPending_);
    copyButton_->setEnabled(!finishPending_);
}

void ScrollCaptureWindow::pause(const QString &message) {
    paused_ = true;
    timer_.stop();
    findChild<QPushButton *>("scrollPause")->setText(QStringLiteral("恢复采集"));
    status_->setText(message);
    show();
}

void ScrollCaptureWindow::finish() {
    requestFinish(false);
}

void ScrollCaptureWindow::copy() {
    requestFinish(true);
}

void ScrollCaptureWindow::requestFinish(bool copy) {
    if (closed_ || finishPending_)
        return;
    timer_.stop();
    copyPending_ = copy;
    finishPending_ = true;
    finishTime_.start();
    copyButton_->setEnabled(false);
    findChild<QPushButton *>("scrollPause")->setEnabled(false);
    finishButton_->setEnabled(false);
    status_->setText(copy ? QStringLiteral("正在复制当前范围…") : QStringLiteral("正在完成当前帧…"));
    if (!busy_ && paused_)
        deliverResult();
    else if (!busy_)
        sample();
}

void ScrollCaptureWindow::deliverResult() {
    Document document;
    document.image = stitcher_->image();
    if (document.image.isNull()) {
        finishPending_ = false;
        copyPending_ = false;
        copyButton_->setEnabled(stitcher_->frameCount() > 0);
        findChild<QPushButton *>("scrollPause")->setEnabled(true);
        finishButton_->setEnabled(stitcher_->frameCount() > 0);
        pause(QStringLiteral("尚无可用图像或图像分配失败。请恢复采集后重试。"));
        return;
    }
    if (copyPending_) {
        pendingCopy_ = document;
        clipboardTime_.start();
        tryCopy();
        return;
    }
    finishPending_ = false;
    closed_ = true;
    stopPresentation();
    hide();
    emit completed(document);
    close();
}

void ScrollCaptureWindow::tryCopy() {
    if (closed_ || !finishPending_ || !copyPending_)
        return;
    if (QGuiApplication::platformName() == "windows") {
        if (!OpenClipboard(nullptr)) {
            if (clipboardTime_.elapsed() < 2000)
                QTimer::singleShot(25, this, &ScrollCaptureWindow::tryCopy);
            else
                completeCopy(false);
            return;
        }
        CloseClipboard();
    }
    emit copyRequested(pendingCopy_);
}

void ScrollCaptureWindow::completeCopy(bool successful) {
    if (closed_ || !copyPending_)
        return;
    finishPending_ = false;
    copyPending_ = false;
    pendingCopy_ = {};
    if (successful) {
        closed_ = true;
        stopPresentation();
        close();
    } else {
        copyButton_->setEnabled(stitcher_->frameCount() > 0);
        finishButton_->setEnabled(stitcher_->frameCount() > 0);
        findChild<QPushButton *>("scrollPause")->setEnabled(true);
        pause(QStringLiteral("复制未完成，当前范围已保留。请稍后按 Ctrl+C 重试。"));
    }
}

void ScrollCaptureWindow::closeEvent(QCloseEvent *event) {
    stopPresentation();
    if (!closed_)
        emit canceled();
    closed_ = true;
    timer_.stop();
    QWidget::closeEvent(event);
}
