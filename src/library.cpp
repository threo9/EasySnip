#include "library.h"
#include <QtWidgets>

namespace {
QByteArray imageBytes(const QImage &image) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return data;
}
bool atomicWrite(const QString &path, const QByteArray &bytes) {
    if (bytes.isEmpty())
        return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool safeId(const QString &id) {
    return QRegularExpression("^[0-9a-f-]{36}$").match(id).hasMatch();
}
bool readArchiveBytes(QDataStream &stream, QByteArray &bytes, quint32 maximum) {
    quint32 size = 0;
    stream >> size;
    if (stream.status() != QDataStream::Ok || size > maximum ||
        qint64(size) > stream.device()->bytesAvailable())
        return false;
    bytes.resize(int(size));
    return stream.readRawData(bytes.data(), int(size)) == int(size);
}
} // namespace
QJsonObject Record::toJson() const {
    return {{"version", 1},
            {"id", id},
            {"title", title},
            {"created", created.toString(Qt::ISODateWithMs)},
            {"closedAt", closedAt.toString(Qt::ISODateWithMs)},
            {"group", group},
            {"source", sourceFile},
            {"preview", previewFile},
            {"document", document},
            {"width", imageSize.width()},
            {"height", imageSize.height()},
            {"geometry", QJsonArray{geometry.x(), geometry.y(), geometry.width(), geometry.height()}},
            {"opacity", opacity},
            {"history", history},
            {"pinned", pinned},
            {"closed", closed}};
}
Record Record::fromJson(const QJsonObject &object) {
    Record record;
    record.id = object["id"].toString();
    record.title = object["title"].toString();
    record.created = QDateTime::fromString(object["created"].toString(), Qt::ISODateWithMs);
    record.closedAt = QDateTime::fromString(object["closedAt"].toString(), Qt::ISODateWithMs);
    record.group = object["group"].toString(QStringLiteral("默认分组"));
    record.sourceFile = object["source"].toString();
    record.previewFile = object["preview"].toString();
    record.document = object["document"].toObject();
    record.imageSize = QSize(object["width"].toInt(), object["height"].toInt());
    const auto geometry = object["geometry"].toArray();
    record.geometry =
        QRect(geometry[0].toInt(), geometry[1].toInt(), geometry[2].toInt(), geometry[3].toInt());
    record.opacity = qBound(0.1, object["opacity"].toDouble(1), 1.0);
    record.history = object["history"].toBool(true);
    record.pinned = object["pinned"].toBool();
    record.closed = object["closed"].toBool();
    return record;
}
Library::Library(Settings &settings, QObject *parent)
    : QObject(parent), settings_(settings), directory_(settings.historyDirectory()) {
    QDir().mkpath(directory_);
    load();
}
QString Library::path(const QString &fileName) const {
    return QDir(directory_).filePath(fileName);
}
QVector<Record> Library::records() const {
    return records_;
}
Record Library::record(const QString &id) const {
    for (const auto &item : records_)
        if (item.id == id)
            return item;
    return {};
}
QString Library::previewPath(const Record &record) const {
    return path(record.previewFile);
}
Document Library::document(const QString &id) const {
    const Record item = record(id);
    if (item.id.isEmpty())
        return {};
    return Document::fromJson(item.document, QImage(path(item.sourceFile)));
}
void Library::load() {
    QFile state(path("state.json"));
    if (state.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(state.readAll()).object();
        for (const auto &group : object["groups"].toArray())
            if (!group.toString().trimmed().isEmpty())
                groups_.append(group.toString());
        activeGroup_ = object["activeGroup"].toString();
    }
    if (groups_.isEmpty())
        groups_.append(QStringLiteral("默认分组"));
    if (!groups_.contains(activeGroup_))
        activeGroup_ = groups_.first();
    const auto files = QDir(directory_).entryList({"*.json"}, QDir::Files);
    for (const auto &name : files) {
        if (name == "state.json")
            continue;
        QFile file(path(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
            continue;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
        if (!json.isObject())
            continue;
        auto item = Record::fromJson(json.object());
        if (!safeId(item.id) || item.id + ".json" != name ||
            QFileInfo(item.sourceFile).fileName() != item.sourceFile ||
            QFileInfo(item.previewFile).fileName() != item.previewFile ||
            !QFile::exists(path(item.sourceFile)))
            continue;
        if (!groups_.contains(item.group))
            groups_.append(item.group);
        records_.append(item);
    }
    std::sort(records_.begin(), records_.end(),
              [](const Record &a, const Record &b) { return a.created > b.created; });
}
bool Library::writeRecord(const Record &record) {
    if (!atomicWrite(path(record.id + ".json"), QJsonDocument(record.toJson()).toJson())) {
        emit error(QStringLiteral("无法保存历史记录，请检查存储目录权限和磁盘空间。"));
        return false;
    }
    return true;
}
bool Library::saveDocument(Record record, const Document &document) {
    if (document.image.isNull())
        return false;
    const Record old = this->record(record.id);
    const QString revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.sourceFile = revision + ".png";
    record.previewFile = revision + "-preview.png";
    record.imageSize = document.image.size();
    record.document = document.toJson();
    const bool sourceSaved = atomicWrite(path(record.sourceFile), imageBytes(document.image));
    const bool previewSaved =
        sourceSaved && atomicWrite(path(record.previewFile),
                                   imageBytes(document.render().scaled(320, 220, Qt::KeepAspectRatio,
                                                                       Qt::SmoothTransformation)));
    if (!previewSaved || !writeRecord(record)) {
        QFile::remove(path(record.sourceFile));
        QFile::remove(path(record.previewFile));
        if (!previewSaved)
            emit error(QStringLiteral("图像保存失败，请检查磁盘空间。"));
        return false;
    }
    bool found = false;
    for (auto &item : records_)
        if (item.id == record.id) {
            item = record;
            found = true;
            break;
        }
    if (!found)
        records_.prepend(record);
    if (!old.sourceFile.isEmpty())
        QFile::remove(path(old.sourceFile));
    if (!old.previewFile.isEmpty())
        QFile::remove(path(old.previewFile));
    emit changed();
    return true;
}
QString Library::add(const Document &document, bool history) {
    Record record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.created = QDateTime::currentDateTime();
    record.title = record.created.toString("yyyy-MM-dd HH:mm:ss");
    record.group = activeGroup_;
    record.history = history;
    if (!saveDocument(record, document))
        return {};
    return record.id;
}
bool Library::updateDocument(const QString &id, const Document &document) {
    const Record item = record(id);
    return !item.id.isEmpty() && saveDocument(item, document);
}
bool Library::updateRecord(const Record &record) {
    if (!safeId(record.id) || this->record(record.id).id.isEmpty() || !writeRecord(record))
        return false;
    for (auto &item : records_)
        if (item.id == record.id) {
            item = record;
            emit changed();
            return true;
        }
    return false;
}
bool Library::remove(const QString &id) {
    for (int i = 0; i < records_.size(); ++i)
        if (records_[i].id == id) {
            const auto item = records_[i];
            if (!QFile::remove(path(id + ".json"))) {
                emit error(QStringLiteral("无法删除记录文件。"));
                return false;
            }
            QFile::remove(path(item.sourceFile));
            QFile::remove(path(item.previewFile));
            records_.removeAt(i);
            emit changed();
            return true;
        }
    return false;
}
void Library::clearHistory() {
    const auto snapshot = records_;
    for (auto item : snapshot)
        if (item.history) {
            if (item.pinned) {
                item.history = false;
                updateRecord(item);
            } else
                remove(item.id);
        }
}
void Library::prune() {
    int histories = 0;
    const auto snapshot = records_;
    for (auto item : snapshot)
        if (item.history && ++histories > settings_.value("history/limit", 100).toInt()) {
            if (item.pinned) {
                item.history = false;
                updateRecord(item);
            } else
                remove(item.id);
        }
    QVector<Record> closed;
    for (const auto &item : records_)
        if (item.pinned && item.closed)
            closed.append(item);
    std::sort(closed.begin(), closed.end(),
              [](const Record &a, const Record &b) { return a.closedAt > b.closedAt; });
    for (int i = qMax(0, settings_.value("pins/recoverLimit", 20).toInt()); i < closed.size(); ++i) {
        auto item = closed[i];
        if (item.history) {
            item.pinned = false;
            item.closed = false;
            updateRecord(item);
        } else
            remove(item.id);
    }
}
QStringList Library::groups() const {
    return groups_;
}
QString Library::activeGroup() const {
    return activeGroup_;
}
bool Library::saveState() {
    return atomicWrite(path("state.json"),
                       QJsonDocument(QJsonObject{{"version", 1},
                                                 {"groups", QJsonArray::fromStringList(groups_)},
                                                 {"activeGroup", activeGroup_}})
                           .toJson());
}
void Library::setActiveGroup(const QString &group) {
    if (!groups_.contains(group))
        return;
    activeGroup_ = group;
    saveState();
    emit changed();
}
bool Library::setGroups(const QStringList &groups) {
    if (groups.isEmpty())
        return false;
    for (auto item : records_)
        if (!groups.contains(item.group)) {
            item.group = groups.first();
            if (!updateRecord(item))
                return false;
        }
    groups_ = groups;
    if (!groups_.contains(activeGroup_))
        activeGroup_ = groups_.first();
    const bool saved = saveState();
    emit changed();
    return saved;
}
bool Library::renameGroup(const QString &oldName, const QString &newName) {
    if (newName.trimmed().isEmpty() || groups_.contains(newName) || !groups_.contains(oldName))
        return false;
    for (auto item : records_)
        if (item.group == oldName) {
            item.group = newName;
            if (!updateRecord(item))
                return false;
        }
    groups_[groups_.indexOf(oldName)] = newName;
    if (activeGroup_ == oldName)
        activeGroup_ = newName;
    const bool saved = saveState();
    emit changed();
    return saved;
}
bool Library::exportBackup(const QString &fileName) {
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_14);
    const QJsonObject header{{"groups", QJsonArray::fromStringList(groups_)},
                             {"activeGroup", activeGroup_},
                             {"count", records_.size()}};
    stream << quint32(0x45534e50) << quint32(2) << QJsonDocument(header).toJson(QJsonDocument::Compact);
    for (const auto &item : records_) {
        QFile image(path(item.sourceFile));
        if (!image.open(QIODevice::ReadOnly))
            return false;
        stream << QJsonDocument(item.toJson()).toJson(QJsonDocument::Compact) << image.readAll();
    }
    return stream.status() == QDataStream::Ok && file.commit();
}
bool Library::importBackup(const QString &fileName) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_14);
    quint32 magic = 0, version = 0;
    stream >> magic >> version;
    if (magic != 0x45534e50 || version != 2)
        return false;
    QByteArray headerBytes;
    if (!readArchiveBytes(stream, headerBytes, 1024 * 1024))
        return false;
    const QJsonDocument headerDocument = QJsonDocument::fromJson(headerBytes);
    if (!headerDocument.isObject())
        return false;
    const auto header = headerDocument.object();
    const int count = header["count"].toInt(-1);
    if (count < 0 || count > 10000)
        return false;
    QStringList groups;
    for (const auto &group : header["groups"].toArray())
        if (!group.toString().trimmed().isEmpty())
            groups.append(group.toString());
    // Validate the entire archive before importing any records.
    struct Entry {
        Record record;
        QImage image;
    };
    QVector<Entry> entries;
    quint64 decodedBytes = 0;
    for (int i = 0; i < count; ++i) {
        QByteArray json, pixels;
        if (!readArchiveBytes(stream, json, 16 * 1024 * 1024) ||
            !readArchiveBytes(stream, pixels, 128 * 1024 * 1024))
            return false;
        const auto parsed = QJsonDocument::fromJson(json);
        QBuffer buffer(&pixels);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (!parsed.isObject() || !size.isValid())
            return false;
        decodedBytes += quint64(size.width()) * quint64(size.height()) * 4;
        if (decodedBytes > 512ULL * 1024 * 1024)
            return false;
        const QImage image = reader.read();
        if (image.isNull())
            return false;
        auto item = Record::fromJson(parsed.object());
        if (!safeId(item.id))
            return false;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entries.append({item, image});
    }
    if (!file.atEnd())
        return false;
    QStringList added;
    for (const auto &entry : entries) {
        if (!saveDocument(entry.record, Document::fromJson(entry.record.document, entry.image))) {
            for (const auto &id : added)
                remove(id);
            return false;
        }
        added.append(entry.record.id);
    }
    for (const auto &group : groups)
        if (!group.trimmed().isEmpty() && !groups_.contains(group))
            groups_.append(group);
    saveState();
    prune();
    emit changed();
    return true;
}

PinWindow::PinWindow(const Record &record, Document document, Settings &settings, const QStringList &groups)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), id_(record.id),
      document_(std::move(document)), settings_(settings), groups_(groups) {
    setWindowTitle(QStringLiteral("EasySnip 贴图"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(32, 32);
    image_ = document_.render();
    QRect position = record.geometry;
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    if (position.isEmpty()) {
        const QSize size =
            image_.size().scaled(screen.size() * 0.65, Qt::KeepAspectRatio).boundedTo(image_.size());
        position = QRect(screen.center() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    bool onScreen = false;
    for (const auto *monitor : QGuiApplication::screens())
        if (monitor->availableGeometry().intersects(position))
            onScreen = true;
    if (!onScreen)
        position.moveTopLeft(screen.topLeft() + QPoint(40, 40));
    setGeometry(position);
    setWindowOpacity(record.opacity);
    saveTimer_.setInterval(400);
    saveTimer_.setSingleShot(true);
    connect(&saveTimer_, &QTimer::timeout, this,
            [this] { emit geometryChanged(id_, geometry(), windowOpacity()); });
}
QString PinWindow::recordId() const {
    return id_;
}
void PinWindow::setDocument(const Document &document) {
    document_ = document;
    image_ = document.render();
    update();
}
void PinWindow::setGroups(const QStringList &groups) {
    groups_ = groups;
}
void PinWindow::scheduleSave() {
    saveTimer_.start();
}
void PinWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(rect(), image_);
    painter.setPen(QColor(100, 150, 180, 140));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (picker_ && rect().contains(hover_)) {
        const QPoint point(qBound(0, hover_.x() * image_.width() / width(), image_.width() - 1),
                           qBound(0, hover_.y() * image_.height() / height(), image_.height() - 1));
        const QRect lens(qBound(0, hover_.x() + 20, qMax(0, width() - 132)),
                         qBound(0, hover_.y() + 20, qMax(0, height() - 162)), 128, 128);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(lens, image_.copy(QRect(point - QPoint(8, 8), QSize(16, 16))));
        painter.setPen(Qt::red);
        painter.drawRect(QRect(lens.center(), QSize(8, 8)));
        painter.fillRect(QRect(lens.bottomLeft(), QSize(128, 30)), QColor("#10151e"));
        painter.setPen(Qt::white);
        painter.drawText(QRect(lens.bottomLeft(), QSize(128, 30)), Qt::AlignCenter,
                         colorText(image_.pixelColor(point),
                                   settings_.value("color/format", "HEX").toString(),
                                   settings_.value("color/uppercase", true).toBool()));
    }
}
void PinWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    if (picker_) {
        const QPoint point(qBound(0, event->x() * image_.width() / width(), image_.width() - 1),
                           qBound(0, event->y() * image_.height() / height(), image_.height() - 1));
        QApplication::clipboard()->setText(colorText(image_.pixelColor(point),
                                                     settings_.value("color/format", "HEX").toString(),
                                                     settings_.value("color/uppercase", true).toBool()));
        return;
    }
    resizing_ = settings_.value("pins/edgeResize", true).toBool() &&
                (event->x() > width() - 14 || event->y() > height() - 14);
    dragging_ = !resizing_;
    dragOffset_ = event->globalPos() - frameGeometry().topLeft();
    dragOrigin_ = event->globalPos();
    originalSize_ = size();
}
void PinWindow::mouseMoveEvent(QMouseEvent *event) {
    hover_ = event->pos();
    if (dragging_) {
        QPoint next = event->globalPos() - dragOffset_;
        const int snap = settings_.value("pins/snapDistance", 12).toInt();
        for (const auto *screen : QGuiApplication::screens()) {
            const QRect bounds = screen->availableGeometry();
            if (qAbs(next.x() - bounds.left()) < snap)
                next.setX(bounds.left());
            if (qAbs(next.y() - bounds.top()) < snap)
                next.setY(bounds.top());
            if (qAbs(next.x() + width() - bounds.right() - 1) < snap)
                next.setX(bounds.right() - width() + 1);
            if (qAbs(next.y() + height() - bounds.bottom() - 1) < snap)
                next.setY(bounds.bottom() - height() + 1);
        }
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *pin = qobject_cast<PinWindow *>(widget);
            if (!pin || pin == this || !pin->isVisible())
                continue;
            const QRect other = pin->geometry();
            if (qAbs(next.x() - other.right() - 1) < snap)
                next.setX(other.right() + 1);
            if (qAbs(next.y() - other.bottom() - 1) < snap)
                next.setY(other.bottom() + 1);
        }
        move(next);
        scheduleSave();
    } else if (resizing_) {
        const int width = qMax(32, originalSize_.width() + event->globalX() - dragOrigin_.x());
        resize(width, qMax(32, qRound(double(width) * image_.height() / image_.width())));
        scheduleSave();
    } else
        setCursor(picker_                                                     ? Qt::CrossCursor
                  : (event->x() > width() - 14 || event->y() > height() - 14) ? Qt::SizeFDiagCursor
                                                                              : Qt::SizeAllCursor);
    if (picker_)
        update();
}
void PinWindow::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = resizing_ = false;
}
void PinWindow::mouseDoubleClickEvent(QMouseEvent *) {
    emit actionRequested(id_, "edit");
}
void PinWindow::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier))
        setWindowOpacity(qBound(0.1, windowOpacity() + (event->angleDelta().y() > 0 ? 0.05 : -0.05), 1.0));
    else {
        const double factor = event->angleDelta().y() > 0 ? 1.1 : 1 / 1.1;
        const int w = qBound(32, qRound(width() * factor), image_.width() * 5);
        resize(w, qMax(32, qRound(double(w) * image_.height() / image_.width())));
    }
    scheduleSave();
}
void PinWindow::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu;
    for (const auto &action :
         QVector<QPair<QString, QString>>{{QStringLiteral("标注编辑"), "edit"},
                                          {QStringLiteral("复制图像"), "copy"},
                                          {QStringLiteral("另存为"), "save-as"},
                                          {QStringLiteral("识别二维码 / 条形码"), "decode"}})
        connect(menu.addAction(action.first), &QAction::triggered, this,
                [this, action] { emit actionRequested(id_, action.second); });
    auto *picker = menu.addAction(QStringLiteral("取色放大镜"));
    picker->setCheckable(true);
    picker->setChecked(picker_);
    connect(picker, &QAction::toggled, this, [this](bool enabled) {
        picker_ = enabled;
        update();
    });
    auto *groups = menu.addMenu(QStringLiteral("移动到分组"));
    for (const auto &group : groups_)
        connect(groups->addAction(group), &QAction::triggered, this,
                [this, group] { emit groupRequested(id_, group); });
    connect(menu.addAction(QStringLiteral("恢复 100% 大小")), &QAction::triggered, this, [this] {
        resize(image_.size());
        scheduleSave();
    });
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("关闭（可恢复）")), &QAction::triggered, this, [this] { close(); });
    connect(menu.addAction(QStringLiteral("销毁记录")), &QAction::triggered, this,
            [this] { emit actionRequested(id_, "destroy"); });
    menu.exec(event->globalPos());
}
void PinWindow::closeEvent(QCloseEvent *event) {
    emit geometryChanged(id_, geometry(), windowOpacity());
    emit actionRequested(id_, "close");
    event->ignore();
    hide();
}
void PinWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape)
        close();
    else if (event->key() == Qt::Key_E)
        emit actionRequested(id_, "edit");
    else if (event->matches(QKeySequence::Copy))
        emit actionRequested(id_, "copy");
    else if (event->matches(QKeySequence::Save))
        emit actionRequested(id_, "save-as");
    else
        QWidget::keyPressEvent(event);
}
bool PinWindow::event(QEvent *event) {
    if (event->type() == QEvent::WindowActivate)
        qApp->setProperty("EasySnipActivePin", id_);
    return QWidget::event(event);
}

LibraryWindow::LibraryWindow(Library &library, Settings &settings) : library_(library), settings_(settings) {
    setWindowTitle(QStringLiteral("EasySnip · 截图与贴图"));
    resize(QSize(uiPixels(1120), uiPixels(760))
               .boundedTo(QGuiApplication::primaryScreen()->availableGeometry().size() * 0.92));
    setAcceptDrops(true);
    auto *root = new QWidget;
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setCentralWidget(root);
    auto *sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(uiPixels(220));
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(22, 28, 20, 22);
    side->setSpacing(16);
    auto *brand = new QLabel("EasySnip");
    brand->setObjectName("brand");
    side->addWidget(brand);
    auto *tagline = new QLabel(QStringLiteral("捕捉 · 标注 · 留在屏幕"));
    tagline->setObjectName("muted");
    side->addWidget(tagline);
    auto *capture = new QPushButton(QStringLiteral("＋  开始截图"));
    capture->setObjectName("primary");
    side->addWidget(capture);
    connect(capture, &QPushButton::clicked, this, [this] { emit command("capture"); });
    navigation_ = new QListWidget;
    side->addWidget(navigation_, 1);
    auto *groups = new QPushButton(QStringLiteral("管理分组"));
    side->addWidget(groups);
    connect(groups, &QPushButton::clicked, this, &LibraryWindow::manageGroups);
    auto *preferences = new QPushButton(QStringLiteral("设置"));
    side->addWidget(preferences);
    connect(preferences, &QPushButton::clicked, this, [this] { emit command("settings"); });
    auto *version = new QLabel("Qt 5.14.2  ·  Windows");
    version->setObjectName("muted");
    side->addWidget(version);
    layout->addWidget(sidebar);
    auto *content = new QWidget;
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(30, 28, 30, 20);
    body->setSpacing(14);
    layout->addWidget(content, 1);
    auto *top = new QHBoxLayout;
    heading_ = new QLabel(QStringLiteral("截图历史"));
    heading_->setObjectName("heading");
    top->addWidget(heading_);
    top->addStretch();
    auto *import = new QPushButton(QStringLiteral("打开图片"));
    top->addWidget(import);
    connect(import, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开图片"), {},
                                                          "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
        if (!path.isEmpty())
            emit fileOpened(path);
    });
    auto *clip = new QPushButton(QStringLiteral("剪贴板贴图"));
    top->addWidget(clip);
    connect(clip, &QPushButton::clicked, this, [this] { emit command("clipboard"); });
    body->addLayout(top);
    summary_ = new QLabel;
    summary_->setObjectName("muted");
    body->addWidget(summary_);
    auto *filter = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("搜索日期、标题或分组"));
    filter->addWidget(search_, 1);
    auto *extra = new QPushButton(QStringLiteral("管理"));
    auto *menu = new QMenu(extra);
    for (const auto &pair :
         QVector<QPair<QString, QString>>{{QStringLiteral("截图预设 / 延时"), "preset"},
                                          {QStringLiteral("显示 / 隐藏全部贴图"), "toggle-pins"},
                                          {QStringLiteral("恢复最近关闭贴图"), "recover"},
                                          {QStringLiteral("导出备份"), "backup"},
                                          {QStringLiteral("导入备份"), "restore"},
                                          {QStringLiteral("清空截图历史"), "clear-history"},
                                          {QStringLiteral("退出 EasySnip"), "quit"}})
        connect(menu->addAction(pair.first), &QAction::triggered, this,
                [this, pair] { emit command(pair.second); });
    extra->setMenu(menu);
    filter->addWidget(extra);
    body->addLayout(filter);
    gallery_ = new QListWidget;
    gallery_->setViewMode(QListView::IconMode);
    gallery_->setResizeMode(QListView::Adjust);
    gallery_->setMovement(QListView::Static);
    gallery_->setIconSize(QSize(uiPixels(220), uiPixels(150)));
    gallery_->setGridSize(QSize(uiPixels(246), uiPixels(208)));
    gallery_->setSpacing(8);
    gallery_->setWordWrap(true);
    gallery_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    gallery_->setContextMenuPolicy(Qt::CustomContextMenu);
    body->addWidget(gallery_, 1);
    empty_ = new QLabel(QStringLiteral("按 F1 捕捉第一个画面\n也可以打开图片，或把图片文件拖到这里。"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QString("color:#8fa0b9;font-size:%1px;padding:50px").arg(uiPixels(17)));
    body->addWidget(empty_);
    auto *help = new QLabel(QStringLiteral("双击编辑 · 右键管理 · 贴图滚轮缩放 · Ctrl + 滚轮调整透明度"));
    help->setObjectName("muted");
    body->addWidget(help);
    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 3)
            library_.setActiveGroup(navigation_->item(row)->data(Qt::UserRole).toString());
        else
            refresh();
    });
    connect(search_, &QLineEdit::textChanged, this, &LibraryWindow::refresh);
    connect(gallery_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit recordAction(item->data(Qt::UserRole).toString(), "edit");
    });
    connect(gallery_, &QListWidget::customContextMenuRequested, this, &LibraryWindow::itemMenu);
    connect(&library_, &Library::changed, this, &LibraryWindow::refresh);
    refresh();
}
void LibraryWindow::refresh() {
    const QString selectedId =
        gallery_->currentItem() ? gallery_->currentItem()->data(Qt::UserRole).toString() : QString();
    int row = navigation_->currentRow();
    if (row < 0)
        row = 0;
    const QString group = row >= 3 && navigation_->item(row)
                              ? navigation_->item(row)->data(Qt::UserRole).toString()
                              : QString();
    {
        const QSignalBlocker block(navigation_);
        navigation_->clear();
        navigation_->addItems(
            {QStringLiteral("截图历史"), QStringLiteral("所有贴图"), QStringLiteral("已关闭贴图")});
        for (const auto &name : library_.groups()) {
            auto *item = new QListWidgetItem(name, navigation_);
            item->setData(Qt::UserRole, name);
        }
        if (!group.isEmpty())
            row = qMax(0, library_.groups().indexOf(group) + 3);
        row = qMin(row, navigation_->count() - 1);
        navigation_->setCurrentRow(row);
    }
    gallery_->clear();
    const QString query = search_->text().trimmed();
    int count = 0;
    heading_->setText(navigation_->currentItem()->text());
    for (const auto &record : library_.records()) {
        const bool visible = row == 0   ? record.history
                             : row == 1 ? record.pinned && !record.closed
                             : row == 2 ? record.pinned && record.closed
                                        : record.pinned && !record.closed &&
                                              record.group == navigation_->currentItem()->text();
        if (!visible ||
            (!query.isEmpty() && !(record.title + record.group).contains(query, Qt::CaseInsensitive)))
            continue;
        auto *item = new QListWidgetItem(QIcon(library_.previewPath(record)),
                                         record.title + QString("\n%1 × %2  ·  %3")
                                                            .arg(record.imageSize.width())
                                                            .arg(record.imageSize.height())
                                                            .arg(record.group),
                                         gallery_);
        item->setData(Qt::UserRole, record.id);
        item->setTextAlignment(Qt::AlignHCenter);
        if (record.id == selectedId)
            gallery_->setCurrentItem(item);
        ++count;
    }
    summary_->setText(
        QStringLiteral("%1 张图像    ·    当前贴图分组  %2").arg(count).arg(library_.activeGroup()));
    empty_->setVisible(count == 0);
    gallery_->setVisible(count > 0);
}
void LibraryWindow::itemMenu(const QPoint &point) {
    const auto selected = gallery_->selectedItems();
    if (selected.isEmpty())
        return;
    QStringList ids;
    for (auto *item : selected)
        ids.append(item->data(Qt::UserRole).toString());
    if (ids.size() > 1 && settings_.value("pins/includeActiveInMultiSelect", false).toBool()) {
        const QString active = qApp->property("EasySnipActivePin").toString();
        if (!active.isEmpty() && !library_.record(active).id.isEmpty() && !ids.contains(active))
            ids.append(active);
    }
    QMenu menu;
    for (const auto &pair : QVector<QPair<QString, QString>>{{QStringLiteral("标注编辑"), "edit"},
                                                             {QStringLiteral("贴到屏幕 / 恢复"), "pin"},
                                                             {QStringLiteral("复制图像"), "copy"},
                                                             {QStringLiteral("另存为"), "save-as"},
                                                             {QStringLiteral("关闭贴图"), "close"},
                                                             {QStringLiteral("重命名"), "rename"},
                                                             {QStringLiteral("销毁记录"), "destroy"}})
        connect(menu.addAction(pair.first), &QAction::triggered, this, [this, ids, pair] {
            for (const auto &id : ids)
                emit recordAction(id, pair.second);
        });
    auto *groups = menu.addMenu(QStringLiteral("移动到分组"));
    for (const auto &group : library_.groups())
        connect(groups->addAction(group), &QAction::triggered, this, [this, ids, group] {
            for (const auto &id : ids) {
                auto item = library_.record(id);
                item.group = group;
                library_.updateRecord(item);
            }
        });
    menu.exec(gallery_->viewport()->mapToGlobal(point));
}
void LibraryWindow::manageGroups() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("贴图分组"));
    dialog.resize(420, 440);
    QVBoxLayout layout(&dialog);
    QLabel hint(QStringLiteral("拖动排序，删除分组会将贴图移到第一个分组。"));
    hint.setWordWrap(true);
    layout.addWidget(&hint);
    QListWidget list;
    list.addItems(library_.groups());
    list.setDragDropMode(QAbstractItemView::InternalMove);
    layout.addWidget(&list);
    QHBoxLayout actions;
    QPushButton add(QStringLiteral("新增")), rename(QStringLiteral("重命名")), remove(QStringLiteral("删除"));
    actions.addWidget(&add);
    actions.addWidget(&rename);
    actions.addWidget(&remove);
    layout.addLayout(&actions);
    auto currentNames = [&] {
        QStringList names;
        for (int i = 0; i < list.count(); ++i)
            names.append(list.item(i)->text());
        return names;
    };
    connect(&add, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString name = QInputDialog::getText(&dialog, QStringLiteral("新增分组"),
                                                   QStringLiteral("名称"), QLineEdit::Normal, "", &ok)
                                 .trimmed();
        if (ok && !name.isEmpty() && !currentNames().contains(name))
            list.addItem(name);
    });
    connect(&rename, &QPushButton::clicked, &dialog, [&] {
        if (!list.currentItem())
            return;
        const QString old = list.currentItem()->text();
        bool ok = false;
        const QString name = QInputDialog::getText(&dialog, QStringLiteral("重命名"), QStringLiteral("名称"),
                                                   QLineEdit::Normal, old, &ok)
                                 .trimmed();
        if (!ok || name.isEmpty() || currentNames().contains(name))
            return;
        if (library_.groups().contains(old))
            library_.renameGroup(old, name);
        list.currentItem()->setText(name);
    });
    connect(&remove, &QPushButton::clicked, &dialog, [&] {
        if (list.count() > 1)
            delete list.takeItem(list.currentRow());
    });
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    dialog.exec();
    library_.setGroups(currentNames());
}
void LibraryWindow::closeEvent(QCloseEvent *event) {
    if (settings_.value("general/tray", true).toBool() && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
    } else {
        emit command("quit");
        event->accept();
    }
}
void LibraryWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}
void LibraryWindow::dropEvent(QDropEvent *event) {
    for (const auto &url : event->mimeData()->urls())
        if (url.isLocalFile())
            emit fileOpened(url.toLocalFile());
    event->acceptProposedAction();
}
