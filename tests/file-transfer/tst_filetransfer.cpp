#include <QGuiApplication>
#include <QTest>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QSslSocket>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTranslator>
#include "backend/filetransfer.h"
#include "backend/identitymanager.h"
#include "desk_files.h"

// Loopback TLS fixture runs the actual host filesystem implementation, not a mock filesystem.
class FileServer : public QTcpServer {
public:
    desk_files::Service service;
    QSslConfiguration identity;
    int requests = 0;
    explicit FileServer(const QString& root) : service(std::filesystem::u8path(root.toUtf8().constData())), identity(IdentityManager::get()->getSslConfig()) {}
    void incomingConnection(qintptr descriptor) override {
        auto socket = new QSslSocket(this);
        socket->setSslConfiguration(identity);
        socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QSslSocket::sslErrors, socket, [this, socket](const QList<QSslError>& errors) {
            if (socket->peerCertificate() == identity.localCertificate()) socket->ignoreSslErrors(errors);
        });
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            auto split = buffer->indexOf("\r\n\r\n"); if (split < 0) return;
            qint64 length = 0;
            for (auto line : buffer->left(split).split('\n')) if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < split + 4 + length) return;
            ++requests;
            QByteArray result;
            try { result = QByteArray::fromStdString(service.execute(desk_files::json::parse(buffer->mid(split + 4, length).toStdString())).dump()); }
            catch (...) { result = "{\"ok\":false,\"error\":\"Malformed request\"}"; }
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(result.size()) + "\r\n\r\n" + result);
            socket->disconnectFromHost();
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};
class TransferTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static void write(const QString& path, const QByteArray& content) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(content), content.size()); }
    static QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
private slots:
    void initTestCase() {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
        QQuickStyle::setStyle("Material");
        QCoreApplication::setOrganizationName("DeskTransferTest");
        QCoreApplication::setApplicationName("isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QVERIFY(QSslSocket::supportsSsl());
        IdentityManager::get();
    }
    void sandboxAndAtomicWrites() {
        QTemporaryDir root, outside;
        desk_files::Service service(std::filesystem::u8path(root.path().toStdString()));
        auto op = [&](desk_files::json q) { return service.execute(q); };
        for (const auto& p : {"../escape", "/etc/passwd", "C:/Windows", "dir/../../escape", ".desk-transfers/secret", "x\\y", "file:stream"}) QVERIFY(!op({{"op", "stat"}, {"path", p}})["ok"].get<bool>());
        std::error_code ec;
        std::filesystem::create_directory_symlink(std::filesystem::u8path(outside.path().toStdString()), std::filesystem::u8path((root.path()+"/link").toStdString()), ec);
        if (!ec) QVERIFY(!op({{"op", "stat"}, {"path", "link/file"}})["ok"].get<bool>());
        auto begin = op({{"op", "begin"}, {"path", "test.txt"}, {"size", 3}, {"expected", "missing"}});
        QVERIFY(begin["ok"].get<bool>()); auto token = begin["token"];
        QVERIFY(!QFile::exists(root.filePath("test.txt")));
        QVERIFY(!op({{"op", "write"}, {"token", token}, {"offset", 1}, {"data", "YWJj"}})["ok"].get<bool>());
        QVERIFY(op({{"op", "write"}, {"token", token}, {"offset", 0}, {"data", "YWJj"}})["ok"].get<bool>());
        QVERIFY(!op({{"op", "finish"}, {"token", token}, {"sha256", "bad"}})["ok"].get<bool>());
        QVERIFY(!QFile::exists(root.filePath("test.txt")));
        QVERIFY(QDir(root.filePath(".desk-transfers")).isEmpty());
        write(root.filePath("keep.txt"), "original");
        begin = op({{"op", "begin"}, {"path", "keep.txt"}, {"size", 0}, {"expected", "missing"}});
        QVERIFY(!begin["ok"].get<bool>()); QCOMPARE(read(root.filePath("keep.txt")), QByteArray("original"));
        QVERIFY(!op({{"op", "read"}, {"path", "keep.txt"}, {"offset", 0}, {"version", "stale"}})["ok"].get<bool>());
    }
    void uploadDownloadFoldersAndConflicts() {
        QTemporaryDir remote, local, destination;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.setProperty("limitMiB", 0);
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseLocal(local.path());
        QByteArray data(700000, '\0'); for (int i=0; i<data.size(); ++i) data[i] = char(i % 251);
        write(local.filePath(QString::fromUtf8("中文 空格.bin")), data);
        QDir(local.path()).mkdir("folder"); QDir(local.filePath("folder")).mkdir("empty-dir"); write(local.filePath("folder/zero"), ""); write(local.filePath("folder/nested.txt"), "nested"); write(local.filePath("folder/.hidden"), "hidden");
        client.enqueue(true, {QString::fromUtf8("中文 空格.bin"), "folder"});
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 20000);
        for (auto j : client.jobs()) QCOMPARE(j.toMap()["state"].toString(), QString("done"));
        QCOMPARE(read(remote.filePath(QString::fromUtf8("中文 空格.bin"))), data);
        QCOMPARE(read(remote.filePath("folder/nested.txt")), QByteArray("nested"));
        QVERIFY(QDir(remote.filePath("folder/empty-dir")).exists());
        QVERIFY(QFile::exists(remote.filePath("folder/zero")));
        client.clearFinished();
        client.browseLocal(destination.path()); client.enqueue(false, {QString::fromUtf8("中文 空格.bin"), "folder"});
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 20000);
        for (auto j : client.jobs()) QVERIFY2(j.toMap()["state"].toString() == "done", qPrintable(j.toMap()["detail"].toString()));
        QCOMPARE(read(destination.filePath(QString::fromUtf8("中文 空格.bin"))), data);
        QCOMPARE(read(destination.filePath("folder/nested.txt")), QByteArray("nested"));
        QCOMPARE(read(destination.filePath("folder/.hidden")), QByteArray("hidden"));
        client.clearFinished();
        client.enqueue(false, {QString::fromUtf8("中文 空格.bin")});
        QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000);
        client.resolveConflict("keep"); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(destination.filePath(QString::fromUtf8("中文 空格 (1).bin"))), data);
        client.clearFinished(); client.browseLocal(local.path());
        write(local.filePath(QString::fromUtf8("中文 空格.bin")), "changed");
        client.enqueue(true, {QString::fromUtf8("中文 空格.bin")});
        QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000);
        client.resolveConflict("overwrite"); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(remote.filePath(QString::fromUtf8("中文 空格.bin"))), QByteArray("changed"));
        client.clearFinished(); client.enqueue(true, {QString::fromUtf8("中文 空格.bin")});
        QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000);
        client.resolveConflict("skip"); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(client.jobs().first().toMap()["state"].toString(), QString("skipped"));
    }
    void cancelKeepsSourceAndExistingDestination() {
        QTemporaryDir remote, local;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000); client.browseLocal(local.path()); client.setProperty("limitMiB", 1);
        write(local.filePath("large.bin"), QByteArray(3 * 1024 * 1024, 'a')); write(remote.filePath("large.bin"), "old");
        client.enqueue(true, {"large.bin"}); QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000); client.resolveConflict("overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(client.jobs().first().toMap()["done"].toLongLong() > 0, 10000);
        client.cancel(0); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QDir(remote.filePath(".desk-transfers")).isEmpty(), 10000);
        QCOMPARE(read(remote.filePath("large.bin")), QByteArray("old")); QCOMPARE(QFileInfo(local.filePath("large.bin")).size(), qint64(3 * 1024 * 1024));
        client.retry(0); QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000); client.resolveConflict("overwrite");
        client.setProperty("limitMiB", 0); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 15000);
        QCOMPARE(QFileInfo(remote.filePath("large.bin")).size(), qint64(3 * 1024 * 1024));
        client.clearFinished();
        write(local.filePath("large.bin"), "local-original");
        client.setProperty("limitMiB", 1);
        client.enqueue(false, {"large.bin"}); QTRY_VERIFY_WITH_TIMEOUT(!client.conflict().isEmpty(), 10000); client.resolveConflict("overwrite");
        QTRY_VERIFY_WITH_TIMEOUT(client.jobs().first().toMap()["done"].toLongLong() > 0, 10000);
        client.cancel(0); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(local.filePath("large.bin")), QByteArray("local-original"));
    }
    void qmlLoadsAndRenders() {
        QTemporaryDir remote, local;
        QDir(local.path()).mkdir(QString::fromUtf8("设计素材"));
        write(local.filePath(QString::fromUtf8("项目说明.pdf")), "Preview");
        QDir(remote.path()).mkdir(QString::fromUtf8("工作文件"));
        write(remote.filePath(QString::fromUtf8("会议记录.txt")), "Preview");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QTranslator translator;
        QVERIFY(translator.load(QFINDTESTDATA("../../app/languages/qml_zh_CN.qm")));
        QCoreApplication::installTranslator(&translator);
        for (bool dark : {false, true}) {
            QQmlApplicationEngine engine;
            QStringList warnings;
            connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors) { for (const auto& error : errors) warnings << error.toString(); });
            engine.rootContext()->setContextProperty("transfer", &client);
            engine.rootContext()->setContextProperty("hostName", "Windows PC");
            engine.rootContext()->setContextProperty("transferDark", dark);
            engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join("\n")));
            auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
            QTest::qWait(400);
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
            const auto output = qEnvironmentVariable("DESK_SCREENSHOT_DIR");
            if (!output.isEmpty()) {
                QDir().mkpath(output);
                QVERIFY(window->grabWindow().save(QDir(output).filePath(dark ? "file-transfer-dark.png" : "file-transfer-light.png")));
            }
            if (!dark) {
                std::function<QQuickItem*(QQuickItem*, const QString&)> findItem;
                findItem = [&findItem](QQuickItem* parent, const QString& name) -> QQuickItem* {
                    if (parent->objectName() == name) return parent;
                    for (auto child : parent->childItems()) if (auto found = findItem(child, name)) return found;
                    return nullptr;
                };
                auto source = findItem(window->contentItem(), "localFileRow1");
                auto target = findItem(window->contentItem(), "remoteFilePane");
                QVERIFY(source); QVERIFY(target);
                const QPoint from = source->mapToScene(QPointF(140, 20)).toPoint();
                const QPoint to = target->mapToScene(QPointF(170, target->height() - 100)).toPoint();
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
                for (int step = 1; step <= 10; ++step) QTest::mouseMove(window, from + (to - from) * step / 10, 20);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
                QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(remote.filePath(QString::fromUtf8("项目说明.pdf"))), 10000);
                QCOMPARE(read(remote.filePath(QString::fromUtf8("项目说明.pdf"))), QByteArray("Preview"));
                QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
            }
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
            window->hide();
        }
        QCoreApplication::removeTranslator(&translator);
    }
    void finishedHistorySurvivesWindowRestart() {
        QTemporaryDir remote, local;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        write(local.filePath("history.txt"), "history");
        {
            FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
            QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
            client.browseLocal(local.path()); client.enqueue(true, {"history.txt"});
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
            QCOMPARE(client.jobs().last().toMap()["state"].toString(), QString("done"));
        }
        FileTransfer restored("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QCOMPARE(restored.jobs().last().toMap()["name"].toString(), QString("history.txt"));
        QCOMPARE(restored.jobs().last().toMap()["state"].toString(), QString("done"));
        QVERIFY(!restored.busy()); restored.clearFinished();
        FileTransfer empty("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QVERIFY(empty.jobs().isEmpty());
    }
    void rejectsUnpairedAndInvalidNames() {
        QTemporaryDir remote;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), QSslCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(!client.error().isEmpty(), 2000); QVERIFY(!client.ready()); QCOMPARE(server.requests, 0);
        for (const auto& name : {"..", ".", "bad/name", "bad\\name", "file:stream", "foo.", "foo ", ".desk-transfers", "CON", "nul.txt", "COM1.log"}) QVERIFY(!FileTransfer::validName(name));
        QVERIFY(FileTransfer::validName(QString::fromUtf8("中文 文档.txt")));
    }
};
QTEST_MAIN(TransferTest)
#include "tst_filetransfer.moc"
