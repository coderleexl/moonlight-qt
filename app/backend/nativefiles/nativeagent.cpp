#include "nativeagent.h"
#include "../identitymanager.h"
#include "activity.h"
#include "bridgehttp.h"
#include "clipboardbroker.h"
#include "jsontransport.h"
#include "nativeplatform.h"
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <cstdio>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif
namespace {
bool parentAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    auto process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process)
        return false;
    bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0;
#endif
}
QString lockName()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath("desk-clipboard-"
            + QString::fromLatin1(
                QCryptographicHash::hash(QSettings().fileName().toUtf8(), QCryptographicHash::Sha256).toHex().left(24))
            + ".lock");
}
}
void NativeAgent::stop(QProcess& process)
{
    if (process.state() == QProcess::NotRunning)
        return;
    process.closeWriteChannel();
    if (!process.waitForFinished(1500)) {
        process.terminate();
        if (!process.waitForFinished(1000))
            process.kill();
    }
}
bool NativeAgent::startHost(QProcess& process, QProcessEnvironment& environment)
{
    const auto secret
        = (QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces))
              .toLatin1();
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("DESK_BRIDGE_TOKEN", QString::fromLatin1(secret));
    process.setProcessEnvironment(env);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({ "--desk-clipboard-host", QString::number(QCoreApplication::applicationPid()) });
    process.start();
    if (!process.waitForStarted(3000))
        return false;
    QByteArray line;
    QElapsedTimer timeout;
    timeout.start();
    QJsonObject ready;
    while (!ready["port"].toInt() && timeout.elapsed() < 7000 && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        line += process.readAllStandardOutput();
        while (line.contains('\n')) {
            auto end = line.indexOf('\n');
            auto object = QJsonDocument::fromJson(line.left(end)).object();
            line.remove(0, end + 1);
            if (object["port"].toInt())
                ready = object;
        }
        if (line.size() > 65536)
            break;
    }
    if (!ready["port"].toInt()) {
        stop(process);
        return false;
    }
    environment.insert("DESK_CLIPBOARD_PORT", QString::number(ready["port"].toInt()));
    environment.insert("DESK_CLIPBOARD_TOKEN", QString::fromLatin1(secret));
    return true;
}
void NativeAgent::startClient(QProcess& process, const QString& host, quint16 port, const QSslCertificate& certificate)
{
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({ "--desk-clipboard-client", QString::number(QCoreApplication::applicationPid()), host,
        QString::number(port), QString::fromLatin1(certificate.toDer().toBase64()) });
    process.start();
    process.waitForStarted(2000);
}
int NativeAgent::run(const QStringList& args)
{
    if (args.size() < 3)
        return 2;
    auto platform = NativeFilePlatform::create();
    if (!platform || !platform->supported())
        return 3;
    platform->setAgentMode();
    QGuiApplication::setQuitOnLastWindowClosed(false);
    // Parent owns stdin: EOF is a graceful stop even while the parent's Qt loop is suspended.
    std::thread([] {
        while (std::getchar() != EOF) { }
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    }).detach();
    const auto parent = args[2].toLongLong();
    if (parent <= 0)
        return 2;
    QTimer parentTimer;
    parentTimer.setInterval(1000);
    QObject::connect(&parentTimer, &QTimer::timeout, [parent] {
        if (!parentAlive(parent))
            QCoreApplication::quit();
    });
    parentTimer.start();
    QLockFile clipboardLock(lockName());
    auto clipboard = QGuiApplication::clipboard();
    if (args[1] == "--desk-clipboard-host") {
        auto secret = qgetenv("DESK_BRIDGE_TOKEN");
        qunsetenv("DESK_BRIDGE_TOKEN");
        if (secret.size() < 64)
            return 2;
        ClipboardBroker broker;
        BridgeHttp server(secret);
        if (!server.listen(QHostAddress::LocalHost, 0))
            return 3;
        NativeTransport local;
        local.url = QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/");
        local.bearer = secret;
        QObject::connect(&broker, &ClipboardBroker::sessionChanged, [&](bool active) {
            if (!active) {
                platform->revoke();
                clipboardLock.unlock();
            }
        });
        QObject::connect(clipboard, &QClipboard::dataChanged, [&] {
            if (!platform->ownsClipboard())
                broker.publish(platform->copiedFiles());
        });
        QObject::connect(&broker, &ClipboardBroker::remoteChanged, [&](QJsonObject offer) {
            if (offer.isEmpty()) {
                platform->revoke();
                return;
            }
            platform->publish(
                offer, [local, id = offer["id"].toString()](const QString& file, qint64 offset, int length) {
                    return local.call({ { "op", "client-read" }, { "offer", id }, { "file", file },
                        { "offset", offset }, { "length", length } });
                });
        });
        server.handler = [&](QJsonObject request, BridgeHttp::Reply reply) {
            if (request["op"] == "client-read") {
                broker.requestClient(request["offer"].toString(), request["file"].toString(),
                    request["offset"].toVariant().toLongLong(), request["length"].toInt(), reply);
                return;
            }
            const auto peer = request.take("peer").toString();
            if (request["op"] == "open" && broker.owner().isEmpty() && !clipboardLock.tryLock(0)) {
                reply({ { "ok", false }, { "error", "Clipboard is in use by another Desk session" } });
                return;
            }
            broker.execute(peer, request, reply);
        };
        std::printf("{\"port\":%u}\n", server.serverPort());
        std::fflush(stdout);
        return QCoreApplication::exec();
    }
    if (args[1] != "--desk-clipboard-client" || args.size() != 6 || !clipboardLock.tryLock(0))
        return 3;
    NativeTransport remote;
    remote.url = QUrl();
    remote.url.setScheme("https");
    remote.url.setHost(args[3]);
    remote.url.setPort(args[4].toUShort());
    remote.url.setPath("/desk/clipboard");
    remote.peer = QSslCertificate(QByteArray::fromBase64(args[5].toLatin1()), QSsl::Der);
    remote.identity = IdentityManager::get()->getSslConfig();
    auto filesUrl = remote.url;
    filesUrl.setPath("/desk/files");
    NativeActivity activity(NativeActivity::peerKey(filesUrl, remote.peer.toDer()));
    QObject::connect(
        platform.get(), &NativeFilePlatform::error, &activity, [&](QString message) { activity.notice(message); });
    NativeRead outgoingRead;
    QObject context;
    NativeOfferStore local;
    QString lease, remoteId;
    QJsonObject outgoing;
    bool changed = false, inflight = false;
    QJsonArray responses;
    QObject::connect(clipboard, &QClipboard::dataChanged, &context, [&] {
        if (lease.isEmpty() || platform->ownsClipboard())
            return;
        auto files = platform->copiedFiles();
        outgoing = files.isEmpty() ? QJsonObject() : local.publish(files);
        if (!outgoing["ok"].toBool()) {
            if (!files.isEmpty())
                activity.notice(outgoing["error"].toString());
            outgoing = {};
            local.clear();
        }
        outgoingRead = activity.track(
            outgoing,
            [&local, id = outgoing["id"].toString()](
                const QString& file, qint64 offset, int length) { return local.read(id, file, offset, length); },
            true);
        changed = true;
    });
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &context, [&] {
        if (inflight || lease.isEmpty())
            return;
        inflight = true;
        QJsonObject request { { "op", "exchange" }, { "lease", lease }, { "responses", responses } };
        responses = {};
        if (changed && request["responses"].toArray().isEmpty()) {
            request["offer"] = outgoing;
            changed = false;
        }
        remote.send(&context, request, [&](QJsonObject result) {
            inflight = false;
            if (!result["ok"].toBool()) {
                activity.notice(result["error"].toString());
                platform->revoke();
                QCoreApplication::quit();
                return;
            }
            auto offer = result["offer"].toObject();
            auto id = offer["id"].toString();
            if (id != remoteId) {
                remoteId = id;
                if (offer.isEmpty())
                    platform->revoke();
                else if (!NativeOfferStore::validManifest(offer)
                    || !platform->publish(offer,
                        activity.track(
                            offer,
                            [remote, lease, id](const QString& file, qint64 offset, int length) {
                                return remote.call({ { "op", "read" }, { "lease", lease }, { "offer", id },
                                    { "file", file }, { "offset", offset }, { "length", length } });
                            },
                            false))) {
                    platform->revoke();
                    activity.notice("Cannot publish the remote file selection to the system clipboard");
                }
            }
            for (auto v : result["requests"].toArray()) {
                const auto r = v.toObject();
                auto answer = outgoingRead && r["offer"] == outgoing["id"]
                    ? outgoingRead(r["file"].toString(), r["offset"].toVariant().toLongLong(), r["length"].toInt())
                    : QJsonObject { { "ok", false }, { "error", "Selection expired" } };
                answer["request"] = r["request"];
                responses.append(answer);
            }
        });
    });
    remote.send(&context, { { "op", "open" } }, [&](QJsonObject result) {
        lease = result["lease"].toString();
        if (lease.isEmpty()) {
            activity.notice(result["error"].toString());
            QCoreApplication::quit();
        } else
            poll.start();
    });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [&] {
        platform->revoke();
        local.clear();
        if (!lease.isEmpty()) {
            auto closing = remote;
            closing.timeoutMs = 1000;
            closing.call({ { "op", "close" }, { "lease", lease } });
        }
    });
    return QCoreApplication::exec();
}
