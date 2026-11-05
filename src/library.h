#pragma once
#include "document.h"
#include "settings.h"
#include <QDateTime>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

class QListWidget;
class QLabel;
class QLineEdit;

struct Record {
    QString id;
    QString title;
    QDateTime created;
    QDateTime closedAt;
    QString group;
    QString sourceFile;
    QString previewFile;
    QJsonObject document;
    QSize imageSize;
    QRect geometry;
    double opacity = 1;
    bool history = true;
    bool pinned = false;
    bool closed = false;
    QJsonObject toJson() const;
    static Record fromJson(const QJsonObject &object);
};

class Library : public QObject {
    Q_OBJECT
  public:
    explicit Library(Settings &settings, QObject *parent = nullptr);
    QVector<Record> records() const;
    Record record(const QString &id) const;
    Document document(const QString &id) const;
    QString previewPath(const Record &record) const;
    QString add(const Document &document, bool history = true);
    bool updateDocument(const QString &id, const Document &document);
    bool updateRecord(const Record &record);
    bool remove(const QString &id);
    void clearHistory();
    void prune();
    QStringList groups() const;
    QString activeGroup() const;
    bool setGroups(const QStringList &groups);
    bool renameGroup(const QString &oldName, const QString &newName);
    void setActiveGroup(const QString &group);
    bool exportBackup(const QString &path);
    bool importBackup(const QString &path);
  signals:
    void changed();
    void error(QString message);

  private:
    bool saveState();
    bool saveDocument(Record record, const Document &document);
    bool writeRecord(const Record &record);
    void load();
    QString path(const QString &fileName) const;
    Settings &settings_;
    QString directory_;
    QVector<Record> records_;
    QStringList groups_;
    QString activeGroup_;
};

class PinWindow : public QWidget {
    Q_OBJECT
  public:
    PinWindow(const Record &record, Document document, Settings &settings, const QStringList &groups);
    QString recordId() const;
    void setDocument(const Document &document);
    void setGroups(const QStringList &groups);
  signals:
    void actionRequested(QString id, QString action);
    void geometryChanged(QString id, QRect geometry, double opacity);
    void groupRequested(QString id, QString group);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

  private:
    void scheduleSave();
    QString id_;
    Document document_;
    QImage image_;
    Settings &settings_;
    QStringList groups_;
    QPoint dragOffset_;
    QPoint dragOrigin_;
    QSize originalSize_;
    bool dragging_ = false;
    bool resizing_ = false;
    bool picker_ = false;
    QPoint hover_;
    QTimer saveTimer_;
};

class LibraryWindow : public QMainWindow {
    Q_OBJECT
  public:
    LibraryWindow(Library &library, Settings &settings);
    void refresh();
  signals:
    void command(QString action);
    void recordAction(QString id, QString action);
    void fileOpened(QString path);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    void manageGroups();
    void itemMenu(const QPoint &point);
    Library &library_;
    Settings &settings_;
    QListWidget *navigation_ = nullptr;
    QListWidget *gallery_ = nullptr;
    QLabel *heading_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *empty_ = nullptr;
    QLineEdit *search_ = nullptr;
};
