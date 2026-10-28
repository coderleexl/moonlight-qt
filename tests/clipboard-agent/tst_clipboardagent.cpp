#include "fakestate.h"
#include "backend/identitymanager.h"
#include "backend/nativefiles/nativeagent.h"
#include "backend/nativefiles/jsontransport.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <utility>

namespace {
void isolatedSettings(const QString& directory)
{
    QCoreApplication::setOrganizationName("DeskClipboardAgentTest");
    QCoreApplication::setApplicationName("isolated");
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
}

QJsonObject remoteOffer(const QString& id)
{
    return { { "ok", true }, { "id", id },
        { "entries", QJsonArray { QJsonObject {
            { "id", "file" }, { "path", "remote.txt" }, { "size", 4 }, { "directory", false } } } } };
}

class ClipboardServer final : public QTcpServer {
public:
    using Reply = std::function<void(QJsonObject)>;
    QSslConfiguration identity = IdentityManager::get()->getSslConfig();
    QJsonObject offer;
    QList<QJsonObject> outgoingOffers;
    int opened = 0, exchanges = 0;
    bool holdOpen = false, holdNextExchange = false;
    Reply pendingOpen, pendingExchange;

    void answer(QJsonObject request, Reply reply)
    {
        if (request["op"] == "open") {
            ++opened;
            const QJsonObject result { { "ok", true }, { "lease", "test-lease" }, { "offer", offer } };
            if (holdOpen)
                pendingOpen = [reply, result](QJsonObject) { reply(result); };
            else
                reply(result);
        } else if (request["op"] == "exchange") {
            ++exchanges;
            if (request.contains("offer"))
                outgoingOffers.append(request["offer"].toObject());
            const QJsonObject result { { "ok", true }, { "offer", offer }, { "requests", QJsonArray() } };
            if (holdNextExchange) {
                holdNextExchange = false;
                pendingExchange = [reply, result](QJsonObject) { reply(result); };
            } else
                reply(result);
        } else
            reply({ { "ok", true } });
    }
    void releaseOpen()
    {
        auto reply = std::exchange(pendingOpen, {});
        reply({});
    }
    void releaseExchange()
    {
        auto reply = std::exchange(pendingExchange, {});
        reply({});
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto socket = new QSslSocket(this);
        socket->setSslConfiguration(identity);
        socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QSslSocket::sslErrors, socket, [this, socket](const QList<QSslError>& errors) {
            if (socket->peerCertificate() == identity.localCertificate())
                socket->ignoreSslErrors(errors);
        });
        auto buffer = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        connect(socket, &QSslSocket::readyRead, socket, [this, socket, buffer, handled] {
            if (*handled)
                return;
            buffer->append(socket->readAll());
            const auto split = buffer->indexOf("\r\n\r\n");
            if (split < 0)
                return;
            int length = -1;
            for (const auto& line : buffer->left(split).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toInt();
            if (length < 0 || buffer->size() < split + 4 + length)
                return;
            *handled = true;
            QPointer<QSslSocket> guard(socket);
            answer(QJsonDocument::fromJson(buffer->mid(split + 4, length)).object(), [guard](QJsonObject result) {
                if (!guard)
                    return;
                const auto body = QJsonDocument(result).toJson(QJsonDocument::Compact);
                guard->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                guard->disconnectFromHost();
            });
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};

class AgentProcess final : public QProcess {
public:
    ~AgentProcess() override
    {
        closeWriteChannel();
        QElapsedTimer timer;
        timer.start();
        while (state() != NotRunning && timer.elapsed() < 3000)
            QTest::qWait(10); // The parent TLS server must answer the child's close request.
        if (state() != NotRunning) {
            kill();
            waitForFinished(1000);
        }
    }
};
}

class ClipboardAgentTest final : public QObject {
    Q_OBJECT
    QTemporaryDir settings;

    void start(AgentProcess& child, ClipboardServer& server, const QString& statePath)
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("DESK_TEST_SETTINGS", settings.path());
        environment.insert("DESK_TEST_CLIPBOARD_STATE", statePath);
        child.setProcessEnvironment(environment);
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({ "--desk-clipboard-client", QString::number(QCoreApplication::applicationPid()),
            "127.0.0.1", QString::number(server.serverPort()),
            QString::fromLatin1(server.identity.localCertificate().toDer().toBase64()) });
        child.start();
        QVERIFY(child.waitForStarted(3000));
    }
    static qint64 localCopy(const QString& path, const QString& kind, const QString& file = {})
    {
        const auto state = changeFakeClipboard(path, [&](QJsonObject& state) {
            state["revision"] = state["revision"].toVariant().toLongLong() + 1;
            state["kind"] = kind;
            state["owned"] = false;
            state["files"] = file.isEmpty() ? QJsonArray() : QJsonArray { file };
        });
        return state["revision"].toVariant().toLongLong();
    }
    static void contentKinds()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("image-with-cached-path") << QString("image");
        QTest::newRow("explicit-file") << QString("file");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(settings.isValid());
        isolatedSettings(settings.path());
        QVERIFY(QSslSocket::supportsSsl());
        IdentityManager::get();
        QSettings().sync();
    }
    void localCopyDuringDelayedOpen_data() { contentKinds(); }
    void localCopyDuringDelayedOpen()
    {
        QFETCH(QString, kind);
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        const auto filePath = temporary.filePath("copied.png");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test content");
        file.close();
        localCopy(statePath, "empty");
        ClipboardServer server;
        server.holdOpen = true;
        server.offer = remoteOffer("stale-during-open");
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_COMPARE_WITH_TIMEOUT(server.opened, 1, 5000);
        const auto revision = localCopy(statePath, kind, filePath);
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["observedRevision"].toVariant().toLongLong(), revision, 3000);
        server.releaseOpen();
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= 3, 5000);
        QCOMPARE(child.state(), QProcess::Running);
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), kind);
        QVERIFY(readFakeClipboard(statePath)["publications"].toArray().isEmpty());
        QVERIFY(!server.outgoingOffers.isEmpty());
        const auto outgoing = server.outgoingOffers.first();
        if (kind == "image")
            QVERIFY(outgoing.isEmpty());
        else
            QCOMPARE(outgoing["entries"].toArray().first().toObject()["path"].toString(), QString("copied.png"));
    }
    void startupClipboardIsPreserved_data() { contentKinds(); }
    void startupClipboardIsPreserved()
    {
        QFETCH(QString, kind);
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        const auto filePath = temporary.filePath("old-copy.png");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("pre-existing clipboard content");
        file.close();
        localCopy(statePath, kind, filePath);
        ClipboardServer server;
        server.offer = remoteOffer("pre-existing-remote");
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= 3, 5000);
        QCOMPARE(child.state(), QProcess::Running);
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), kind);
        QVERIFY(readFakeClipboard(statePath)["publications"].toArray().isEmpty());
        for (const auto& outgoing : server.outgoingOffers)
            QVERIFY2(outgoing.isEmpty(), "Startup must not export clipboard files copied before this session");
        server.offer = remoteOffer("new-remote-copy");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 1, 5000);
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().first().toString(), QString("new-remote-copy"));
    }
    void lateExchangePreservesImageAndSuppressesSameOffer()
    {
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        localCopy(statePath, "empty");
        ClipboardServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= 2, 5000);
        server.offer = remoteOffer("previous-remote");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 1, 5000);
        server.holdNextExchange = true;
        server.offer = remoteOffer("delayed-remote");
        QTRY_VERIFY_WITH_TIMEOUT(bool(server.pendingExchange), 5000);
        localCopy(statePath, "image");
        server.releaseExchange(); // Deliberately race the 200 ms clipboard watcher.
        const auto exchanges = server.exchanges;
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= exchanges + 3, 5000);
        QCOMPARE(child.state(), QProcess::Running);
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), QString("image"));
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().size(), 1);
        QVERIFY(!server.outgoingOffers.isEmpty());
        QVERIFY(server.outgoingOffers.last().isEmpty());
        server.offer = remoteOffer("subsequent-remote");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 2, 5000);
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().last().toString(), QString("subsequent-remote"));
    }
    void firstRemoteCopyAfterOpenPublishesImmediately()
    {
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        localCopy(statePath, "image");
        ClipboardServer server;
        server.offer = remoteOffer("before-session");
        server.holdOpen = true;
        server.holdNextExchange = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_VERIFY_WITH_TIMEOUT(bool(server.pendingOpen), 5000);
        server.releaseOpen(); // The open reply contains the older selection.
        server.offer = remoteOffer("first-copy-after-open");
        QTRY_VERIFY_WITH_TIMEOUT(bool(server.pendingExchange), 5000);
        QCOMPARE(server.exchanges, 1);
        QVERIFY(readFakeClipboard(statePath)["publications"].toArray().isEmpty());
        server.releaseExchange();
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 1, 5000);
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().first().toString(), QString("first-copy-after-open"));
        QCOMPARE(child.state(), QProcess::Running);
    }
    void copyAfterNativeWriteIsStillObserved()
    {
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        localCopy(statePath, "empty");
        ClipboardServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= 2, 5000);
        changeFakeClipboard(statePath, [](QJsonObject& state) { state["injectImageAfterPublication"] = true; });
        server.offer = remoteOffer("publication-race");
        QTRY_VERIFY_WITH_TIMEOUT(readFakeClipboard(statePath).contains("injectedRevision"), 5000);
        const auto revision = readFakeClipboard(statePath)["injectedRevision"].toVariant().toLongLong();
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["inspectedRevision"].toVariant().toLongLong(), revision, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!server.outgoingOffers.isEmpty(), 5000);
        QVERIFY(server.outgoingOffers.last().isEmpty());
        const auto exchanges = server.exchanges;
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= exchanges + 2, 5000);
        QCOMPARE(child.state(), QProcess::Running);
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), QString("image"));
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().size(), 1);
        server.offer = remoteOffer("after-publication-race");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 2, 5000);
    }
    void remoteClearDoesNotSuppressNextRemoteCopy()
    {
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        localCopy(statePath, "empty");
        ClipboardServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        AgentProcess child;
        start(child, server, statePath);
        QTRY_VERIFY_WITH_TIMEOUT(server.exchanges >= 2, 5000);
        server.offer = remoteOffer("remote-a");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 1, 5000);
        server.offer = {};
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["kind"].toString(), QString("empty"), 5000);
        server.offer = remoteOffer("remote-b");
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 2, 5000);
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().last().toString(), QString("remote-b"));
        QVERIFY2(server.outgoingOffers.isEmpty(), "Our own clear must not be treated as a new local copy");
        QCOMPARE(child.state(), QProcess::Running);
    }
    void hostRevisionOnlyCopyWinsUntilAcknowledged()
    {
        QTemporaryDir temporary;
        const auto statePath = temporary.filePath("clipboard.json");
        localCopy(statePath, "empty");
        const QByteArray bearer(64, 't');
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("DESK_TEST_SETTINGS", settings.path());
        environment.insert("DESK_TEST_CLIPBOARD_STATE", statePath);
        environment.insert("DESK_BRIDGE_TOKEN", QString::fromLatin1(bearer));
        AgentProcess child;
        child.setProcessEnvironment(environment);
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({ "--desk-clipboard-host", QString::number(QCoreApplication::applicationPid()) });
        child.start();
        QVERIFY(child.waitForStarted(3000));
        QByteArray ready;
        QTRY_VERIFY_WITH_TIMEOUT((ready += child.readAllStandardOutput()).contains('\n'), 5000);
        const auto port = QJsonDocument::fromJson(ready.left(ready.indexOf('\n'))).object()["port"].toInt();
        QVERIFY(port > 0);
        NativeTransport transport;
        transport.url = QUrl(QString("http://127.0.0.1:%1/").arg(port));
        transport.bearer = bearer;
        const auto opened = transport.call({ { "op", "open" }, { "peer", "test-peer" } });
        QVERIFY(opened["ok"].toBool());
        const auto lease = opened["lease"];
        auto exchange = [&](const QString& id) {
            return transport.call({ { "op", "exchange" }, { "peer", "test-peer" }, { "lease", lease },
                { "offer", remoteOffer(id) }, { "responses", QJsonArray() } });
        };
        QVERIFY(exchange("host-previous-remote")["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 1, 3000);
        const auto revision = localCopy(statePath, "image");
        // No Qt clipboard signal is emitted by the fake. The incoming exchange
        // must observe the revision before publishing its conflicting offer.
        const auto conflicted = exchange("host-conflicting-remote");
        QVERIFY(conflicted["ok"].toBool());
        QVERIFY(conflicted["offer"].toObject().isEmpty());
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), QString("image"));
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["inspectedRevision"].toVariant().toLongLong(), revision, 3000);
        const auto acknowledged = exchange("host-conflicting-remote");
        QVERIFY(acknowledged["ok"].toBool());
        QVERIFY(acknowledged["offer"].toObject().isEmpty());
        QVERIFY(exchange("host-conflicting-remote")["ok"].toBool());
        QCOMPARE(readFakeClipboard(statePath)["kind"].toString(), QString("image"));
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().size(), 1);
        QVERIFY(exchange("host-new-remote")["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(readFakeClipboard(statePath)["publications"].toArray().size(), 2, 3000);
        QCOMPARE(readFakeClipboard(statePath)["publications"].toArray().last().toString(), QString("host-new-remote"));
        QCOMPARE(child.state(), QProcess::Running);
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    if (application.arguments().value(1).startsWith("--desk-clipboard-")) {
        isolatedSettings(qEnvironmentVariable("DESK_TEST_SETTINGS"));
        return NativeAgent::run(application.arguments());
    }
    ClipboardAgentTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_clipboardagent.moc"
