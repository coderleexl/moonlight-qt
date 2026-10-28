#include <QGuiApplication>
#include <QTest>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QSslSocket>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTranslator>
#include <QTimer>
#include "backend/filetransfer.h"
#include "backend/nativefiles/offerstore.h"
#include "backend/nativefiles/jsontransport.h"
#include "backend/identitymanager.h"
#include "../../third_party/sunshine/src/desk_files.h"

// Loopback TLS fixture runs the actual host filesystem implementation, not a mock filesystem.
class FileServer : public QTcpServer {
public:
    desk_files::Service service;
    QSslConfiguration identity;
    int requests = 0;
    bool delayedClose = false;
    bool legacyProtocol = false;
    QList<QJsonObject> operations;
    explicit FileServer(const QString& root, bool filesystemAccess = false)
        : service(std::filesystem::u8path(root.toUtf8().constData()), filesystemAccess),
          identity(IdentityManager::get()->getSslConfig()) {}
    void incomingConnection(qintptr descriptor) override {
        auto socket = new QSslSocket(this);
        socket->setSslConfiguration(identity);
        socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QSslSocket::sslErrors, socket, [this, socket](const QList<QSslError>& errors) {
            if (socket->peerCertificate() == identity.localCertificate()) socket->ignoreSslErrors(errors);
        });
        auto buffer = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        connect(socket, &QSslSocket::readyRead, socket, [this, socket, buffer, handled] {
            if (*handled) { socket->abort(); return; }
            buffer->append(socket->readAll());
            auto split = buffer->indexOf("\r\n\r\n"); if (split < 0) return;
            qint64 length = 0;
            for (auto line : buffer->left(split).split('\n')) if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < split + 4 + length) return;
            *handled = true;
            ++requests;
            QByteArray result;
            try {
                const auto request = desk_files::json::parse(buffer->mid(split + 4, length).toStdString());
                operations.append(QJsonDocument::fromJson(buffer->mid(split + 4, length)).object());
                auto response = legacyProtocol && request.value("op", std::string()) == "capabilities"
                    ? desk_files::json { { "ok", false }, { "error", "Unknown operation" } }
                    : service.execute(request);
                if (legacyProtocol && request.value("op", std::string()) == "list" && response.value("ok", false))
                    response = { { "ok", true }, { "protocol", 1 }, { "root", response["root"] }, { "entries", response["entries"] } };
                result = QByteArray::fromStdString(response.dump());
            }
            catch (...) { result = "{\"ok\":false,\"error\":\"Malformed request\"}"; }
            socket->write(QByteArray("HTTP/1.1 200 OK\r\n") + (delayedClose ? "" : "Connection: close\r\n")
                + "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(result.size()) + "\r\n\r\n" + result);
            if (delayedClose)
                QTimer::singleShot(100, socket, &QSslSocket::disconnectFromHost);
            else
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
    static QQuickItem* findItem(QQuickItem* parent, const QString& name) {
        if (parent->objectName() == name) return parent;
        for (auto child : parent->childItems()) if (auto found = findItem(child, name)) return found;
        return nullptr;
    }
    static QStringList selectedNames(QQuickItem* pane) {
        return pane->property("selected").value<QJSValue>().toVariant().toStringList();
    }
    static bool confirmTransfer(QQuickWindow* window) {
        QElapsedTimer elapsed; elapsed.start();
        while (elapsed.elapsed() < 5000) {
            auto button = findItem(window->contentItem(), "confirmTransferButton");
            if (button && button->isVisible() && button->isEnabled()) {
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
                return true;
            }
            QTest::qWait(20);
        }
        return false;
    }
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
        for (const auto& p : {"../escape", "/etc/passwd", "/", "C:/Windows", "C:relative", "dir/../../escape", ".desk-transfers/secret", ".DESK-TRANSFERS/secret", "x\\y", "file:stream"}) {
            const auto result = op({{"op", "stat"}, {"path", p}});
            QVERIFY2(!result["ok"].get<bool>(), p);
        }
#ifdef Q_OS_WIN
        for (const auto& p : {"CON", "nul.txt", "NUL.backup.txt", "COM1.log", "Lpt9.backup.txt"})
            QVERIFY2(!op({{"op", "stat"}, {"path", p}})["ok"].get<bool>(), p);
#endif
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
    void nativeTransportUsesPinnedTls() {
        QTemporaryDir root;
        FileServer server(root.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        NativeTransport transport;
        transport.url = QUrl(QString("https://127.0.0.1:%1/desk/files").arg(server.serverPort()));
        transport.identity = IdentityManager::get()->getSslConfig();
        transport.peer = server.identity.localCertificate();
        transport.timeoutMs = 3000;
        QVERIFY(transport.call({{"op", "list"}, {"path", ""}})["ok"].toBool());
        // No pin must fail before sending file metadata to the endpoint.
        const auto requests = server.requests;
        transport.peer = QSslCertificate();
        QVERIFY(!transport.call({{"op", "list"}, {"path", ""}})["ok"].toBool());
        QCOMPARE(server.requests, requests);
    }
    void nativeAdapterReadsOnDemand() {
        QTemporaryDir root,target;
        const auto bytes=QByteArray(900000,'n');write(root.filePath("原生文件.bin"),bytes);
        NativeOfferStore store;auto offer=store.publish({root.filePath("原生文件.bin")});QVERIFY(offer["ok"].toBool());
        QCOMPARE(store.bytesRead(),qint64(0));
        auto reader=[&](const QString& id,qint64 offset,int length){return store.read(offer["id"].toString(),id,offset,length);};
        auto result=nativeAdapterProbe(offer,reader,target.filePath("renamed.bin"));
        if(result["skip"].toBool())QSKIP(qPrintable(result["error"].toString()));
        QVERIFY(result["ok"].toBool());QCOMPARE(read(target.filePath("renamed.bin")),bytes);
        QVERIFY(store.bytesRead()>0);
    }
#ifdef Q_OS_MACOS
    void nativeImageClipboardRemainsAnImage() {
        QTemporaryDir root;
        write(root.filePath("photo.png"), "cached image");
        const auto result = nativeImageClipboardProbe(root.filePath("photo.png"));
        QCOMPARE(result["fileCount"].toInt(), 1); // Finder file copies still work.
        QCOMPARE(result["imageFileCount"].toInt(), 0);
        QVERIFY(result["unchanged"].toBool());
    }
    void nativeChatClipboardRemainsRich() {
        QTemporaryDir root;
        write(root.filePath("photo.png"), "cached picture");
        const auto result = nativeImageClipboardProbe(root.filePath("photo.png"));
        const auto cases = result["richCopies"].toArray();
        QCOMPARE(cases.size(), 5);
        for (const auto value : cases) {
            const auto entry = value.toObject();
            QVERIFY(entry["declared"].toBool());
            QVERIFY2(entry["files"].toInt() == 0, qPrintable(entry["type"].toString()));
            QVERIFY(entry["unchanged"].toBool());
        }
    }
    void nativeClipboardReadsOnlyExplicitFiles() {
        QTemporaryDir root;
        write(root.filePath("photo.png"), "cached picture");
        const auto result = nativeImageClipboardProbe(root.filePath("photo.png"));
        const auto cases = result["deferredCopies"].toArray();
        QCOMPARE(cases.size(), 9);
        QCOMPARE(result["legacyFiles"].toInt(), 0);
        QCOMPARE(result["legacyReads"].toInt(), 0);
        QVERIFY(result["legacyUnchanged"].toBool());
        for (const auto value : cases) {
            const auto entry = value.toObject();
            const auto name = entry["name"].toString();
            const auto diagnostic = QJsonDocument(entry).toJson(QJsonDocument::Compact);
            QVERIFY2(entry["written"].toBool(), diagnostic.constData());
            const bool explicitFile = name == "explicit-file" || name == "mixed-items";
            const bool replaced = name == "replaced-during-read";
            const bool imageAdded = name == "image-added-during-read";
            QVERIFY2(entry["files"].toInt() == (explicitFile ? 1 : 0), diagnostic.constData());
            // No text, generic URL, private application data or image promises
            // should be requested just because the clipboard changed.
            const int expectedReads = explicitFile || replaced || imageAdded ? 1 : 0;
            QVERIFY2(entry["reads"].toInt() == expectedReads, diagnostic.constData());
            QCOMPARE(entry["fileReads"].toInt(), expectedReads);
            QCOMPARE(entry["unchanged"].toBool(), !replaced);
            if (replaced || imageAdded)
                QVERIFY(entry["hasImage"].toBool());
        }
    }
    void nativePasteOnlyInspectsMatchingDestinations() {
        QTemporaryDir source, destination;
        const QByteArray content(500000, 'p');
        write(source.filePath("photo.png"), content);
        NativeOfferStore store;
        const auto offer = store.publish({source.filePath("photo.png")});
        const auto path = destination.filePath("photo 2.png");
        const auto result = nativePasteWatcherProbe(offer, [&](const QString& id, qint64 offset, int length) {
            return store.read(offer["id"].toString(), id, offset, length);
        }, path);
        QVERIFY2(result["published"].toBool(), QJsonDocument(result).toJson().constData());
        QCOMPARE(result["beforePasteReads"].toInt(), 0);
        QCOMPARE(result["unrelatedReads"].toInt(), 0);
        QCOMPARE(result["photoReads"].toInt(), 0);
        QCOMPARE(result["libraryReads"].toInt(), 0);
        QVERIFY(result["copied"].toBool());
        QCOMPARE(result["pasteReads"].toInt(), 1);
        QVERIFY(result["finished"].toBool());
        QCOMPARE(read(path), content);
        QCOMPARE(result["afterLocalCopyReads"].toInt(), 0);
        QVERIFY(result["localCopyPreserved"].toBool());
    }
    void nativeClipboardPublishesSelections() {
        QTemporaryDir root;
        write(root.filePath("jira.exe"), QByteArray(600000, 'e'));
        write(root.filePath("中文 文件.txt"), "unicode file");
        NativeOfferStore store;
        auto offer = store.publish({root.filePath("jira.exe"), root.filePath("中文 文件.txt")});
        QVERIFY(offer["ok"].toBool());
        auto result = nativeClipboardProbe(offer, [&](const QString& id, qint64 offset, int length) {
            return store.read(offer["id"].toString(), id, offset, length);
        });
        QVERIFY2(result["ok"].toBool(), QJsonDocument(result).toJson().constData());
        QVERIFY(result["owned"].toBool());
        QCOMPARE(result["urls"].toInt(), 2);
        QVERIFY(result["replaced"].toBool());
        QVERIFY(result["ownedAfterReplace"].toBool());
        QCOMPARE(store.bytesRead(), qint64(0));
    }
    void nativeClipboardRejectsStalePublication() {
        QTemporaryDir root;
        write(root.filePath("remote.txt"), "remote file");
        NativeOfferStore store;
        const auto offer = store.publish({root.filePath("remote.txt")});
        const auto result = nativeClipboardProbe(offer, [&](const QString& id, qint64 offset, int length) {
            return store.read(offer["id"].toString(), id, offset, length);
        });
        const auto cases = result["protectedCopies"].toArray();
        QCOMPARE(cases.size(), 3);
        QVERIFY(result["ownedClearRevision"].toBool());
        QVERIFY(result["unownedClearRevision"].toBool());
        QVERIFY(result["unownedClearPreserved"].toBool());
        for (const auto value : cases) {
            const auto entry = value.toObject();
            const auto diagnostic = QJsonDocument(entry).toJson(QJsonDocument::Compact);
            QVERIFY2(!entry["ownedLocalCopy"].toBool(), diagnostic.constData());
            QVERIFY2(entry["superseded"].toBool(), diagnostic.constData());
            QVERIFY2(entry["preserved"].toBool(), diagnostic.constData());
            QVERIFY2(entry["published"].toBool(), diagnostic.constData());
            QVERIFY(entry["revisionMatches"].toBool());
            QVERIFY(entry["ownedAfterPublish"].toBool());
        }
        QCOMPARE(store.bytesRead(), qint64(0));
    }
#endif
    void uploadFromSystemFileUrls() {
        QTemporaryDir remote, local;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1",server.serverPort(),server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(),10000);
        write(local.filePath("from-finder.txt"),"system file");
        client.uploadFiles({QUrl::fromLocalFile(local.filePath("from-finder.txt"))});
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),10000);
        QCOMPARE(read(remote.filePath("from-finder.txt")),QByteArray("system file"));
        client.uploadFiles({QUrl("https://example.com/not-a-local-file")});
        QVERIFY(!client.error().isEmpty());
    }
    void downloadFromHostClosingEachRequest() {
        QTemporaryDir remote, local;
        const QByteArray contents(2 * 1024 * 1024 + 23, 'e');
        write(remote.filePath("jira.exe"), contents);
        FileServer server(remote.path());
        // Match the host's framed response without a Connection header. The
        // delayed close must never turn the next chunk into a reused request.
        server.delayedClose = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.setProperty("limitMiB", 0);
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseLocal(local.path());
        client.enqueue(false, {"jira.exe"});
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(client.jobs().first().toMap()["state"].toString(), QString("done"));
        QCOMPARE(read(local.filePath("jira.exe")), contents);
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
        for (auto j : client.jobs()) QVERIFY2(j.toMap()["state"].toString() == "done", qPrintable(j.toMap()["detail"].toString()));
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
    void explicitTransferTargetsSurviveBrowsing_data() {
        QTest::addColumn<bool>("upload");
        QTest::newRow("upload") << true;
        QTest::newRow("download") << false;
    }
    void explicitTransferTargetsSurviveBrowsing() {
        QFETCH(bool, upload);
        QTemporaryDir remote, local, destination, otherLocal;
        QVERIFY(QDir(remote.path()).mkdir("source"));
        QVERIFY(QDir(remote.path()).mkdir("destination"));
        QVERIFY(QDir(remote.path()).mkdir("other"));
        write(local.filePath("chosen.txt"), "intended upload");
        write(otherLocal.filePath("chosen.txt"), "wrong local source");
        write(remote.filePath("source/chosen.txt"), "intended download");
        write(remote.filePath("other/chosen.txt"), "wrong remote source");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseLocal(otherLocal.path());
        client.browseRemote("other");
        QTRY_COMPARE_WITH_TIMEOUT(client.remotePath(), QString("other"), 10000);
        client.enqueueTo(upload, {"chosen.txt"}, upload ? local.path() : QString("source"),
            upload ? QString("destination") : destination.path());
        client.browseLocal(local.path());
        client.browseRemote("");
        const auto output = upload ? remote.filePath("destination/chosen.txt") : destination.filePath("chosen.txt");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(output), upload ? QByteArray("intended upload") : QByteArray("intended download"));
        QVERIFY(!QFileInfo::exists(remote.filePath("chosen.txt")));
        QCOMPARE(read(otherLocal.filePath("chosen.txt")), QByteArray("wrong local source"));
        QCOMPARE(read(remote.filePath("other/chosen.txt")), QByteArray("wrong remote source"));
    }
    void explicitFileUrlsUseChosenDestination() {
        QTemporaryDir remote, local;
        QVERIFY(QDir(remote.path()).mkdir("chosen"));
        QVERIFY(QDir(remote.path()).mkdir("browsed"));
        write(local.filePath("from-finder.txt"), "explicit destination");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseRemote("browsed");
        QTRY_COMPARE_WITH_TIMEOUT(client.remotePath(), QString("browsed"), 10000);
        client.uploadFilesTo({QUrl::fromLocalFile(local.filePath("from-finder.txt"))}, "chosen");
        client.browseRemote("");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(remote.filePath("chosen/from-finder.txt")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(remote.filePath("chosen/from-finder.txt")), QByteArray("explicit destination"));
        QVERIFY(!QFileInfo::exists(remote.filePath("browsed/from-finder.txt")));
        QVERIFY(!QFileInfo::exists(remote.filePath("from-finder.txt")));
    }
    void folderPickerIsIndependentAndCreatesLocalFolders() {
        QTemporaryDir remote, source, destination;
        QVERIFY(QDir(destination.path()).mkdir("existing"));
        write(destination.filePath("not-a-folder.txt"), "leave intact");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseLocal(source.path());
        const auto sourcePath = client.localPath();
        client.openFolderPicker(false, destination.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.property("folderValid").toBool() && !client.property("folderLoading").toBool(), 5000);
        QCOMPARE(client.property("folderPath").toString(), QFileInfo(destination.path()).canonicalFilePath());
        QCOMPARE(client.localPath(), sourcePath);
        const auto entries = client.property("folderEntries").toList();
        QVERIFY(!entries.isEmpty());
        for (const auto& entry : entries) QVERIFY(entry.toMap()["dir"].toBool());
        client.createPickerFolder("new folder");
        QTRY_VERIFY_WITH_TIMEOUT(QDir(destination.filePath("new folder")).exists(), 5000);
        client.browseFolderPicker(destination.filePath("existing"));
        QTRY_COMPARE_WITH_TIMEOUT(client.property("folderPath").toString(), QFileInfo(destination.filePath("existing")).canonicalFilePath(), 5000);
        QCOMPARE(client.localPath(), sourcePath);
        client.browseFolderPicker(destination.filePath("not-a-folder.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(!client.property("folderError").toString().isEmpty(), 5000);
        QVERIFY(!client.property("folderValid").toBool());
        QCOMPARE(read(destination.filePath("not-a-folder.txt")), QByteArray("leave intact"));
        QCOMPARE(client.localPath(), sourcePath);
    }
    void remoteFilesystemCapabilityAndLegacyFallback_data() {
        QTest::addColumn<bool>("filesystemAccess");
        QTest::addColumn<bool>("legacyProtocol");
        QTest::newRow("all-computer") << true << false;
        QTest::newRow("shared-folder-only") << false << false;
        QTest::newRow("legacy-host") << true << true;
    }
    void remoteFilesystemCapabilityAndLegacyFallback() {
        QFETCH(bool, filesystemAccess);
        QFETCH(bool, legacyProtocol);
        QTemporaryDir remote, outside;
        write(remote.filePath("shared.txt"), "shared content");
        write(outside.filePath("outside.txt"), "outside content");
        FileServer server(remote.path(), filesystemAccess);
        server.legacyProtocol = legacyProtocol;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        const bool allComputer = filesystemAccess && !legacyProtocol;
        QCOMPARE(client.property("remoteFilesystem").toBool(), allComputer);
        QVERIFY(!client.property("remotePlaces").toList().isEmpty());
        QVERIFY(!client.property("localPlaces").toList().isEmpty());
        if (allComputer) {
            const auto target = QFileInfo(outside.path()).canonicalFilePath();
            client.browseRemote(target);
            QTRY_COMPARE_WITH_TIMEOUT(client.remotePath(), target, 10000);
            QCOMPARE(client.remoteEntries().size(), 1);
            QCOMPARE(client.remoteEntries().first().toMap()["name"].toString(), QString("outside.txt"));
            QCOMPARE(client.property("remoteDisplayPath").toString(), target);
            QVERIFY(!client.property("remoteParentPath").toString().isEmpty());
            bool scoped = false;
            for (const auto& request : server.operations)
                if (request["op"] == "list" && request["path"] == target && request["scope"] == "computer") scoped = true;
            QVERIFY(scoped);
        } else {
            QCOMPARE(client.remotePath(), QString());
            QCOMPARE(client.remoteEntries().first().toMap()["name"].toString(), QString("shared.txt"));
            client.browseRemote(QFileInfo(outside.path()).canonicalFilePath());
            QTRY_VERIFY_WITH_TIMEOUT(!client.error().isEmpty(), 10000);
            QCOMPARE(client.remotePath(), QString());
            for (const auto& request : server.operations) QVERIFY(request["scope"] != "computer");
        }
    }
    void invalidExplicitDestinationsAreRejected_data() {
        QTest::addColumn<bool>("upload");
        QTest::addColumn<bool>("isFile");
        QTest::newRow("upload-parent-escape") << true << false;
        QTest::newRow("upload-file-as-folder") << true << true;
        QTest::newRow("download-missing-folder") << false << false;
        QTest::newRow("download-file-as-folder") << false << true;
    }
    void computerScopeTransfersOutsideSharedFolder() {
        QTemporaryDir shared, outside, local, downloaded;
        const auto remoteDestination = QFileInfo(outside.path()).canonicalFilePath();
        const QByteArray contents(300001, 'c');
        write(local.filePath("outside-share.bin"), contents);
        FileServer server(shared.path(), true); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QVERIFY(client.property("remoteFilesystem").toBool());
        client.enqueueTo(true, {"outside-share.bin"}, local.path(), remoteDestination);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(outside.filePath("outside-share.bin")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(outside.filePath("outside-share.bin")), contents);
        QVERIFY(!QFileInfo::exists(shared.filePath("outside-share.bin")));
        client.clearFinished();
        client.enqueueTo(false, {"outside-share.bin"}, remoteDestination, downloaded.path());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(downloaded.filePath("outside-share.bin")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(downloaded.filePath("outside-share.bin")), contents);
        QCOMPARE(read(local.filePath("outside-share.bin")), contents);
        for (const auto& request : server.operations)
            if (request["op"] == "read" || request["op"] == "begin") QCOMPARE(request["scope"].toString(), QString("computer"));
    }
    void remoteFolderPickerDoesNotNavigateBrowser() {
        QTemporaryDir shared, outside;
        write(shared.filePath("browser.txt"), "main browser content");
        write(outside.filePath("not-folder.txt"), "picker file");
        FileServer server(shared.path(), true); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        const auto browserPath = client.remotePath();
        const auto target = QFileInfo(outside.path()).canonicalFilePath();
        client.openFolderPicker(true, target);
        QTRY_VERIFY_WITH_TIMEOUT(client.property("folderValid").toBool() && !client.property("folderLoading").toBool(), 10000);
        QCOMPARE(client.property("folderPath").toString(), target);
        QCOMPARE(client.remotePath(), browserPath);
        QCOMPARE(client.remoteEntries().first().toMap()["name"].toString(), QString("browser.txt"));
        QVERIFY(client.property("folderEntries").toList().isEmpty());
        client.createPickerFolder("chosen destination");
        QTRY_VERIFY_WITH_TIMEOUT(QDir(outside.filePath("chosen destination")).exists(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.property("folderLoading").toBool(), 10000);
        QCOMPARE(client.remotePath(), browserPath);
        client.browseFolderPicker(target + "/not-folder.txt");
        QTRY_VERIFY_WITH_TIMEOUT(!client.property("folderError").toString().isEmpty(), 10000);
        QVERIFY(!client.property("folderValid").toBool());
        QCOMPARE(client.remotePath(), browserPath);
    }
    void invalidExplicitDestinationsAreRejected() {
        QFETCH(bool, upload);
        QFETCH(bool, isFile);
        QTemporaryDir remote, local;
        write(local.filePath("chosen.txt"), "local original");
        write(local.filePath("target.txt"), "local target original");
        write(remote.filePath("chosen.txt"), "remote original");
        write(remote.filePath("target.txt"), "remote target original");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        const auto target = upload ? (isFile ? QString("target.txt") : QString("../outside"))
            : (isFile ? local.filePath("target.txt") : local.filePath("missing"));
        QStringList errors;
        connect(&client, &FileTransfer::errorChanged, &client, [&] { errors.append(client.error()); });
        client.enqueueTo(upload, {"chosen.txt"}, upload ? local.path() : QString(), target);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QVERIFY2(!client.error().isEmpty()
            || (!client.jobs().isEmpty() && client.jobs().last().toMap()["state"] == "failed"),
            qPrintable("Error changes: " + errors.join(" -> ")));
        QCOMPARE(read(local.filePath("chosen.txt")), QByteArray("local original"));
        QCOMPARE(read(local.filePath("target.txt")), QByteArray("local target original"));
        QCOMPARE(read(remote.filePath("chosen.txt")), QByteArray("remote original"));
        QCOMPARE(read(remote.filePath("target.txt")), QByteArray("remote target original"));
        QVERIFY(!QFileInfo::exists(local.filePath("missing/chosen.txt")));
    }
    void qmlLoadsAndRenders() {
        QTemporaryDir remote, local;
        const QString folderName = QString::fromUtf8("本机项目资料与截图归档完整目录名称");
        QDir(local.path()).mkdir(folderName);
        write(local.filePath(QString::fromUtf8("项目说明.pdf")), "Preview");
        QDir(remote.path()).mkdir(QString::fromUtf8("远端设计交付材料与会议记录文件夹"));
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
            const auto output = qEnvironmentVariable("DESK_SCREENSHOT_DIR");
            for (const auto& size : {QSize(1100, 760), QSize(900, 640)}) {
                window->resize(size);
                QTest::qWait(300);
                QCOMPARE(window->size(), size);
                QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
                auto localPane = findItem(window->contentItem(), "localFilePane"); QVERIFY(localPane);
                auto remotePane = findItem(window->contentItem(), "remoteFilePane"); QVERIFY(remotePane);
                const auto localRect = localPane->mapRectToScene(localPane->boundingRect());
                const auto remoteRect = remotePane->mapRectToScene(remotePane->boundingRect());
                QVERIFY(localRect.left() >= 0 && localRect.top() >= 0);
                QVERIFY(remoteRect.right() <= window->width() && remoteRect.bottom() <= window->height());
                QVERIFY(localRect.right() <= remoteRect.left());
                auto row = findItem(localPane, "localFileRow0"); QVERIFY(row);
                auto label = findItem(row, "fileNameLabel"); QVERIFY(label);
                QCOMPARE(label->property("text").toString(), folderName);
                QVERIFY2(!label->property("truncated").toBool(), "The complete folder name must remain readable in a small window");
                QVERIFY(label->height() + 1 >= label->property("contentHeight").toDouble());
                if (!output.isEmpty()) {
                    QDir().mkpath(output);
                    const auto theme = dark ? QString("dark") : QString("light");
                    const auto image = window->grabWindow();
                    QVERIFY(image.save(QDir(output).filePath(QString("file-transfer-%1-%2x%3.png").arg(theme).arg(size.width()).arg(size.height()))));
                    if (size.width() == 1100)
                        QVERIFY(image.save(QDir(output).filePath(QString("file-transfer-%1.png").arg(theme))));
                }
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
            QVERIFY(window->setProperty("queueExpanded", true));
            QTest::qWait(150);
            const auto destinationText = QFileInfo(remote.path()).canonicalFilePath() + '/' + QString::fromUtf8("项目说明.pdf");
            std::function<QQuickItem*(QQuickItem*)> findDestination = [&](QQuickItem* item) -> QQuickItem* {
                if (item->property("text").toString().contains(destinationText)) return item;
                for (auto child : item->childItems()) if (auto found = findDestination(child)) return found;
                return nullptr;
            };
            auto destinationLabel = findDestination(window->contentItem()); QVERIFY(destinationLabel);
            QVERIFY(destinationLabel->isVisible());
            if (!output.isEmpty())
                QVERIFY(window->grabWindow().save(QDir(output).filePath(dark ? "file-transfer-queue-dark-900x640.png" : "file-transfer-queue-light-900x640.png")));
            QVERIFY(QMetaObject::invokeMethod(window, "chooseTransfer", Q_ARG(QVariant, QVariant(false)),
                Q_ARG(QVariant, QVariant(QStringList {QString::fromUtf8("项目说明.pdf")})), Q_ARG(QVariant, QVariant(client.localPath()))));
            QTRY_VERIFY_WITH_TIMEOUT(client.folderValid() && !client.folderLoading(), 10000);
            auto dialog = window->findChild<QObject*>("destinationDialog"); QVERIFY(dialog);
            QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
            for (const auto& size : {QSize(1100, 760), QSize(900, 640)}) {
                window->resize(size);
                QTest::qWait(200);
                auto confirm = findItem(window->contentItem(), "confirmTransferButton"); QVERIFY(confirm);
                QVERIFY(confirm->isVisible() && confirm->isEnabled());
                const auto bounds = confirm->mapRectToScene(confirm->boundingRect());
                QVERIFY(bounds.left() >= 0 && bounds.right() <= window->width() && bounds.bottom() <= window->height());
                if (!output.isEmpty()) {
                    const auto theme = dark ? QString("dark") : QString("light");
                    QVERIFY(window->grabWindow().save(QDir(output).filePath(
                        QString("file-transfer-destination-%1-%2x%3.png").arg(theme).arg(size.width()).arg(size.height()))));
                }
            }
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
            window->hide();
        }
        QCoreApplication::removeTranslator(&translator);
    }
    void uploadUsesOnlyCheckedFiles_data() {
        QTest::addColumn<bool>("clickSelectedRow");
        QTest::addColumn<bool>("checkboxEdge");
        QTest::newRow("uncheck-folders") << false << false;
        QTest::newRow("click-file-after-multiselection") << true << false;
        QTest::newRow("checkbox-right-edge") << false << true;
    }
    void uploadUsesOnlyCheckedFiles() {
        QFETCH(bool, clickSelectedRow);
        QFETCH(bool, checkboxEdge);
        QTemporaryDir remote, local;
        for (const auto& name : {"store", "attachments"}) {
            QVERIFY(QDir(local.path()).mkdir(name));
            write(local.filePath(QString(name) + "/unselected.txt"), "must stay local");
        }
        write(local.filePath("first.md"), "first selected file");
        write(local.filePath("second.md"), "second selected file");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1000, 800);
        QTest::qWait(250);
        std::function<QQuickItem*(QQuickItem*, const QString&)> find;
        find = [&find](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto child : parent->childItems()) if (auto item = find(child, name)) return item;
            return nullptr;
        };
        auto pane = find(window->contentItem(), "localFilePane"); QVERIFY(pane);
        auto clickCheck = [&](int index) {
            auto row = find(pane, "localFileRow" + QString::number(index));
            if (!row) return false;
            auto box = find(row, "fileCheckBox");
            if (!box) return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                box->mapToScene(QPointF(checkboxEdge ? box->width()-2 : box->width()/2, box->height()/2)).toPoint());
            return true;
        };
        for (int i = 0; i < 4; ++i) QVERIFY(clickCheck(i));
        if (clickSelectedRow) {
            auto row = find(pane, "localFileRow2"); QVERIFY(row);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                row->mapToScene(QPointF(140, row->height()/2)).toPoint());
            QVERIFY(clickCheck(3));
        } else {
            QVERIFY(clickCheck(0));
            QVERIFY(clickCheck(1));
        }
        auto selected = pane->property("selected").value<QJSValue>().toVariant().toStringList();
        QCOMPARE(selected, QStringList({"first.md", "second.md"}));
        auto upload = find(pane, "transferSelectionButton"); QVERIFY(upload);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            upload->mapToScene(QPointF(upload->width()/2, upload->height()/2)).toPoint());
        QVERIFY(confirmTransfer(window));
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(remote.filePath("first.md")), QByteArray("first selected file"));
        QCOMPARE(read(remote.filePath("second.md")), QByteArray("second selected file"));
        QVERIFY(!QFileInfo::exists(remote.filePath("store")));
        QVERIFY(!QFileInfo::exists(remote.filePath("attachments")));
        window->hide();
    }
    void enteringFolderDoesNotSelectItsContents_data() {
        QTest::addColumn<bool>("remotePane");
        QTest::newRow("local") << false;
        QTest::newRow("remote") << true;
    }
    void enteringFolderDoesNotSelectItsContents() {
        QFETCH(bool, remotePane);
        QTemporaryDir remote, local;
        const auto source = remotePane ? remote.path() : local.path();
        const QString folder = QString::fromUtf8("商店需求分析");
        QVERIFY(QDir(source).mkpath(folder + "/store"));
        write(QDir(source).filePath(folder + "/store/unselected.txt"), "must not transfer");
        write(QDir(source).filePath(folder + "/design.md"), "selected document");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1000, 800);
        QTest::qWait(250);
        std::function<QQuickItem*(QQuickItem*, const QString&)> find;
        find = [&find](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto child : parent->childItems()) if (auto item = find(child, name)) return item;
            return nullptr;
        };
        const QString prefix = remotePane ? "remote" : "local";
        auto pane = find(window->contentItem(), prefix + "FilePane"); QVERIFY(pane);
        auto row = find(pane, prefix + "FileRow0"); QVERIFY(row);
        const auto position = row->mapToScene(QPointF(140, row->height()/2)).toPoint();
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE_WITH_TIMEOUT(pane->property("currentPath").toString(),
            remotePane ? folder : QFileInfo(QDir(source).filePath(folder)).canonicalFilePath(), 10000);
        QTest::qWait(100);
        QCOMPARE(pane->property("selected").value<QJSValue>().toVariant().toStringList(), QStringList());
        auto contentRow = find(pane, prefix + "FileRow1"); QVERIFY(contentRow);
        auto checkbox = find(contentRow, "fileCheckBox"); QVERIFY(checkbox);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            checkbox->mapToScene(QPointF(checkbox->width()/2, checkbox->height()/2)).toPoint());
        QCOMPARE(pane->property("selected").value<QJSValue>().toVariant().toStringList(), QStringList({"design.md"}));
        auto button = find(pane, "transferSelectionButton"); QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        QVERIFY(confirmTransfer(window));
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        const QDir destination(remotePane ? local.path() : remote.path());
        QCOMPARE(read(destination.filePath("design.md")), QByteArray("selected document"));
        QVERIFY(!QFileInfo::exists(destination.filePath("store")));
        QVERIFY(!QFileInfo::exists(destination.filePath(folder)));
        window->hide();
    }
    void modifierSelectionAndSingleClickNavigation_data() {
        QTest::addColumn<bool>("remotePane");
        QTest::newRow("local") << false;
        QTest::newRow("remote") << true;
    }
    void modifierSelectionAndSingleClickNavigation() {
        QFETCH(bool, remotePane);
        QTemporaryDir remote, local;
        const QDir source(remotePane ? remote.path() : local.path());
        QVERIFY(QDir(source).mkdir("folder"));
        for (const auto& name : {"a.txt", "b.txt", "c.txt", "d.txt"}) write(source.filePath(name), name);
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        const auto prefix = remotePane ? QString("remote") : QString("local");
        auto pane = findItem(window->contentItem(), prefix + "FilePane"); QVERIFY(pane);
        auto clickRow = [&](int index, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            auto row = findItem(pane, prefix + "FileRow" + QString::number(index));
            if (!row) return false;
            QTest::mouseClick(window, Qt::LeftButton, modifiers,
                row->mapToScene(QPointF(140, row->height()/2)).toPoint());
            return true;
        };
        const auto originalPath = pane->property("currentPath").toString();
        QVERIFY(clickRow(0));
        QCOMPARE(pane->property("currentPath").toString(), originalPath);
        QCOMPARE(selectedNames(pane), QStringList({"folder"}));
        QVERIFY(clickRow(1));
        QCOMPARE(selectedNames(pane), QStringList({"a.txt"}));
        QVERIFY(clickRow(3, Qt::ControlModifier));
        const auto toggled = selectedNames(pane);
        QCOMPARE(QSet<QString>(toggled.cbegin(), toggled.cend()), QSet<QString>({"a.txt", "c.txt"}));
        QVERIFY(clickRow(1));
        QVERIFY(clickRow(3, Qt::ShiftModifier));
        const auto range = selectedNames(pane);
        QCOMPARE(QSet<QString>(range.cbegin(), range.cend()), QSet<QString>({"a.txt", "b.txt", "c.txt"}));
        QCOMPARE(pane->property("currentPath").toString(), originalPath);
        window->hide();
    }
    void confirmationPreservesSourceAndDestination_data() {
        QTest::addColumn<bool>("upload");
        QTest::newRow("upload") << true;
        QTest::newRow("download") << false;
    }
    void confirmationPreservesSourceAndDestination() {
        QFETCH(bool, upload);
        QTemporaryDir remote, local;
        for (const auto& name : {"source", "destination", "other"}) {
            QVERIFY(QDir(remote.path()).mkdir(name));
            QVERIFY(QDir(local.path()).mkdir(name));
        }
        write(local.filePath("source/chosen.txt"), "chosen upload");
        write(local.filePath("other/chosen.txt"), "wrong local copy");
        write(remote.filePath("source/chosen.txt"), "chosen download");
        write(remote.filePath("other/chosen.txt"), "wrong remote copy");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        client.browseLocal(local.filePath(upload ? "source" : "destination"));
        client.browseRemote(upload ? "destination" : "source");
        QTRY_COMPARE_WITH_TIMEOUT(client.remotePath(), upload ? QString("destination") : QString("source"), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        auto pane = findItem(window->contentItem(), upload ? "localFilePane" : "remoteFilePane"); QVERIFY(pane);
        auto row = findItem(pane, upload ? "localFileRow0" : "remoteFileRow0"); QVERIFY(row);
        auto box = findItem(row, "fileCheckBox"); QVERIFY(box);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            box->mapToScene(QPointF(box->width()/2, box->height()/2)).toPoint());
        auto button = findItem(pane, "transferSelectionButton"); QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(findItem(window->contentItem(), "confirmTransferButton")
            && findItem(window->contentItem(), "confirmTransferButton")->isVisible(), 5000);
        auto targetField = findItem(window->contentItem(), "folderPathField"); QVERIFY(targetField);
        const auto confirmedPath = targetField->property("text").toString();
        QVERIFY(targetField->setProperty("text", confirmedPath + "/not-yet-opened"));
        auto confirmation = findItem(window->contentItem(), "confirmTransferButton");
        QTRY_VERIFY_WITH_TIMEOUT(!confirmation->isEnabled(), 5000);
        QVERIFY(client.jobs().isEmpty());
        QVERIFY(targetField->setProperty("text", confirmedPath));
        QTRY_VERIFY_WITH_TIMEOUT(confirmation->isEnabled(), 5000);
        client.browseLocal(local.filePath("other"));
        client.browseRemote("other");
        QTRY_COMPARE_WITH_TIMEOUT(client.remotePath(), QString("other"), 10000);
        QVERIFY(confirmTransfer(window));
        const auto output = upload ? remote.filePath("destination/chosen.txt") : local.filePath("destination/chosen.txt");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(output), upload ? QByteArray("chosen upload") : QByteArray("chosen download"));
        QCOMPARE(read(local.filePath("other/chosen.txt")), QByteArray("wrong local copy"));
        QCOMPARE(read(remote.filePath("other/chosen.txt")), QByteArray("wrong remote copy"));
        window->hide();
    }
    void choosingNestedDestinationThenTransferring_data() {
        QTest::addColumn<bool>("upload");
        QTest::addColumn<bool>("filesystemAccess");
        QTest::addColumn<bool>("legacyProtocol");
        QTest::newRow("upload-shared") << true << false << false;
        QTest::newRow("download-shared") << false << false << false;
        QTest::newRow("upload-legacy") << true << false << true;
        QTest::newRow("download-legacy") << false << false << true;
        QTest::newRow("upload-computer") << true << true << false;
        QTest::newRow("download-computer") << false << true << false;
    }
    void choosingNestedDestinationThenTransferring() {
        QFETCH(bool, upload);
        QFETCH(bool, filesystemAccess);
        QFETCH(bool, legacyProtocol);
        QTemporaryDir remote, local;
        const QString firstFolder = QStringLiteral("子目录 保存");
        const QString secondFolder = QStringLiteral("二级目录");
        const QString nestedPath = firstFolder + '/' + secondFolder;
        const QString name = QStringLiteral("测试 文件.txt");
        QVERIFY(QDir(remote.path()).mkpath(nestedPath));
        QVERIFY(QDir(local.path()).mkpath(nestedPath));
        write(local.filePath(name), "local nested upload");
        write(remote.filePath(name), "remote nested download");
        FileServer server(remote.path(), filesystemAccess);
        server.legacyProtocol = legacyProtocol;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        auto pane = findItem(window->contentItem(), upload ? "localFilePane" : "remoteFilePane"); QVERIFY(pane);
        // Each source has one directory followed by the file to transfer.
        auto row = findItem(pane, upload ? "localFileRow1" : "remoteFileRow1"); QVERIFY(row);
        auto box = findItem(row, "fileCheckBox"); QVERIFY(box);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            box->mapToScene(QPointF(box->width()/2, box->height()/2)).toPoint());
        QCOMPARE(selectedNames(pane), QStringList {name});
        auto transferButton = findItem(pane, "transferSelectionButton"); QVERIFY(transferButton);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            transferButton->mapToScene(QPointF(transferButton->width()/2, transferButton->height()/2)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(client.property("folderValid").toBool()
            && !client.property("folderLoading").toBool(), 5000);
        auto list = findItem(window->contentItem(), "destinationFolderList"); QVERIFY(list);
        QTRY_VERIFY_WITH_TIMEOUT(list->isVisible(), 5000);
        QTest::qWait(200); // Let the modal opening transition finish before clicking its rows.
        QString expected = upload ? (filesystemAccess ? QFileInfo(remote.path()).canonicalFilePath() : QString())
                                  : QFileInfo(local.path()).canonicalFilePath();
        for (const auto& folder : {firstFolder, secondFolder}) {
            QTRY_COMPARE_WITH_TIMEOUT(list->property("count").toInt(), 1, 5000);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                list->mapToScene(QPointF(120, 22)).toPoint());
            expected += (expected.isEmpty() ? QString() : QString("/")) + folder;
            QTRY_COMPARE_WITH_TIMEOUT(client.property("folderPath").toString(), expected, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(client.property("folderValid").toBool()
                && !client.property("folderLoading").toBool(), 5000);
        }
        QVERIFY(confirmTransfer(window));
        QTRY_VERIFY_WITH_TIMEOUT(!client.jobs().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        const auto job = client.jobs().last().toMap();
        QVERIFY2(job["state"].toString() == "done", qPrintable(job["detail"].toString() + " / " + client.error()));
        const auto output = (upload ? remote : local).filePath(nestedPath + '/' + name);
        QCOMPARE(read(output), upload ? QByteArray("local nested upload") : QByteArray("remote nested download"));
        QCOMPARE(read(local.filePath(name)), QByteArray("local nested upload"));
        QCOMPARE(read(remote.filePath(name)), QByteArray("remote nested download"));
        window->hide();
    }
    void chooseLocalFolderFromUi() {
        QTemporaryDir remote, local, destination;
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        auto button = findItem(window->contentItem(), "localChooseFolderButton"); QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(findItem(window->contentItem(), "folderPathField")
            && findItem(window->contentItem(), "folderPathField")->isVisible(), 5000);
        auto field = findItem(window->contentItem(), "folderPathField");
        auto choose = findItem(window->contentItem(), "chooseFolderButton"); QVERIFY(choose);
        QVERIFY(field->setProperty("text", destination.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!choose->isEnabled(), 5000);
        field->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(client.property("folderPath").toString(), QFileInfo(destination.path()).canonicalFilePath(), 5000);
        QCOMPARE(client.localPath(), QFileInfo(local.path()).canonicalFilePath());
        QTRY_VERIFY_WITH_TIMEOUT(choose->isEnabled(), 5000);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            choose->mapToScene(QPointF(choose->width()/2, choose->height()/2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(client.localPath(), QFileInfo(destination.path()).canonicalFilePath(), 5000);
        window->hide();
    }
    void droppingOnFolderUsesThatDestination() {
        QTemporaryDir remote, local;
        QVERIFY(QDir(remote.path()).mkdir("destination"));
        write(local.filePath("chosen.txt"), "folder drop payload");
        FileServer server(remote.path()); QVERIFY(server.listen(QHostAddress::LocalHost));
        FileTransfer client("127.0.0.1", server.serverPort(), server.identity.localCertificate());
        client.browseLocal(local.path());
        QTRY_VERIFY_WITH_TIMEOUT(client.ready(), 10000);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("transfer", &client);
        engine.rootContext()->setContextProperty("hostName", "Test host");
        engine.rootContext()->setContextProperty("transferDark", false);
        engine.load(QUrl("qrc:/gui/FileTransferWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        auto source = findItem(window->contentItem(), "localFileRow0"); QVERIFY(source);
        auto target = findItem(window->contentItem(), "remoteFileRow0"); QVERIFY(target);
        const auto from = source->mapToScene(QPointF(140, source->height()/2)).toPoint();
        const auto to = target->mapToScene(QPointF(180, target->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) QTest::mouseMove(window, from + (to - from) * step / 10, 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(remote.filePath("destination/chosen.txt")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 10000);
        QCOMPARE(read(remote.filePath("destination/chosen.txt")), QByteArray("folder drop payload"));
        QVERIFY(!QFileInfo::exists(remote.filePath("chosen.txt")));
        QCOMPARE(client.remotePath(), QString());
        window->hide();
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
