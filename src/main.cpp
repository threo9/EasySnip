#include "application.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QTranslator>
#include <QLibraryInfo>
#include <QMessageBox>

int runSelfTests(const QString &directory, bool native);

namespace {
QString logPath;
qint64 logLimit = 2 * 1024 * 1024;
int logRotations = 3;
QMutex logMutex;
QStringList pendingLogs;
void flushLogLocked() {
    if (logPath.isEmpty())
        return;
    if (pendingLogs.isEmpty())
        return;
    if (QFileInfo(logPath).size() > logLimit) {
        QFile::remove(logPath + '.' + QString::number(logRotations));
        for (int i = logRotations - 1; i >= 1; --i)
            QFile::rename(logPath + '.' + QString::number(i), logPath + '.' + QString::number(i + 1));
        QFile::rename(logPath, logPath + ".1");
    }
    QFile file(logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        for (const auto &line : pendingLogs)
            out << line << '\n';
        out.flush();
        pendingLogs.clear();
    }
}
void logMessage(QtMsgType type, const QMessageLogContext &, const QString &message) {
    QMutexLocker locker(&logMutex);
    pendingLogs.append(QDateTime::currentDateTime().toString(Qt::ISODateWithMs) +
                       QString(" [%1] ").arg(int(type)) + message);
    if (type == QtFatalMsg || pendingLogs.size() >= 100)
        flushLogLocked();
}
} // namespace

int main(int argc, char *argv[]) {
    // Native capture, mouse input and selections share physical-pixel coordinates.
    Native::enableDpiAwareness();
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication app(argc, argv);
    QApplication::setApplicationName("EasySnip");
    QApplication::setOrganizationName("EasySnip");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setQuitOnLastWindowClosed(false);
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_zh_CN", QApplication::applicationDirPath() + "/translations") ||
        qtTranslator.load("qt_zh_CN", QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EasySnip — Windows 截图与标注工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    for (const QString &option : {"tray", "capture", "scroll", "screen", "fullscreen", "active", "last", "settings", "quit", "admin", "native-smoke-test"})
        parser.addOption(QCommandLineOption(option));
    parser.addOption(QCommandLineOption("open", "Open an image file", "path"));
    parser.addOption(QCommandLineOption("data-dir", "Override application data directory", "path"));
    parser.addOption(
        QCommandLineOption("self-test", "Run deterministic regression tests and UI renders", "directory"));
    parser.process(app);
    if (parser.isSet("self-test"))
        return runSelfTests(parser.value("self-test"), parser.isSet("native-smoke-test"));
    Settings settings(parser.value("data-dir"));
    logPath = settings.value("log/file", settings.dataDirectory() + "/EasySnip.log").toString();
    logLimit = qMax(65536LL, settings.value("log/maxBytes", 2 * 1024 * 1024).toLongLong());
    logRotations = qBound(1, settings.value("log/rotations", 3).toInt(), 20);
    qInstallMessageHandler(logMessage);
    QTimer logTimer;
    QObject::connect(&logTimer, &QTimer::timeout, &app, [] {
        QMutexLocker locker(&logMutex);
        flushLogLocked();
    });
    QObject::connect(&app, &QApplication::aboutToQuit, &app, [] {
        QMutexLocker locker(&logMutex);
        flushLogLocked();
    });
    logTimer.start(qBound(100, settings.value("log/flushMs", 1000).toInt(), 60000));
    const QString serverName =
        "EasySnip-" + QString::number(qHash(QDir(settings.dataDirectory()).canonicalPath().toLower()), 16);
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(250)) {
        socket.write(
            QJsonDocument(QJsonArray::fromStringList(app.arguments())).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        return 0;
    }
    if (!parser.isSet("quit") && !Native::isElevated() &&
        (parser.isSet("admin") || settings.value("runtime/administrator", false).toBool())) {
        QString error;
        const bool launched = Native::restartElevated(app.arguments(), &error);
        if (!error.isEmpty())
            QMessageBox::critical(nullptr, QStringLiteral("EasySnip 管理员启动"), error);
        qInfo() << "Administrator launch requested" << launched << error;
        QMutexLocker locker(&logMutex);
        flushLogLocked();
        return launched || error.isEmpty() ? 0 : 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) {
        qCritical() << "Cannot create single-instance server" << server.errorString();
        return 1;
    }
    Application controller(settings);
    qInfo() << "EasySnip started, pid" << QApplication::applicationPid()
            << "elevated" << Native::isElevated() << "capture key" << settings.shortcut("capture");
    QObject::connect(&server, &QLocalServer::newConnection, &controller, [&] {
        while (server.hasPendingConnections()) {
            auto *client = server.nextPendingConnection();
            const auto readCommand = [client, &controller] {
                if (client->bytesAvailable() > 65536) {
                    client->disconnectFromServer();
                    return;
                }
                if (!client->canReadLine())
                    return;
                QStringList args;
                for (const auto &item : QJsonDocument::fromJson(client->readLine()).array())
                    args.append(item.toString());
                controller.processArguments(args);
                client->disconnectFromServer();
            };
            QObject::connect(client, &QLocalSocket::readyRead, &controller, readCommand);
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            readCommand();
        }
    });
    QTimer::singleShot(0, &controller, [&] { controller.start(app.arguments()); });
    const int result = app.exec();
    settings.sync();
    return result;
}
