#include "editor.h"
#include "native.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <cmath>
#include <QOpenGLWidget>

namespace {
class AcceleratedPreview final : public QOpenGLWidget {
  public:
    AcceleratedPreview(QWidget *parent, std::function<void(QPainter &)> paint)
        : QOpenGLWidget(parent), paint_(std::move(paint)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

  protected:
    void paintGL() override {
        QPainter painter(this);
        paint_(painter);
    }

  private:
    std::function<void(QPainter &)> paint_;
};
} // namespace

AnnotationCanvas::AnnotationCanvas(Document document, Settings &settings, QWidget *parent)
    : QWidget(parent), document_(std::move(document)), settings_(settings) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    style_.color = settings_.toolColor(toolKey(tool_));
    for (const auto &item : document_.annotations)
        if (item.tool == Tool::Number)
            number_ = qMax(number_, item.number + 1);
    if (settings_.value("runtime/gpuPreview", false).toBool()) {
        gpuPreview_ = new AcceleratedPreview(this, [this](QPainter &painter) { paintContent(painter); });
        QTimer::singleShot(500, this, [this] {
            if (gpuPreview_ && !gpuPreview_->isValid()) {
                gpuPreview_->hide();
                gpuPreview_->deleteLater();
                gpuPreview_ = nullptr;
                qWarning() << "OpenGL preview unavailable, using raster preview";
                update();
            }
        });
    }
    redraw();
}
const Document &AnnotationCanvas::document() const {
    return document_;
}
Tool AnnotationCanvas::tool() const {
    return tool_;
}
int AnnotationCanvas::selectedIndex() const {
    return selected_;
}
void AnnotationCanvas::setTool(Tool tool) {
    finishDrawing();
    tool_ = tool;
    selected_ = -1;
    style_.color = settings_.toolColor(toolKey(tool));
    if (tool == Tool::Highlighter && !settings_.value("colors/" + toolKey(tool)).isValid())
        style_.color.setAlpha(100);
    picker_ = false;
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit selectionChanged();
    update();
    setFocus();
}
Annotation AnnotationCanvas::style() const {
    return selected_ >= 0 ? document_.annotations[selected_] : style_;
}
void AnnotationCanvas::setStyle(const Annotation &style) {
    style_ = style;
    settings_.setToolColor(toolKey(selected_ >= 0 ? document_.annotations[selected_].tool : tool_),
                           style.color);
    if (selected_ >= 0) {
        checkpoint();
        auto &item = document_.annotations[selected_];
        item.color = style.color;
        item.fill = style.fill;
        item.outline = style.outline;
        item.width = style.width;
        item.fontSize = style.fontSize;
        item.outlineWidth = style.outlineWidth;
        item.rotation = style.rotation;
        item.lineStyle = style.lineStyle;
        item.shape = style.shape;
        item.arrowStyle = style.arrowStyle;
        redraw();
    }
}
void AnnotationCanvas::setZoom(double zoom) {
    zoom_ = qBound(0.05, zoom, 8.0);
    setFixedSize(qMax(1, qRound(document_.image.width() * zoom_)),
                 qMax(1, qRound(document_.image.height() * zoom_)));
    if (gpuPreview_)
        gpuPreview_->setGeometry(rect());
    update();
}
double AnnotationCanvas::zoom() const {
    return zoom_;
}
bool AnnotationCanvas::acceleratedPreview() const {
    return gpuPreview_ && gpuPreview_->isValid();
}
void AnnotationCanvas::setBaseImage(const QImage &image) {
    if (image.size() == document_.image.size()) {
        document_.image = image;
        redraw();
    }
}
void AnnotationCanvas::setRadius(int radius) {
    document_.cornerRadius = radius;
    redraw();
}
void AnnotationCanvas::checkpoint() {
    undo_.append(document_.annotations);
    if (undo_.size() > 100)
        undo_.removeFirst();
    redo_.clear();
}
void AnnotationCanvas::redraw() {
    rendered_ = document_.render();
    setZoom(zoom_);
    update();
    emit changed();
}
void AnnotationCanvas::undo() {
    if (undo_.isEmpty())
        return;
    redo_.append(document_.annotations);
    document_.annotations = undo_.takeLast();
    selected_ = -1;
    drawing_ = polyline_ = moving_ = false;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::redo() {
    if (redo_.isEmpty())
        return;
    undo_.append(document_.annotations);
    document_.annotations = redo_.takeLast();
    selected_ = -1;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::clear() {
    if (document_.annotations.isEmpty())
        return;
    checkpoint();
    document_.annotations.clear();
    selected_ = -1;
    drawing_ = polyline_ = false;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::copyObject() {
    if (selected_ < 0)
        return;
    auto *mime = new QMimeData;
    mime->setData("application/x-easysnip-annotation",
                  QJsonDocument(document_.annotations[selected_].toJson()).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
}
void AnnotationCanvas::pasteObject() {
    const auto *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat("application/x-easysnip-annotation"))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(mime->data("application/x-easysnip-annotation"));
    if (!json.isObject())
        return;
    checkpoint();
    auto item = Annotation::fromJson(json.object());
    translate(item, QPointF(16, 16));
    document_.annotations.append(item);
    selected_ = document_.annotations.size() - 1;
    tool_ = Tool::Select;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::deleteObject() {
    if (selected_ < 0)
        return;
    checkpoint();
    document_.annotations.removeAt(selected_);
    selected_ = -1;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::togglePicker() {
    picker_ = !picker_;
    update();
    setFocus();
}
void AnnotationCanvas::finishDrawing() {
    if (polyline_) {
        polyline_ = drawing_ = false;
        selected_ = document_.annotations.size() - 1;
        redraw();
        emit selectionChanged();
    }
}
QPointF AnnotationCanvas::imagePoint(const QPoint &point) const {
    return QPointF(qBound(0.0, point.x() / zoom_, double(document_.image.width() - 1)),
                   qBound(0.0, point.y() / zoom_, double(document_.image.height() - 1)));
}
void AnnotationCanvas::translate(Annotation &item, const QPointF &delta) {
    item.rect.translate(delta);
    for (auto &point : item.points)
        point += delta;
}
int AnnotationCanvas::objectAt(const QPointF &point) const {
    for (int i = document_.annotations.size() - 1; i >= 0; --i) {
        const auto &item = document_.annotations[i];
        QTransform transform;
        transform.translate(item.rect.center().x(), item.rect.center().y());
        transform.rotate(item.rotation);
        transform.translate(-item.rect.center().x(), -item.rect.center().y());
        const QPointF local = transform.inverted().map(point);
        if (!item.points.isEmpty()) {
            QPainterPath path;
            path.moveTo(item.points.first());
            for (int j = 1; j < item.points.size(); ++j)
                path.lineTo(item.points[j]);
            QPainterPathStroker stroke;
            stroke.setWidth(qMax(10.0 / zoom_, item.width + 4));
            if (stroke.createStroke(path).contains(local))
                return i;
        } else if (item.bounds().contains(local))
            return i;
    }
    return -1;
}
void AnnotationCanvas::paintEvent(QPaintEvent *) {
    if (gpuPreview_) {
        gpuPreview_->update();
        return;
    }
    QPainter painter(this);
    paintContent(painter);
}
void AnnotationCanvas::paintContent(QPainter &painter) {
    for (int y = 0; y < height(); y += 16)
        for (int x = 0; x < width(); x += 16)
            painter.fillRect(x, y, 16, 16, ((x / 16 + y / 16) % 2) ? QColor("#303a48") : QColor("#242d3a"));
    painter.scale(zoom_, zoom_);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1);
    painter.drawImage(0, 0, rendered_);
    if (selected_ >= 0 && selected_ < document_.annotations.size() && !drawing_) {
        const auto &item = document_.annotations[selected_];
        painter.save();
        painter.translate(item.rect.center());
        painter.rotate(item.rotation);
        painter.translate(-item.rect.center());
        const QRectF bounds = item.bounds();
        painter.setPen(QPen(QColor("#54d6b0"), 1 / zoom_, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bounds);
        painter.setBrush(QColor("#54d6b0"));
        painter.drawRect(
            QRectF(bounds.bottomRight() - QPointF(4 / zoom_, 4 / zoom_), QSizeF(8 / zoom_, 8 / zoom_)));
        painter.restore();
    }
    if (picker_ && rendered_.rect().contains(hover_.toPoint())) {
        painter.resetTransform();
        const QPoint at = (hover_ * zoom_).toPoint();
        const QRect target(qBound(0, at.x() + 20, qMax(0, width() - 176)),
                           qBound(0, at.y() + 25, qMax(0, height() - 202)), 168, 168);
        painter.drawImage(target, rendered_.copy(QRect(hover_.toPoint() - QPoint(10, 10), QSize(21, 21))));
        painter.setPen(QPen(Qt::red, 2));
        painter.drawRect(QRect(target.center() - QPoint(4, 4), QSize(8, 8)));
        painter.fillRect(QRect(target.bottomLeft(), QSize(168, 30)), QColor("#172131"));
        painter.setPen(Qt::white);
        painter.drawText(QRect(target.bottomLeft(), QSize(168, 30)), Qt::AlignCenter,
                         colorText(rendered_.pixelColor(hover_.toPoint()),
                                   settings_.value("color/format", "HEX").toString(),
                                   settings_.value("color/uppercase", true).toBool()));
    }
}
void AnnotationCanvas::mousePressEvent(QMouseEvent *event) {
    setFocus();
    if (event->button() == Qt::RightButton) {
        finishDrawing();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF point = imagePoint(event->pos());
    if (picker_) {
        const QString text = colorText(rendered_.pixelColor(point.toPoint()),
                                       settings_.value("color/format", "HEX").toString(),
                                       settings_.value("color/uppercase", true).toBool());
        QApplication::clipboard()->setText(text);
        emit colorPicked(text);
        return;
    }
    if (polyline_) {
        document_.annotations.last().points.append(point);
        redraw();
        return;
    }
    anchor_ = previous_ = point;
    penStraight_ = false;
    if (tool_ == Tool::Select) {
        resizing_ = false;
        if (selected_ >= 0) {
            const auto &item = document_.annotations[selected_];
            QTransform transform;
            transform.translate(item.rect.center().x(), item.rect.center().y());
            transform.rotate(item.rotation);
            transform.translate(-item.rect.center().x(), -item.rect.center().y());
            resizing_ = QLineF(point, transform.map(item.bounds().bottomRight())).length() < 12 / zoom_;
        }
        if (!resizing_)
            selected_ = objectAt(point);
        if (selected_ >= 0) {
            checkpoint();
            if (event->modifiers().testFlag(Qt::AltModifier)) {
                document_.annotations.append(document_.annotations[selected_]);
                selected_ = document_.annotations.size() - 1;
            }
            original_ = document_.annotations[selected_];
            moving_ = !resizing_;
        }
        emit selectionChanged();
        update();
        return;
    }
    Annotation item = style_;
    item.tool = tool_;
    item.rotation = 0;
    item.points.clear();
    item.rect = QRectF(point, QSizeF(1, 1));
    const bool canMatch = tool_ == Tool::Rectangle || tool_ == Tool::RoundedRectangle ||
                          tool_ == Tool::Ellipse || tool_ == Tool::Highlighter || tool_ == Tool::Mosaic ||
                          tool_ == Tool::Blur || tool_ == Tool::Eraser;
    if (canMatch && event->modifiers().testFlag(Qt::ControlModifier)) {
        QRect match;
        for (const auto &element : document_.detectedElements) {
            if (element.contains(point.toPoint()) &&
                (match.isEmpty() ||
                 qint64(element.width()) * element.height() < qint64(match.width()) * match.height()))
                match = element;
        }
        if (!match.isEmpty()) {
            item.rect = match;
            checkpoint();
            document_.annotations.append(item);
            selected_ = document_.annotations.size() - 1;
            redraw();
            emit selectionChanged();
            return;
        }
    }
    if (tool_ == Tool::Text) {
        bool ok = false;
        item.text = QInputDialog::getMultiLineText(this, QStringLiteral("添加文字"),
                                                   QStringLiteral("文字内容"), "", &ok);
        if (!ok || item.text.isEmpty())
            return;
        QFont font;
        font.setPixelSize(qRound(item.fontSize));
        QFontMetrics metrics(font);
        int width = 0;
        const auto lines = item.text.split('\n');
        for (const auto &line : lines)
            width = qMax(width, metrics.horizontalAdvance(line));
        item.rect.setSize(QSizeF(width + 12, metrics.height() * lines.size() + 8));
    } else if (tool_ == Tool::Number) {
        const double side = qMax(30.0, item.fontSize * 1.6);
        item.rect = QRectF(point - QPointF(side / 2, side / 2), QSizeF(side, side));
        item.number = number_++;
    } else {
        drawing_ = true;
        if (tool_ == Tool::Line || tool_ == Tool::Arrow || tool_ == Tool::Pen || tool_ == Tool::Polyline)
            item.points = {point, point};
        polyline_ = tool_ == Tool::Polyline;
    }
    checkpoint();
    document_.annotations.append(item);
    selected_ = document_.annotations.size() - 1;
    redraw();
    emit selectionChanged();
}
void AnnotationCanvas::mouseMoveEvent(QMouseEvent *event) {
    const QPointF point = imagePoint(event->pos());
    hover_ = point;
    emit positionChanged(QStringLiteral("%1, %2 px  ·  %3")
                             .arg(qRound(point.x()))
                             .arg(qRound(point.y()))
                             .arg(rendered_.pixelColor(point.toPoint()).name().toUpper()));
    if (selected_ < 0) {
        if (picker_)
            update();
        return;
    }
    auto &item = document_.annotations[selected_];
    if (moving_)
        translate(item, point - previous_);
    else if (resizing_) {
        const QRectF old = original_.bounds();
        const double sx = qMax(0.05, (point.x() - old.left()) / qMax(1.0, old.width()));
        const double sy = event->modifiers().testFlag(Qt::ShiftModifier)
                              ? sx
                              : qMax(0.05, (point.y() - old.top()) / qMax(1.0, old.height()));
        item = original_;
        item.rect.setSize(QSizeF(original_.rect.width() * sx, original_.rect.height() * sy));
        for (auto &p : item.points)
            p = old.topLeft() + QPointF((p.x() - old.left()) * sx, (p.y() - old.top()) * sy);
    } else if (drawing_ || polyline_) {
        QPointF end = point;
        if (item.tool == Tool::Pen) {
            const bool straight = event->modifiers().testFlag(Qt::ShiftModifier);
            if (straight && !penStraight_)
                item.points.append(point);
            penStraight_ = straight;
        }
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (item.tool == Tool::Line || item.tool == Tool::Arrow || item.tool == Tool::Polyline ||
                item.tool == Tool::Pen) {
                const QPointF start = (item.tool == Tool::Polyline || item.tool == Tool::Pen)
                                          ? item.points[item.points.size() - 2]
                                          : anchor_;
                const QLineF line(start, point);
                const double radians =
                    qRound(std::atan2(point.y() - start.y(), point.x() - start.x()) / (M_PI / 4)) * M_PI / 4;
                end = start + QPointF(std::cos(radians) * line.length(), std::sin(radians) * line.length());
            } else {
                const double side = qMax(qAbs(point.x() - anchor_.x()), qAbs(point.y() - anchor_.y()));
                end = anchor_ + QPointF(point.x() >= anchor_.x() ? side : -side,
                                        point.y() >= anchor_.y() ? side : -side);
            }
        }
        if (item.tool == Tool::Pen && !event->modifiers().testFlag(Qt::ShiftModifier))
            item.points.append(end);
        else if (!item.points.isEmpty())
            item.points.last() = end;
        item.rect = QRectF(anchor_, end).normalized();
    } else {
        if (picker_)
            update();
        return;
    }
    previous_ = point;
    redraw();
}
void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    moving_ = resizing_ = false;
    if (!polyline_) {
        drawing_ = false;
        if (!settings_.value("annotation/selectNew", true).toBool() && tool_ != Tool::Select)
            selected_ = -1;
    }
    update();
    emit selectionChanged();
}
void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if (polyline_) {
        finishDrawing();
        return;
    }
    const int index = objectAt(imagePoint(event->pos()));
    if (index < 0 || document_.annotations[index].tool != Tool::Text)
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, QStringLiteral("编辑文字"), QStringLiteral("文字内容"), document_.annotations[index].text, &ok);
    if (ok) {
        checkpoint();
        document_.annotations[index].text = text;
        redraw();
    }
}
void AnnotationCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }
    if (event->matches(QKeySequence::Copy) && selected_ >= 0) {
        copyObject();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteObject();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteObject();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        finishDrawing();
        selected_ = -1;
        update();
        return;
    }
    if (event->key() == Qt::Key_Return) {
        finishDrawing();
        return;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight) {
        auto next = style();
        next.width = qBound(1.0, next.width + (event->key() == Qt::Key_BracketLeft ? -1 : 1), 100.0);
        setStyle(next);
        emit selectionChanged();
        return;
    }
    QPointF delta;
    if (event->key() == Qt::Key_Left)
        delta.rx() = -1;
    if (event->key() == Qt::Key_Right)
        delta.rx() = 1;
    if (event->key() == Qt::Key_Up)
        delta.ry() = -1;
    if (event->key() == Qt::Key_Down)
        delta.ry() = 1;
    if (selected_ >= 0 && !delta.isNull()) {
        checkpoint();
        translate(document_.annotations[selected_],
                  delta * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1));
        redraw();
        return;
    }
    QWidget::keyPressEvent(event);
}
void AnnotationCanvas::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier))
        setZoom(zoom_ * (event->angleDelta().y() > 0 ? 1.1 : 1 / 1.1));
    else {
        auto next = style();
        next.width = qBound(1.0, next.width + (event->angleDelta().y() > 0 ? 1 : -1), 100.0);
        setStyle(next);
        emit selectionChanged();
    }
}

EditorWindow::EditorWindow(Document document, Settings &settings, const QRect &sourceRect, QWidget *parent)
    : QMainWindow(parent), settings_(settings), sourceRect_(sourceRect) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("EasySnip · 标注编辑"));
    resize(QSize(uiPixels(1240), uiPixels(820))
               .boundedTo(QGuiApplication::primaryScreen()->availableGeometry().size() * 0.92));
    auto *root = new QWidget;
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(18, 16, 18, 12);
    outer->setSpacing(12);
    setCentralWidget(root);
    auto *top = new QHBoxLayout;
    auto *heading = new QLabel(QStringLiteral("标注编辑"));
    heading->setObjectName("heading");
    top->addWidget(heading);
    top->addStretch();
    auto action = [&](const QString &text, const QString &name, const QString &shortcut = {}) {
        auto *button = new QPushButton(text);
        top->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name] {
            canvas_->finishDrawing();
            const bool skip = QApplication::keyboardModifiers().testFlag(Qt::AltModifier);
            emit outputRequested(canvas_->document(), name, skip);
        });
        if (!shortcut.isEmpty()) {
            auto *key = new QShortcut(QKeySequence(shortcut), this);
            connect(key, &QShortcut::activated, this, [this, button, name] {
                if (name == "copy" && canvas_->hasFocus() && canvas_->selectedIndex() >= 0)
                    canvas_->copyObject();
                else
                    button->click();
            });
        }
        return button;
    };
    action(QStringLiteral("复制图像"), "copy", settings.value("keys/copy", "Ctrl+C").toString());
    action(QStringLiteral("另存为"), "save-as", settings.value("keys/save", "Ctrl+S").toString());
    action(QStringLiteral("贴到屏幕"), "pin", settings.value("keys/pin", "Ctrl+P").toString());
    auto *more = new QPushButton(QStringLiteral("更多"));
    top->addWidget(more);
    more->setVisible(settings_.value("output/menuVisible", true).toBool());
    auto *menu = new QMenu(more);
    for (const auto &pair : QVector<QPair<QString, QString>>{{QStringLiteral("快捷保存"), "save"},
                                                             {QStringLiteral("打印"), "print"},
                                                             {QStringLiteral("发送到外部程序"), "external"}})
        connect(menu->addAction(pair.first), &QAction::triggered, this, [this, pair] {
            emit outputRequested(canvas_->document(), pair.second,
                                 QApplication::keyboardModifiers().testFlag(Qt::AltModifier));
        });
    connect(menu->addAction(QStringLiteral("识别二维码 / 条形码")), &QAction::triggered, this,
            &EditorWindow::recognize);
    connect(menu->addAction(QStringLiteral("Windows 系统分享")), &QAction::triggered, this,
            [this] { emit outputRequested(canvas_->document(), "share", true); });
    connect(menu->addAction(QStringLiteral("刷新背景，保留标注")), &QAction::triggered, this,
            &EditorWindow::refreshBackground);
    auto *live = menu->addAction(QStringLiteral("连续刷新 / 定格"));
    live->setCheckable(true);
    live->setEnabled(!sourceRect_.isEmpty());
    connect(live, &QAction::toggled, this, [this](bool enabled) {
        if (enabled)
            refreshTimer_.start(settings_.value("capture/refreshMs", 1000).toInt());
        else
            refreshTimer_.stop();
    });
    more->setMenu(menu);
    auto *done = action(QStringLiteral("完成"), "finish", "Ctrl+Return");
    done->setObjectName("primary");
    outer->addLayout(top);
    auto *toolContainer = new QWidget;
    auto *tools = new QHBoxLayout(toolContainer);
    tools->setSpacing(4);
    tools->setContentsMargins(0, 0, 0, 0);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i <= int(Tool::Magnify); ++i) {
        auto *button = new QToolButton;
        button->setText(toolName(Tool(i)));
        button->setCheckable(true);
        button->setToolTip(toolName(Tool(i)));
        group->addButton(button, i);
        tools->addWidget(button);
        if (i == int(Tool::Rectangle))
            button->setChecked(true);
    }
    tools->addStretch();
    auto *toolScroll = new QScrollArea;
    toolScroll->setWidget(toolContainer);
    toolScroll->setWidgetResizable(true);
    toolScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolScroll->setFixedHeight(uiPixels(55));
    outer->addWidget(toolScroll);
    auto *body = new QHBoxLayout;
    outer->addLayout(body, 1);
    auto *scroll = new QScrollArea;
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setStyleSheet("QScrollArea {background:#080d15; border:1px solid #293448; border-radius:8px;}");
    canvas_ = new AnnotationCanvas(std::move(document), settings);
    scroll->setWidget(canvas_);
    body->addWidget(scroll, 1);
    auto *inspector = new QWidget;
    auto *form = new QFormLayout(inspector);
    form->setContentsMargins(8, 0, 8, 0);
    form->setSpacing(10);
    auto *inspectorScroll = new QScrollArea;
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFixedWidth(uiPixels(242));
    inspectorScroll->setWidget(inspector);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(inspectorScroll);
    auto *label = new QLabel(QStringLiteral("画笔与对象"));
    label->setStyleSheet(QString("font-size:%1px; font-weight:600").arg(uiPixels(17)));
    form->addRow(label);
    palette_ = new QListWidget;
    palette_->setViewMode(QListView::IconMode);
    palette_->setIconSize(QSize(uiPixels(24), uiPixels(24)));
    palette_->setGridSize(QSize(uiPixels(36), uiPixels(38)));
    palette_->setFixedHeight(uiPixels(88));
    palette_->setMovement(QListView::Snap);
    palette_->setDragDropMode(QAbstractItemView::InternalMove);
    palette_->setFlow(QListView::LeftToRight);
    palette_->setWrapping(true);
    for (const auto &color : settings.palette()) {
        QPixmap swatch(24, 24);
        swatch.fill(QColor(color));
        auto *item = new QListWidgetItem(QIcon(swatch), "", palette_);
        item->setData(Qt::UserRole, color);
    }
    palette_->viewport()->installEventFilter(this);
    form->addRow(palette_);
    connect(palette_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        auto next = canvas_->style();
        next.color = QColor(item->data(Qt::UserRole).toString());
        next.color.setAlpha(alpha_->value());
        canvas_->setStyle(next);
        syncInspector();
    });
    auto *paletteEdit = new QPushButton(QStringLiteral("定制 / 导入 / 导出色板"));
    form->addRow(paletteEdit);
    connect(paletteEdit, &QPushButton::clicked, this, &EditorWindow::editPalette);
    color_ = new QPushButton;
    fill_ = new QPushButton(QStringLiteral("背景填充"));
    outline_ = new QPushButton(QStringLiteral("描边颜色"));
    form->addRow(QStringLiteral("颜色"), color_);
    form->addRow(QStringLiteral("填充"), fill_);
    form->addRow(QStringLiteral("文字描边"), outline_);
    auto pick = [this](int type) {
        auto next = canvas_->style();
        const QColor selected =
            QColorDialog::getColor(type == 0   ? next.color
                                   : type == 1 ? next.fill
                                               : next.outline,
                                   this, QStringLiteral("颜色与透明度"), QColorDialog::ShowAlphaChannel);
        if (!selected.isValid())
            return;
        if (type == 0)
            next.color = selected;
        else if (type == 1)
            next.fill = selected;
        else
            next.outline = selected;
        canvas_->setStyle(next);
        syncInspector();
    };
    connect(color_, &QPushButton::clicked, this, [pick] { pick(0); });
    connect(fill_, &QPushButton::clicked, this, [pick] { pick(1); });
    connect(outline_, &QPushButton::clicked, this, [pick] { pick(2); });
    width_ = new QDoubleSpinBox;
    width_->setRange(1, 100);
    width_->setValue(3);
    form->addRow(QStringLiteral("粗细"), width_);
    auto *tips = new QComboBox;
    const auto presets = settings.value("annotation/tips", "2,4,8,12,20").toString().split(',');
    for (const auto &tip : presets.mid(0, 5))
        tips->addItem(tip + " px", tip.toDouble());
    form->addRow(QStringLiteral("笔尖预设"), tips);
    connect(tips, QOverload<int>::of(&QComboBox::activated), this,
            [this, tips] { width_->setValue(tips->currentData().toDouble()); });
    auto *saveTips = new QPushButton(QStringLiteral("编辑 1–5 个笔尖"));
    form->addRow(saveTips);
    connect(saveTips, &QPushButton::clicked, this, [this, tips] {
        bool ok = false;
        const QString value = QInputDialog::getText(
            this, QStringLiteral("笔尖预设"), QStringLiteral("输入 1–5 个粗细（1–100），用逗号分隔"),
            QLineEdit::Normal, settings_.value("annotation/tips", "2,4,8,12,20").toString(), &ok);
        if (!ok)
            return;
        QStringList valid;
        for (const auto &part : value.split(',')) {
            bool number = false;
            const double n = part.toDouble(&number);
            if (number && n >= 1 && n <= 100 && valid.size() < 5)
                valid.append(QString::number(n));
        }
        if (valid.isEmpty())
            return;
        settings_.set("annotation/tips", valid.join(','));
        tips->clear();
        for (const auto &part : valid)
            tips->addItem(part + " px", part.toDouble());
    });
    alpha_ = new QSpinBox;
    alpha_->setRange(0, 255);
    alpha_->setValue(255);
    form->addRow(QStringLiteral("Alpha"), alpha_);
    fontSize_ = new QSpinBox;
    fontSize_->setRange(6, 300);
    fontSize_->setValue(24);
    form->addRow(QStringLiteral("字号"), fontSize_);
    outlineWidth_ = new QSpinBox;
    outlineWidth_->setRange(0, 20);
    form->addRow(QStringLiteral("描边宽度"), outlineWidth_);
    rotation_ = new QDoubleSpinBox;
    rotation_->setRange(-360, 360);
    rotation_->setSuffix(QStringLiteral("°"));
    form->addRow(QStringLiteral("旋转"), rotation_);
    lineStyle_ = new QComboBox;
    lineStyle_->addItems({QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线")});
    form->addRow(QStringLiteral("线型"), lineStyle_);
    shape_ = new QComboBox;
    shape_->addItems({QStringLiteral("矩形区域"), QStringLiteral("椭圆区域"), QStringLiteral("圆形区域")});
    form->addRow(QStringLiteral("效果区域"), shape_);
    arrow_ = new QComboBox;
    arrow_->addItems({QStringLiteral("单向开放"), QStringLiteral("双向"), QStringLiteral("反向"),
                      QStringLiteral("实心箭头")});
    form->addRow(QStringLiteral("箭头"), arrow_);
    auto *radius = new QSpinBox;
    radius->setRange(0, 500);
    radius->setValue(canvas_->document().cornerRadius);
    form->addRow(QStringLiteral("图像圆角"), radius);
    connect(radius, QOverload<int>::of(&QSpinBox::valueChanged), canvas_, &AnnotationCanvas::setRadius);
    for (auto *spin : {alpha_, fontSize_, outlineWidth_})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &EditorWindow::updateStyle);
    for (auto *spin : {width_, rotation_})
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorWindow::updateStyle);
    for (auto *combo : {lineStyle_, shape_, arrow_})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EditorWindow::updateStyle);
    auto *footer = new QHBoxLayout;
    auto command = [&](const QString &text, const std::function<void()> &callback) {
        auto *button = new QPushButton(text);
        footer->addWidget(button);
        connect(button, &QPushButton::clicked, this, callback);
    };
    command(QStringLiteral("撤销"), [this] { canvas_->undo(); });
    command(QStringLiteral("重做"), [this] { canvas_->redo(); });
    command(QStringLiteral("清除标注"), [this] { canvas_->clear(); });
    command(QStringLiteral("取色 / 放大镜"), [this] { canvas_->togglePicker(); });
    footer->addStretch();
    auto *zoom = new QComboBox;
    zoom->addItems({QStringLiteral("适应窗口"), "50%", "100%", "150%", "200%"});
    footer->addWidget(zoom);
    connect(zoom, QOverload<int>::of(&QComboBox::activated), this, [this, scroll](int index) {
        const double ratios[]{1, 0.5, 1, 1.5, 2};
        canvas_->setZoom(
            index == 0 ? qMin(double(scroll->viewport()->width() - 24) / canvas_->document().image.width(),
                              double(scroll->viewport()->height() - 24) / canvas_->document().image.height())
                       : ratios[index]);
    });
    outer->addLayout(footer);
    connect(group, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int id) {
        canvas_->setTool(Tool(id));
        syncInspector();
    });
    connect(canvas_, &AnnotationCanvas::selectionChanged, this, &EditorWindow::syncInspector);
    connect(canvas_, &AnnotationCanvas::colorPicked, this, [this](const QString &text) {
        statusBar()->showMessage(QStringLiteral("已复制色值 ") + text, 4000);
    });
    connect(canvas_, &AnnotationCanvas::positionChanged, this, [this](const QString &text) {
        statusBar()->showMessage(
            text +
            QStringLiteral("   ·   Shift 约束 · Alt 拖动克隆 · 滚轮粗细 · Ctrl+滚轮缩放 · 双击编辑文字"));
    });
    connect(&refreshTimer_, &QTimer::timeout, this, &EditorWindow::refreshBackground);
    QTimer::singleShot(0, this, [this, scroll] {
        canvas_->setZoom(
            qMin(1.0, qMin(double(scroll->viewport()->width() - 24) / canvas_->document().image.width(),
                           double(scroll->viewport()->height() - 24) / canvas_->document().image.height())));
    });
    syncInspector();
}
AnnotationCanvas *EditorWindow::canvas() const {
    return canvas_;
}
void EditorWindow::acknowledgeOutput() {
    output_ = true;
}
void EditorWindow::syncInspector() {
    const Annotation style = canvas_->style();
    const QSignalBlocker a(width_), b(alpha_), c(fontSize_), d(outlineWidth_), e(rotation_), f(lineStyle_),
        g(shape_), h(arrow_);
    width_->setValue(style.width);
    alpha_->setValue(style.color.alpha());
    fontSize_->setValue(qRound(style.fontSize));
    outlineWidth_->setValue(qRound(style.outlineWidth));
    rotation_->setValue(style.rotation);
    lineStyle_->setCurrentIndex(style.lineStyle - 1);
    shape_->setCurrentIndex(style.shape);
    arrow_->setCurrentIndex(style.arrowStyle);
    color_->setText(style.color.name().toUpper());
    color_->setStyleSheet(QString("background:%1;color:%2")
                              .arg(style.color.name(), style.color.lightness() > 140 ? "#111827" : "white"));
    fill_->setText(style.fill.alpha() == 0 ? QStringLiteral("透明") : style.fill.name());
    outline_->setText(style.outline.name());
}
void EditorWindow::updateStyle() {
    auto style = canvas_->style();
    style.width = width_->value();
    style.color.setAlpha(alpha_->value());
    style.fontSize = fontSize_->value();
    style.outlineWidth = outlineWidth_->value();
    style.rotation = rotation_->value();
    style.lineStyle = lineStyle_->currentIndex() + 1;
    style.shape = shape_->currentIndex();
    style.arrowStyle = arrow_->currentIndex();
    canvas_->setStyle(style);
}
void EditorWindow::refreshBackground() {
    if (sourceRect_.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("当前图片没有屏幕来源区域。"), 4000);
        return;
    }
    if (refreshing_)
        return;
    refreshing_ = true;
    hide();
    QTimer::singleShot(100, this, [this] {
        const auto desktop = Native::captureDesktop();
        const QRect region = sourceRect_.translated(-desktop.geometry.topLeft());
        if (desktop.image.rect().contains(region))
            canvas_->setBaseImage(desktop.image.copy(region));
        show();
        raise();
        activateWindow();
        refreshing_ = false;
    });
}
void EditorWindow::editPalette() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("调色板"));
    dialog.resize(420, 360);
    QVBoxLayout layout(&dialog);
    QLabel label(QStringLiteral("每行一个 HEX 颜色，可在侧栏拖动色块排序。"));
    layout.addWidget(&label);
    QStringList colors;
    for (int i = 0; i < palette_->count(); ++i)
        colors.append(palette_->item(i)->data(Qt::UserRole).toString());
    QPlainTextEdit edit(colors.join('\n'));
    layout.addWidget(&edit);
    QHBoxLayout files;
    QPushButton importButton(QStringLiteral("导入")), exportButton(QStringLiteral("导出"));
    files.addWidget(&importButton);
    files.addWidget(&exportButton);
    layout.addLayout(&files);
    connect(&importButton, &QPushButton::clicked, &dialog, [&] {
        const QString path =
            QFileDialog::getOpenFileName(&dialog, QStringLiteral("导入调色板"), {}, "Palette (*.txt)");
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            edit.setPlainText(QString::fromUtf8(file.readAll()));
    });
    connect(&exportButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出调色板"),
                                                          "palette.txt", "Palette (*.txt)");
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(edit.toPlainText().toUtf8());
            if (!file.commit())
                QMessageBox::warning(&dialog, QStringLiteral("导出失败"), file.errorString());
        }
    });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    colors.clear();
    for (const auto &line : edit.toPlainText().split('\n'))
        if (QColor(line.trimmed()).isValid())
            colors.append(QColor(line.trimmed()).name());
    if (colors.isEmpty())
        return;
    settings_.set("annotation/palette", colors);
    palette_->clear();
    for (const auto &color : colors) {
        QPixmap swatch(24, 24);
        swatch.fill(QColor(color));
        auto *item = new QListWidgetItem(QIcon(swatch), "", palette_);
        item->setData(Qt::UserRole, color);
    }
}
void EditorWindow::recognize() {
    statusBar()->showMessage(QStringLiteral("正在识别…"));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString result = watcher->result();
        watcher->deleteLater();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("二维码 / 条形码"));
        dialog.resize(600, 360);
        QVBoxLayout layout(&dialog);
        QPlainTextEdit text(result.isEmpty() ? QStringLiteral("未检测到可读取的二维码或条形码。") : result);
        text.setReadOnly(true);
        layout.addWidget(&text);
        QDialogButtonBox buttons(QDialogButtonBox::Close);
        auto *copy = buttons.addButton(QStringLiteral("复制内容"), QDialogButtonBox::ActionRole);
        copy->setEnabled(!result.isEmpty());
        layout.addWidget(&buttons);
        connect(copy, &QPushButton::clicked, &dialog, [&] { QApplication::clipboard()->setText(result); });
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
        statusBar()->clearMessage();
    });
    const QImage image = canvas_->document().render();
    watcher->setFuture(QtConcurrent::run([image] { return decodeCodes(image); }));
}
void EditorWindow::closeEvent(QCloseEvent *event) {
    refreshTimer_.stop();
    QStringList colors;
    for (int i = 0; i < palette_->count(); ++i)
        colors.append(palette_->item(i)->data(Qt::UserRole).toString());
    settings_.set("annotation/palette", colors);
    if (!output_)
        emit abandoned(canvas_->document());
    QMainWindow::closeEvent(event);
}
bool EditorWindow::eventFilter(QObject *object, QEvent *event) {
    if (object == palette_->viewport() && event->type() == QEvent::Wheel) {
        const auto *wheel = static_cast<QWheelEvent *>(event);
        auto next = canvas_->style();
        next.color.setAlpha(qBound(0, next.color.alpha() + (wheel->angleDelta().y() > 0 ? 8 : -8), 255));
        canvas_->setStyle(next);
        syncInspector();
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}
