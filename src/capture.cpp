#include "capture.h"
#include <QtWidgets>
#include <QtConcurrent>

CaptureOverlay::CaptureOverlay(Native::Desktop desktop, Settings &settings, const QRect &initial)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      desktop_(std::move(desktop)), settings_(settings) {
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setGeometry(desktop_.geometry);
    cursor_ = settings_.value("capture/cursor", false).toBool();
    selection_ = initial.intersected(desktop_.geometry).translated(-desktop_.geometry.topLeft());
    if (initial.isEmpty())
        selection_ = {};
    mouse_ = QCursor::pos() - desktop_.geometry.topLeft();
    detectedElements_ = Native::windowElements(desktop_.geometry);
    detectionAnimation_.setDuration(100);
    detectionAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&detectionAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        animatedHover_ = value.toRect();
        update();
    });
    toolbar_ = new QFrame(this);
    toolbar_->setStyleSheet("QFrame { background:#172131; border:1px solid #41516a; border-radius:8px; } "
                            "QPushButton { padding:7px 10px; }");
    auto *layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(8, 8, 8, 8);
    auto button = [&](const QString &label, const QString &tip, const std::function<void()> &callback) {
        auto *widget = new QPushButton(label);
        widget->setToolTip(tip);
        widget->setFocusPolicy(Qt::NoFocus);
        connect(widget, &QPushButton::clicked, this, callback);
        layout->addWidget(widget);
        return widget;
    };
    button(QStringLiteral("全屏"), "F", [this] {
        selection_ = rect();
        update();
    });
    button(QStringLiteral("当前屏"), "S", [this] {
        for (const auto &screen : desktop_.screens)
            if (screen.contains(QCursor::pos()))
                selection_ = screen.translated(-desktop_.geometry.topLeft());
        update();
    });
    button(QStringLiteral("尺寸 / 比例"), "D", [this] { setDimensions(); });
    auto *element =
        button(QStringLiteral("元素"), QStringLiteral("Tab 切换窗口 / 元素检测，滚轮选择层级"), [this] {
            elements_ = !elements_;
            elementRects_.clear();
            updateDetection();
        });
    element->setCheckable(true);
    element->setObjectName("elementToggle");
    auto *pointer = button(QStringLiteral("指针"), "C", [this] {
        cursor_ = !cursor_;
        update();
    });
    pointer->setCheckable(true);
    pointer->setChecked(cursor_);
    pointer->setObjectName("pointerToggle");
    button(QStringLiteral("刷新"), QStringLiteral("R 刷新，Shift+R 连续刷新 / 定格"), [this] { refresh(); });
    button(QStringLiteral("取消"), "Esc", [this] { cancel(); });
    auto *accept =
        button(QStringLiteral("标注 / 输出 →"), QStringLiteral("Enter 或双击选区"), [this] { finish(); });
    accept->setObjectName("primary");
    toolbar_->adjustSize();
    placeToolbar();
    detectionTimer_.setInterval(150);
    connect(&detectionTimer_, &QTimer::timeout, this, &CaptureOverlay::updateDetection);
    detectionTimer_.start();
    connect(&refreshTimer_, &QTimer::timeout, this, &CaptureOverlay::refresh);
    connect(&elementsWatcher_, &QFutureWatcher<QVector<QRect>>::finished, this, [this] {
        if ((QCursor::pos() - elementPoint_).manhattanLength() > 12 || !selection_.isEmpty())
            return;
        elementRects_ = elementsWatcher_.result();
        for (const auto &element : elementRects_)
            if (!detectedElements_.contains(element))
                detectedElements_.append(element);
        elementIndex_ = 0;
        if (!elementRects_.isEmpty())
            setHover(elementRects_.first().translated(-desktop_.geometry.topLeft()).intersected(rect()));
        update();
    });
}

QRect CaptureOverlay::selectedGlobal() const {
    return selection_.translated(desktop_.geometry.topLeft());
}
QImage CaptureOverlay::backgroundImage() const {
    QImage image = desktop_.image;
    if (cursor_ && !desktop_.cursorImage.isNull()) {
        QPainter painter(&image);
        painter.drawImage(desktop_.cursorPosition - desktop_.geometry.topLeft() - desktop_.cursorHotspot,
                          desktop_.cursorImage);
    }
    return image;
}
Document CaptureOverlay::selectionDocument() const {
    Document document;
    if (!selection_.isEmpty())
        document.image = backgroundImage().copy(selection_.intersected(desktop_.image.rect()));
    document.cornerRadius = radius_;
    if (elements_)
        for (const auto &element : detectedElements_) {
            const QRect clipped = element.intersected(selectedGlobal());
            if (!clipped.isEmpty())
                document.detectedElements.append(clipped.translated(-selectedGlobal().topLeft()));
        }
    return document;
}
void CaptureOverlay::finish() {
    if (selection_.isEmpty())
        selection_ = hover_;
    if (selection_.width() < 1 || selection_.height() < 1)
        return;
    done_ = true;
    refreshTimer_.stop();
    detectionTimer_.stop();
    hide();
    emit captured(selectionDocument(), selectedGlobal());
    close();
}
void CaptureOverlay::cancel() {
    if (done_)
        return;
    done_ = true;
    refreshTimer_.stop();
    detectionTimer_.stop();
    hide();
    emit canceled(selectionDocument());
    close();
}
void CaptureOverlay::placeToolbar() {
    QRect screen = rect();
    for (const auto &item : desktop_.screens)
        if (item.contains(QCursor::pos()))
            screen = item.translated(-desktop_.geometry.topLeft());
    toolbar_->move(screen.center().x() - toolbar_->width() / 2, screen.bottom() - toolbar_->height() - 28);
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.drawImage(0, 0, backgroundImage());
    const QRect target = selection_.isEmpty() ? animatedHover_ : selection_;
    QPainterPath mask;
    mask.addRect(rect());
    QPainterPath hole;
    if (!target.isEmpty())
        hole.addRoundedRect(target, radius_, radius_);
    painter.fillPath(mask.subtracted(hole), QColor(4, 10, 20, 140));
    painter.setPen(QPen(QColor("#54d6b0"), 1));
    painter.setBrush(Qt::NoBrush);
    if (!target.isEmpty()) {
        painter.drawRoundedRect(target.adjusted(0, 0, -1, -1), radius_, radius_);
        const int threshold = settings_.value("capture/anchorThreshold", 20).toInt();
        if (!selection_.isEmpty() && target.width() > threshold && target.height() > threshold) {
            painter.setBrush(QColor("#54d6b0"));
            for (const QPoint &point :
                 {target.topLeft(), target.topRight(), target.bottomLeft(), target.bottomRight(),
                  QPoint(target.center().x(), target.top()), QPoint(target.center().x(), target.bottom()),
                  QPoint(target.left(), target.center().y()), QPoint(target.right(), target.center().y())})
                painter.drawRect(QRect(point - QPoint(3, 3), QSize(6, 6)));
        }
        const QString text = QStringLiteral("%1 × %2 px   (%3, %4)%5")
                                 .arg(target.width())
                                 .arg(target.height())
                                 .arg(target.x() + desktop_.geometry.x())
                                 .arg(target.y() + desktop_.geometry.y())
                                 .arg(typed_.isEmpty() ? "" : "   " + typed_);
        const QRect label(target.left(), qMax(5, target.top() - 33),
                          qMax(240, painter.fontMetrics().horizontalAdvance(text) + 20), 27);
        painter.fillRect(label, QColor("#172131"));
        painter.setPen(Qt::white);
        painter.drawText(label.adjusted(8, 0, -6, 0), Qt::AlignVCenter, text);
    }
    QRect currentScreen = rect();
    for (const auto &screen : desktop_.screens)
        if (screen.contains(QCursor::pos()))
            currentScreen = screen.translated(-desktop_.geometry.topLeft());
    const QRect help(currentScreen.left() + 20, currentScreen.top() + 20,
                     qMin(1000, currentScreen.width() - 40), 36);
    painter.fillRect(help, QColor(16, 25, 39, 225));
    painter.setPen(Qt::white);
    painter.drawText(help.adjusted(12, 0, -10, 0), Qt::AlignVCenter,
                     QStringLiteral("拖动选区 · 单击匹配窗口 · Tab 元素检测 · 空格移动 · 方向键微调 · D 尺寸 "
                                    "· P 取色 · Enter 标注 · Esc 取消%1")
                         .arg(refreshTimer_.isActive() ? QStringLiteral("  [连续刷新]") : ""));
    if (settings_.value("magnifier/enabled", true).toBool() && desktop_.image.rect().contains(mouse_) &&
        !toolbar_->geometry().contains(mouse_)) {
        const int zoom = settings_.value("magnifier/zoom", 8).toInt();
        const int side = 17;
        QPoint position = mouse_ + QPoint(28, 32);
        const QSize box(side * zoom, side * zoom + 50);
        position.setX(qBound(currentScreen.left(), position.x(),
                             qMax(currentScreen.left(), currentScreen.right() - box.width())));
        position.setY(qBound(currentScreen.top(), position.y(),
                             qMax(currentScreen.top(), currentScreen.bottom() - box.height())));
        const QRect lens(position, QSize(side * zoom, side * zoom));
        const QImage patch =
            desktop_.image.copy(QRect(mouse_ - QPoint(side / 2, side / 2), QSize(side, side)));
        painter.fillRect(QRect(position, box), QColor("#172131"));
        painter.save();
        if (settings_.value("magnifier/style", "square").toString() == "round") {
            QPainterPath circle;
            circle.addEllipse(lens);
            painter.setClipPath(circle);
        }
        painter.drawImage(lens, patch);
        if (settings_.value("magnifier/grid", true).toBool()) {
            painter.setPen(QColor(0, 0, 0, 90));
            for (int i = 1; i < side; ++i) {
                painter.drawLine(lens.left() + i * zoom, lens.top(), lens.left() + i * zoom, lens.bottom());
                painter.drawLine(lens.left(), lens.top() + i * zoom, lens.right(), lens.top() + i * zoom);
            }
        }
        painter.setPen(QPen(QColor("#ff536b"), 2));
        painter.drawRect(QRect(position + QPoint(side / 2 * zoom, side / 2 * zoom), QSize(zoom, zoom)));
        painter.restore();
        painter.setPen(Qt::white);
        const QString value =
            colorText(desktop_.image.pixelColor(mouse_), settings_.value("color/format", "HEX").toString(),
                      settings_.value("color/uppercase", true).toBool());
        painter.drawText(QRect(position + QPoint(4, lens.height()), QSize(lens.width() - 8, 48)),
                         Qt::AlignCenter, value + QStringLiteral("\nP 复制 · V 切换格式"));
    }
}
int CaptureOverlay::handleAt(const QPoint &point) const {
    if (selection_.isEmpty())
        return -1;
    const QVector<QPoint> handles{
        selection_.topLeft(),     QPoint(selection_.center().x(), selection_.top()),
        selection_.topRight(),    QPoint(selection_.right(), selection_.center().y()),
        selection_.bottomRight(), QPoint(selection_.center().x(), selection_.bottom()),
        selection_.bottomLeft(),  QPoint(selection_.left(), selection_.center().y())};
    for (int i = 0; i < handles.size(); ++i)
        if ((handles[i] - point).manhattanLength() < 12)
            return i;
    return -1;
}
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        if (!selection_.isEmpty()) {
            selection_ = {};
            update();
        } else
            cancel();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    pressed_ = true;
    anchor_ = previous_ = event->pos();
    handle_ = handleAt(event->pos());
    moving_ = handle_ < 0 && selection_.contains(event->pos());
    if (!moving_ && handle_ < 0)
        selection_ = {};
}
void CaptureOverlay::constrainSelection() {
    selection_ = selection_.normalized().intersected(rect());
    if (ratio_ > 0 && !selection_.isEmpty()) {
        int width = selection_.width();
        int height = qMax(1, qRound(width / ratio_));
        if (height > selection_.height()) {
            height = selection_.height();
            width = qMax(1, qRound(height * ratio_));
        }
        selection_.setSize(QSize(width, height));
    }
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *event) {
    mouse_ = event->pos();
    if (pressed_) {
        const QPoint delta = event->pos() - previous_;
        if (space_ || moving_) {
            selection_.translate(delta);
            anchor_ += delta;
            selection_.moveLeft(qBound(0, selection_.left(), qMax(0, width() - selection_.width())));
            selection_.moveTop(qBound(0, selection_.top(), qMax(0, height() - selection_.height())));
        } else if (handle_ >= 0) {
            QRect next = selection_;
            if (handle_ == 0 || handle_ == 6 || handle_ == 7)
                next.setLeft(event->x());
            if (handle_ == 0 || handle_ == 1 || handle_ == 2)
                next.setTop(event->y());
            if (handle_ == 2 || handle_ == 3 || handle_ == 4)
                next.setRight(event->x());
            if (handle_ == 4 || handle_ == 5 || handle_ == 6)
                next.setBottom(event->y());
            selection_ = next.normalized();
            if (ratio_ > 0)
                selection_.setHeight(qMax(1, qRound(selection_.width() / ratio_)));
            constrainSelection();
        } else {
            QPoint end = event->pos();
            const double ratio = event->modifiers().testFlag(Qt::ShiftModifier) ? 1.0 : ratio_;
            if (ratio > 0)
                end.setY(anchor_.y() +
                         (end.y() >= anchor_.y() ? 1 : -1) * qRound(qAbs(end.x() - anchor_.x()) / ratio));
            selection_ = QRect(anchor_, end).normalized();
            constrainSelection();
        }
        previous_ = event->pos();
    } else
        setCursor(selection_.contains(mouse_) ? Qt::SizeAllCursor : Qt::CrossCursor);
    update();
}
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    pressed_ = false;
    if (!moving_ && handle_ < 0 && (event->pos() - anchor_).manhattanLength() < 4)
        selection_ = hover_;
    constrainSelection();
    update();
}
void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *) {
    finish();
}
void CaptureOverlay::keyPressEvent(QKeyEvent *event) {
    int key = event->key();
    if (settings_.value("input/wasd", false).toBool()) {
        if (key == Qt::Key_W)
            key = Qt::Key_Up;
        if (key == Qt::Key_A)
            key = Qt::Key_Left;
        if (key == Qt::Key_S)
            key = Qt::Key_Down;
        if (key == Qt::Key_D)
            key = Qt::Key_Right;
    }
    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (key == Qt::Key_Space) {
        space_ = true;
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!typed_.isEmpty()) {
            const auto dimensions = typed_.split('x');
            if (dimensions.size() == 2) {
                if (selection_.isEmpty())
                    selection_.moveTopLeft(mouse_);
                selection_.setSize(QSize(qBound(1, dimensions[0].toInt(), width()),
                                         qBound(1, dimensions[1].toInt(), height())));
                constrainSelection();
            }
            typed_.clear();
            update();
        } else
            finish();
        return;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9 && typed_.size() < 13) {
        typed_ += QChar('0' + key - Qt::Key_0);
        update();
        return;
    }
    if (key == Qt::Key_X && !typed_.contains('x')) {
        typed_ += 'x';
        update();
        return;
    }
    if (key == Qt::Key_Backspace) {
        typed_.chop(1);
        update();
        return;
    }
    if (key == Qt::Key_F)
        selection_ = rect();
    if (key == Qt::Key_S)
        for (const auto &screen : desktop_.screens)
            if (screen.contains(QCursor::pos()))
                selection_ = screen.translated(-desktop_.geometry.topLeft());
    if (key == Qt::Key_Tab) {
        elements_ = !elements_;
        findChild<QPushButton *>("elementToggle")->setChecked(elements_);
        elementRects_.clear();
        updateDetection();
    }
    if (key == Qt::Key_C) {
        cursor_ = !cursor_;
        findChild<QPushButton *>("pointerToggle")->setChecked(cursor_);
    }
    if (key == Qt::Key_D)
        setDimensions();
    if (key == Qt::Key_R) {
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (refreshTimer_.isActive())
                refreshTimer_.stop();
            else
                refreshTimer_.start(settings_.value("capture/refreshMs", 1000).toInt());
        } else
            refresh();
    }
    if (key == Qt::Key_V) {
        const QStringList formats{"HEX", "RGB", "HSV"};
        settings_.set("color/format",
                      formats[(formats.indexOf(settings_.value("color/format", "HEX").toString()) + 1) % 3]);
    }
    if (key == Qt::Key_P && desktop_.image.rect().contains(mouse_))
        QApplication::clipboard()->setText(colorText(desktop_.image.pixelColor(mouse_),
                                                     settings_.value("color/format", "HEX").toString(),
                                                     settings_.value("color/uppercase", true).toBool()));
    QPoint step;
    if (key == Qt::Key_Left)
        step.rx() = -1;
    if (key == Qt::Key_Right)
        step.rx() = 1;
    if (key == Qt::Key_Up)
        step.ry() = -1;
    if (key == Qt::Key_Down)
        step.ry() = 1;
    if (!step.isNull()) {
        if (selection_.isEmpty() || event->modifiers().testFlag(Qt::AltModifier))
            QCursor::setPos(QCursor::pos() + step);
        else if (event->modifiers().testFlag(Qt::ShiftModifier))
            selection_.setBottomRight(selection_.bottomRight() + step);
        else if (event->modifiers().testFlag(Qt::ControlModifier))
            selection_.setTopLeft(selection_.topLeft() + step);
        else {
            selection_.translate(step);
            selection_.moveLeft(qBound(0, selection_.left(), qMax(0, width() - selection_.width())));
            selection_.moveTop(qBound(0, selection_.top(), qMax(0, height() - selection_.height())));
        }
        constrainSelection();
    }
    update();
}
void CaptureOverlay::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space)
        space_ = false;
}
void CaptureOverlay::wheelEvent(QWheelEvent *event) {
    if (elements_ && selection_.isEmpty() && !elementRects_.isEmpty()) {
        elementIndex_ =
            qBound(0, elementIndex_ + (event->angleDelta().y() > 0 ? 1 : -1), elementRects_.size() - 1);
        setHover(elementRects_[elementIndex_].translated(-desktop_.geometry.topLeft()).intersected(rect()));
    } else if (event->modifiers().testFlag(Qt::ControlModifier))
        settings_.set(
            "magnifier/zoom",
            qBound(2, settings_.value("magnifier/zoom", 8).toInt() + (event->angleDelta().y() > 0 ? 1 : -1),
                   24));
    update();
}
bool CaptureOverlay::event(QEvent *event) {
    if (event->type() == QEvent::WindowDeactivate && !refreshing_ && !done_ &&
        !QApplication::activeModalWidget() && settings_.value("capture/cancelOnDeactivate", false).toBool())
        QTimer::singleShot(0, this, [this] {
            if (!refreshing_ && !QApplication::activeModalWidget())
                cancel();
        });
    return QWidget::event(event);
}
void CaptureOverlay::updateDetection() {
    if (done_ || refreshing_ || !selection_.isEmpty() || pressed_)
        return;
    const QPoint point = QCursor::pos();
    if (toolbar_->geometry().contains(mapFromGlobal(point)))
        return;
    const QStringList excluded =
        settings_.value("capture/excluded").toString().split(';', Qt::SkipEmptyParts);
    if (elements_) {
        if (!elementsWatcher_.isRunning() &&
            (elementRects_.isEmpty() || (point - elementPoint_).manhattanLength() > 8)) {
            elementPoint_ = point;
            const auto nativePrograms =
                settings_.value("capture/nativeElementsFor").toString().split(';', Qt::SkipEmptyParts);
            elementsWatcher_.setFuture(QtConcurrent::run([point, excluded, nativePrograms] {
                return Native::elementsAt(point, excluded, nativePrograms);
            }));
        }
    } else
        setHover(
            Native::windowAt(point, excluded).translated(-desktop_.geometry.topLeft()).intersected(rect()));
    update();
}
void CaptureOverlay::setHover(const QRect &rectangle) {
    if (rectangle == hover_)
        return;
    hover_ = rectangle;
    detectionAnimation_.stop();
    if (settings_.value("capture/animateDetection", true).toBool() && !animatedHover_.isEmpty() &&
        !hover_.isEmpty()) {
        detectionAnimation_.setStartValue(animatedHover_);
        detectionAnimation_.setEndValue(hover_);
        detectionAnimation_.start();
    } else
        animatedHover_ = hover_;
    update();
}
void CaptureOverlay::refresh() {
    if (refreshing_ || done_)
        return;
    refreshing_ = true;
    hide();
    QTimer::singleShot(90, this, [this] {
        if (done_)
            return;
        auto fresh = Native::captureDesktop();
        if (!fresh.image.isNull()) {
            desktop_ = std::move(fresh);
            setGeometry(desktop_.geometry);
            constrainSelection();
        }
        show();
        raise();
        activateWindow();
        refreshing_ = false;
        update();
    });
}
void CaptureOverlay::setDimensions() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("精确选区"));
    QFormLayout form(&dialog);
    QSpinBox x, y, width, height, radius;
    x.setRange(desktop_.geometry.left(), desktop_.geometry.right());
    y.setRange(desktop_.geometry.top(), desktop_.geometry.bottom());
    width.setRange(1, this->width());
    height.setRange(1, this->height());
    radius.setRange(0, 500);
    const QRect target = selection_.isEmpty() ? QRect(mouse_, QSize(640, 480)) : selection_;
    x.setValue(target.x() + desktop_.geometry.x());
    y.setValue(target.y() + desktop_.geometry.y());
    width.setValue(target.width());
    height.setValue(target.height());
    radius.setValue(radius_);
    QComboBox ratio;
    ratio.addItems({QStringLiteral("自由"), "1:1", "4:3", "16:9", "9:16", QStringLiteral("按输入尺寸固定")});
    form.addRow("X", &x);
    form.addRow("Y", &y);
    form.addRow(QStringLiteral("宽 / px"), &width);
    form.addRow(QStringLiteral("高 / px"), &height);
    form.addRow(QStringLiteral("圆角 / px"), &radius);
    form.addRow(QStringLiteral("比例"), &ratio);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const double ratios[]{0, 1, 4.0 / 3, 16.0 / 9, 9.0 / 16};
    ratio_ = ratio.currentIndex() < 5 ? ratios[ratio.currentIndex()] : double(width.value()) / height.value();
    selection_ = QRect(x.value() - desktop_.geometry.x(), y.value() - desktop_.geometry.y(), width.value(),
                       height.value());
    if (ratio_ > 0)
        selection_.setHeight(qRound(selection_.width() / ratio_));
    radius_ = radius.value();
    constrainSelection();
    update();
}
