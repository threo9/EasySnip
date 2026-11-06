#pragma once
#include "document.h"
#include "settings.h"
#include <QDateTime>
#include <QJsonObject>
#include <QMainWindow>

class QListWidget;
class QLabel;
class QLineEdit;

struct Record {
    QString id;
    QString title;
    QDateTime created;
    QString sourceFile;
    QString previewFile;
    QJsonObject document;
    QSize imageSize;
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
    QString add(const Document &document);
    bool updateDocument(const QString &id, const Document &document);
    bool updateRecord(const Record &record);
    bool remove(const QString &id);
    void clearHistory();
    void prune();
    bool exportBackup(const QString &path);
    bool importBackup(const QString &path);
  signals:
    void changed();
    void error(QString message);

  private:
    bool saveDocument(Record record, const Document &document);
    bool writeRecord(const Record &record);
    void load();
    QString path(const QString &fileName) const;
    Settings &settings_;
    QString directory_;
    QVector<Record> records_;
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
    void itemMenu(const QPoint &point);
    Library &library_;
    Settings &settings_;
    QListWidget *gallery_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *empty_ = nullptr;
    QLineEdit *search_ = nullptr;
};
