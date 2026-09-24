#include "backend/nativefiles/activity.h"
#include "backend/nativefiles/bridgehttp.h"
#include "backend/nativefiles/clipboardbroker.h"
#include "backend/nativefiles/jsontransport.h"
#include "backend/nativefiles/materialize.h"
#include "backend/nativefiles/offerstore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class NativeFilesTest : public QObject {
    Q_OBJECT
    static void write(const QString& name, const QByteArray& bytes)
    {
        QFile f(name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }
private slots:
    void activityProgressAndCancellation()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir root;
        write(root.filePath("file"), "hello");
        NativeOfferStore store;
        auto offer = store.publish({ root.filePath("file") });
        auto id = offer["entries"].toArray().first().toObject()["id"].toString();
        QString key = "test-" + offer["id"].toString();
        NativeActivity activity(key);
        auto read = activity.track(
            offer,
            [&](QString file, qint64 offset, int length) {
                return store.read(offer["id"].toString(), file, offset, length);
            },
            false);
        auto snapshot = [&] {
            auto paths = QDir(NativeActivity::directory()).entryList({ key + "-*.json" });
            if (paths.isEmpty())
                return QJsonObject();
            QFile f(QDir(NativeActivity::directory()).filePath(paths.first()));
            f.open(QIODevice::ReadOnly);
            return QJsonDocument::fromJson(f.readAll()).object();
        };
        QVERIFY(read(id, 0, 5)["ok"].toBool());
        QTRY_VERIFY(!snapshot().isEmpty());
        QCOMPARE(snapshot()["jobs"].toArray().first().toObject()["state"].toString(), QString("done"));
        QVERIFY(read(id, 0, 2)["ok"].toBool());
        QCoreApplication::processEvents();
        auto job = snapshot()["jobs"].toArray().first().toObject();
        QCOMPARE(job["state"].toString(), QString("running"));
        QCOMPARE(job["done"].toInt(), 2);
        auto paths = QDir(NativeActivity::directory()).entryList({ key + "-*.json" });
        auto path = QDir(NativeActivity::directory()).filePath(paths.first());
        write(path + ".cancel",
            QJsonDocument(QJsonObject { { "session", snapshot()["session"] }, { "id", id } }).toJson());
        // Wait for observable cancellation; macOS may coalesce the 200 ms timer.
        QTRY_COMPARE_WITH_TIMEOUT(snapshot()["jobs"].toArray().first().toObject()["state"].toString(),
            QString("cancelled"), 3000);
        QVERIFY(!read(id, 2, 3)["ok"].toBool());
        QFile::remove(path);
    }
    void manifestIdentifiersAndDestinationLinks()
    {
        QTemporaryDir root, target, outside;
        write(root.filePath("a"), "hello");
        NativeOfferStore store;
        auto offer = store.publish({ root.filePath("a") });
        auto forged = offer;
        forged["id"] = "../escape";
        QVERIFY(!NativeOfferStore::validManifest(forged));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(outside.path(), target.filePath("link")));
        auto read = [&](QString file, qint64 offset, int length) {
            return store.read(offer["id"].toString(), file, offset, length);
        };
        QVERIFY(!materializeNative(offer, read, "a", target.filePath("link/escaped")).isEmpty());
        QVERIFY(!QFileInfo::exists(outside.filePath("escaped")));
#endif
    }
    void leaseIsolationAndReverseReads()
    {
        ClipboardBroker broker;
        auto call = [&](QString peer, QJsonObject request) {
            QJsonObject result;
            broker.execute(peer, request, [&](QJsonObject r) { result = r; });
            return result;
        };
        auto opened = call("peer-a", { { "op", "open" } });
        QVERIFY(opened["ok"].toBool());
        QString lease = opened["lease"].toString();
        QVERIFY(!call("peer-b", { { "op", "open" } })["ok"].toBool());
        QVERIFY(!call("peer-b", { { "op", "exchange" }, { "lease", lease } })["ok"].toBool());
        QTemporaryDir root;
        write(root.filePath("a"), "hello");
        NativeOfferStore store;
        auto offer = store.publish({ root.filePath("a") });
        auto id = offer["entries"].toArray().first().toObject()["id"].toString();
        QVERIFY(call("peer-a", { { "op", "exchange" }, { "lease", lease }, { "offer", offer } })["ok"].toBool());
        QJsonObject reply;
        broker.requestClient(offer["id"].toString(), id, 0, 5, [&](QJsonObject r) { reply = r; });
        QVERIFY(reply.isEmpty());
        auto poll = call("peer-a", { { "op", "exchange" }, { "lease", lease } });
        auto requests = poll["requests"].toArray();
        QCOMPARE(requests.size(), 1);
        auto read = requests.first().toObject();
        auto result = store.read(read["offer"].toString(), read["file"].toString(),
            read["offset"].toVariant().toLongLong(), read["length"].toInt());
        result["request"] = read["request"];
        call("peer-a", { { "op", "exchange" }, { "lease", lease }, { "responses", QJsonArray { result } } });
        QCOMPARE(QByteArray::fromBase64(reply["data"].toString().toLatin1()), QByteArray("hello"));
        reply = {};
        broker.requestClient(offer["id"].toString(), id, 0, 5, [&](QJsonObject r) { reply = r; });
        call("peer-a", { { "op", "close" }, { "lease", lease } });
        QVERIFY(!reply["ok"].toBool());
        QVERIFY(!broker.remoteOffer()["ok"].toBool());
        QVERIFY(call("peer-b", { { "op", "open" } })["ok"].toBool());
    }
    void expiredLeaseAndForgedManifest()
    {
        ClipboardBroker broker(nullptr, 5);
        QJsonObject opened;
        broker.execute("peer", { { "op", "open" } }, [&](QJsonObject r) { opened = r; });
        QTest::qWait(10);
        broker.expire();
        QVERIFY(broker.owner().isEmpty());
        QJsonObject result;
        broker.execute(
            "peer", { { "op", "exchange" }, { "lease", opened["lease"] } }, [&](QJsonObject r) { result = r; });
        QVERIFY(!result["ok"].toBool());
        QJsonObject offer { { "id", "offer" },
            { "entries",
                QJsonArray { QJsonObject {
                    { "id", "f" }, { "path", "../outside" }, { "size", 0 }, { "directory", false } } } } };
        QVERIFY(!NativeOfferStore::validManifest(offer));
        offer["entries"] = QJsonArray { QJsonObject {
            { "id", "f" }, { "path", "parent/child" }, { "size", 0 }, { "directory", false } } };
        QVERIFY(!NativeOfferStore::validManifest(offer));
    }
    void authenticatedLoopbackBridge()
    {
        BridgeHttp bridge("test-only-unguessable-secret");
        int requests = 0;
        bridge.handler = [&](QJsonObject request, BridgeHttp::Reply reply) {
            ++requests;
            reply({ { "ok", true }, { "echo", request["value"] } });
        };
        QVERIFY(bridge.listen(QHostAddress::LocalHost));
        NativeTransport transport;
        transport.url = QUrl("http://127.0.0.1:" + QString::number(bridge.serverPort()) + "/");
        transport.bearer = "wrong";
        QVERIFY(!transport.call({ { "value", 42 } })["ok"].toBool());
        QCOMPARE(requests, 0);
        transport.bearer = "test-only-unguessable-secret";
        auto result = transport.call({ { "value", 42 } });
        QVERIFY(result["ok"].toBool());
        QCOMPARE(result["echo"].toInt(), 42);
        QCOMPARE(requests, 1);
    }
    void materializeSelectionAndCancel()
    {
        QTemporaryDir root, target;
        QDir(root.path()).mkpath("tree/empty");
        write(root.filePath("tree/file"), QByteArray(800000, 'z'));
        NativeOfferStore store;
        auto offer = store.publish({ root.filePath("tree") });
        auto read = [&](const QString& id, qint64 offset, int length) {
            return store.read(offer["id"].toString(), id, offset, length);
        };
        QVERIFY(materializeNative(offer, read, "tree", target.filePath("renamed")).isEmpty());
        QCOMPARE(QFileInfo(target.filePath("renamed/file")).size(), qint64(800000));
        QVERIFY(QDir(target.filePath("renamed/empty")).exists());
        QVERIFY(!materializeNative(offer, read, "tree", target.filePath("renamed")).isEmpty());
        auto cancelled = [&](const QString& id, qint64 offset, int length) {
            if (offset)
                store.clear();
            return read(id, offset, length);
        };
        QVERIFY(!materializeNative(offer, cancelled, "tree", target.filePath("cancelled")).isEmpty());
        QVERIFY(!QFileInfo::exists(target.filePath("cancelled/file")));
        QVERIFY(QFileInfo::exists(root.filePath("tree/file")));
    }
    void selectionAndLazyRead()
    {
        QTemporaryDir root;
        QDir(root.path()).mkpath("folder/empty");
        write(root.filePath("folder/中文.txt"), QByteArray(700000, 'x'));
        write(root.filePath("private.txt"), "not selected");
        NativeOfferStore store;
        auto offer = store.publish({ root.filePath("folder") });
        QVERIFY(offer.value("ok").toBool());
        auto entries = offer.value("entries").toArray();
        QCOMPARE(entries.size(), 3);
        QVERIFY(!QJsonDocument(offer).toJson().contains(root.path().toUtf8()));
        QCOMPARE(store.bytesRead(), qint64(0));
        QJsonObject file;
        for (auto e : entries)
            if (!e.toObject()["directory"].toBool())
                file = e.toObject();
        const auto id = offer["id"].toString(), fileId = file["id"].toString();
        auto chunk = store.read(id, fileId, 123, 65536);
        QVERIFY(chunk["ok"].toBool());
        QCOMPARE(QByteArray::fromBase64(chunk["data"].toString().toLatin1()), QByteArray(65536, 'x'));
        QVERIFY(!store.read(id, "../private.txt", 0, 10)["ok"].toBool());
        QVERIFY(!store.read("wrong", fileId, 0, 10)["ok"].toBool());
        QVERIFY(!store.read(id, fileId, -1, 10)["ok"].toBool());
        QVERIFY(!store.read(id, fileId, 0, 262145)["ok"].toBool());
        QVERIFY(!store.read(id, fileId, 700001, 10)["ok"].toBool());
    }
    void replacementRevokesAndSourceChangesFail()
    {
        QTemporaryDir root;
        write(root.filePath("a"), "hello");
        NativeOfferStore store;
        auto first = store.publish({ root.filePath("a") });
        auto file = first["entries"].toArray().first().toObject()["id"].toString();
        auto next = store.publish({ root.filePath("a") });
        QVERIFY(first["id"] != next["id"]);
        QVERIFY(!store.read(first["id"].toString(), file, 0, 5)["ok"].toBool());
        write(root.filePath("a"), "changed");
        auto newFile = next["entries"].toArray().first().toObject()["id"].toString();
        QVERIFY(!store.read(next["id"].toString(), newFile, 0, 5)["ok"].toBool());
        store.clear();
        QVERIFY(!store.manifest()["ok"].toBool());
    }
    void linksAndCollisionsRejected()
    {
        QTemporaryDir root;
        write(root.filePath("secret"), "secret");
        NativeOfferStore store;
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(root.filePath("secret"), root.filePath("link")));
        QVERIFY(!store.publish({ root.filePath("link") })["ok"].toBool());
#endif
        QDir(root.path()).mkpath("a");
        QDir(root.path()).mkpath("b");
        write(root.filePath("a/same"), "a");
        write(root.filePath("b/same"), "b");
        QVERIFY(!store.publish({ root.filePath("a/same"), root.filePath("b/same") })["ok"].toBool());
        QVERIFY(!store.publish({})["ok"].toBool());
    }
};
QTEST_GUILESS_MAIN(NativeFilesTest)
#include "tst_nativefiles.moc"
