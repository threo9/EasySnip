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
            {"source", sourceFile},
            {"preview", previewFile},
            {"document", document},
            {"width", imageSize.width()},
            {"height", imageSize.height()}};
}
Record Record::fromJson(const QJsonObject &object) {
    Record record;
    record.id = object["id"].toString();
    record.title = object["title"].toString();
    record.created = QDateTime::fromString(object["created"].toString(), Qt::ISODateWithMs);
    record.sourceFile = object["source"].toString();
    record.previewFile = object["preview"].toString();
    record.document = object["document"].toObject();
    record.imageSize = QSize(object["width"].toInt(), object["height"].toInt());
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
QString Library::add(const Document &document) {
    Record record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.created = QDateTime::currentDateTime();
    record.title = record.created.toString("yyyy-MM-dd HH:mm:ss");
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
    for (const auto &item : snapshot)
        remove(item.id);
}
void Library::prune() {
    const int limit = qBound(0, settings_.value("history/limit", 100).toInt(), 10000);
    const auto snapshot = records_;
    for (int i = limit; i < snapshot.size(); ++i)
        remove(snapshot[i].id);
}
bool Library::exportBackup(const QString &fileName) {
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_14);
    const QJsonObject header{{"count", records_.size()}};
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
    std::sort(records_.begin(), records_.end(),
              [](const Record &a, const Record &b) { return a.created > b.created; });
    prune();
    emit changed();
    return true;
}

LibraryWindow::LibraryWindow(Library &library, Settings &settings) : library_(library), settings_(settings) {
    setWindowTitle(QStringLiteral("EasySnip · 截图历史"));
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
    auto *tagline = new QLabel(QStringLiteral("截图 · 标注 · 保存"));
    tagline->setObjectName("muted");
    side->addWidget(tagline);
    auto *capture = new QPushButton(QStringLiteral("＋  开始截图"));
    capture->setObjectName("primary");
    side->addWidget(capture);
    connect(capture, &QPushButton::clicked, this, [this] { emit command("capture"); });
    side->addStretch(1);
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
    auto *heading = new QLabel(QStringLiteral("截图历史"));
    heading->setObjectName("heading");
    top->addWidget(heading);
    top->addStretch();
    auto *import = new QPushButton(QStringLiteral("打开图片"));
    top->addWidget(import);
    connect(import, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开图片"), {},
                                                          "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
        if (!path.isEmpty())
            emit fileOpened(path);
    });
    body->addLayout(top);
    summary_ = new QLabel;
    summary_->setObjectName("muted");
    body->addWidget(summary_);
    auto *filter = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("搜索日期或标题"));
    filter->addWidget(search_, 1);
    auto *extra = new QPushButton(QStringLiteral("管理"));
    auto *menu = new QMenu(extra);
    for (const auto &pair :
         QVector<QPair<QString, QString>>{{QStringLiteral("截图预设 / 延时"), "preset"},
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
    empty_ = new QLabel(QStringLiteral("按 %1 捕捉第一个画面\n也可以打开图片，或把图片文件拖到这里。")
                           .arg(settings_.shortcut("capture")));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setStyleSheet(QString("color:#8fa0b9;font-size:%1px;padding:50px").arg(uiPixels(17)));
    body->addWidget(empty_);
    auto *help = new QLabel(QStringLiteral("双击编辑 · 右键复制、保存或管理截图"));
    help->setObjectName("muted");
    body->addWidget(help);
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
    gallery_->clear();
    const QString query = search_->text().trimmed();
    int count = 0;
    for (const auto &record : library_.records()) {
        const QString searchable = record.title + " " + record.created.toString("yyyy-MM-dd HH:mm:ss");
        if (!query.isEmpty() && !searchable.contains(query, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(QIcon(library_.previewPath(record)),
                                         record.title + QString("\n%1 × %2")
                                                            .arg(record.imageSize.width())
                                                            .arg(record.imageSize.height()),
                                         gallery_);
        item->setData(Qt::UserRole, record.id);
        item->setTextAlignment(Qt::AlignHCenter);
        if (record.id == selectedId)
            gallery_->setCurrentItem(item);
        ++count;
    }
    summary_->setText(QStringLiteral("%1 张图像").arg(count));
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
    QMenu menu;
    for (const auto &pair : QVector<QPair<QString, QString>>{{QStringLiteral("标注编辑"), "edit"},
                                                             {QStringLiteral("复制图像"), "copy"},
                                                             {QStringLiteral("另存为"), "save-as"},
                                                             {QStringLiteral("重命名"), "rename"},
                                                             {QStringLiteral("销毁记录"), "destroy"}})
        connect(menu.addAction(pair.first), &QAction::triggered, this, [this, ids, pair] {
            for (const auto &id : ids)
                emit recordAction(id, pair.second);
        });
    menu.exec(gallery_->viewport()->mapToGlobal(point));
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
