#include "nativeagent.h"
#include "../identitymanager.h"
#include "activity.h"
#include "bridgehttp.h"
#include "clipboardbroker.h"
#include "clipboardpublication.h"
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
#include <QDebug>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
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
    if (!process.waitForStarted(2000))
        qWarning() << "File clipboard helper failed to start:" << process.errorString();
    else
        qInfo() << "File clipboard helper started:" << process.processId();
}
int NativeAgent::run(const QStringList& args)
{
    if (args.size() < 3)
        return 2;
    auto platform = NativeFilePlatform::create();
    if (!platform || !platform->supported()) {
        qWarning() << "File clipboard is unavailable on this desktop platform";
        return 3;
    }
    platform->setAgentMode();
    QGuiApplication::setQuitOnLastWindowClosed(false);
    // Parent owns stdin: EOF is a graceful stop even while the parent's Qt loop is suspended.
    std::thread([] {
        // Do not hold a stdio FILE lock while waiting: it can deadlock normal exit.
        char buffer[64];
#ifdef Q_OS_WIN
        DWORD count;
        while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &count, nullptr) && count != 0) { }
#else
        for (;;) {
            const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count > 0 || (count < 0 && errno == EINTR))
                continue;
            break;
        }
#endif
        qInfo() << "File clipboard helper stopping: parent channel closed";
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
        // Parent death must also stop a helper whose GUI/network loop is stuck.
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::_Exit(EXIT_SUCCESS);
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
        ClipboardPublication publication;
        QElapsedTimer publicationClock;
        publicationClock.start();
        bool publishing = false, pendingLocalCopy = false, localCopyReady = true;
        quint64 localCopyRevision = 0;
        auto clipboardRevision = platform->clipboardRevision();
        QTimer localCopySettle;
        localCopySettle.setSingleShot(true);
        localCopySettle.setInterval(200);
        auto localClipboardChanged = [&] {
            if (publishing)
                return;
#ifdef Q_OS_MACOS
            const auto revision = platform->clipboardRevision();
            if (revision == clipboardRevision)
                return;
            clipboardRevision = revision;
#endif
            if (broker.owner().isEmpty() || platform->ownsClipboard())
                return;
            ++localCopyRevision;
            pendingLocalCopy = true;
            publication.localCopy();
            // Revoke the old source immediately, before a staged screenshot or
            // rich clipboard finishes publishing its representations.
            broker.publish({});
#ifdef Q_OS_MACOS
            platform->revoke();
            localCopyReady = false;
            localCopySettle.start();
#else
            broker.publish(platform->copiedFiles());
#endif
        };
        QObject::connect(&localCopySettle, &QTimer::timeout, [&] {
#ifdef Q_OS_MACOS
            if (publishing) {
                localCopySettle.start();
                return;
            }
            localClipboardChanged();
            if (localCopySettle.isActive() || broker.owner().isEmpty() || platform->ownsClipboard())
                return;
            const auto revision = clipboardRevision;
            const auto files = platform->copiedFiles();
            if (platform->clipboardRevision() != revision) {
                localClipboardChanged();
                return;
            }
            broker.publish(files);
            localCopyReady = true;
#endif
        });
        QObject::connect(&broker, &ClipboardBroker::sessionChanged, [&](bool active) {
            localCopySettle.stop();
            publication = ClipboardPublication();
            pendingLocalCopy = false;
            localCopyReady = true;
            // A new session observes only copies made after it opened. Never
            // read or upload content already present in the host pasteboard.
            clipboardRevision = platform->clipboardRevision();
            if (!active) {
                publishing = true;
                clipboardRevision = platform->revokeClipboard(clipboardRevision);
                publishing = false;
                clipboardLock.unlock();
            }
        });
        QObject::connect(clipboard, &QClipboard::dataChanged, localClipboardChanged);
        auto publishRemote = [&] {
            if (publishing || broker.owner().isEmpty())
                return;
#ifdef Q_OS_MACOS
            localClipboardChanged();
#endif
            const auto offer = broker.remoteOffer();
            if (!publication.begin(offer["id"].toString(), publicationClock.elapsed(), pendingLocalCopy))
                return;
            publishing = true;
            if (offer.isEmpty()) {
                clipboardRevision = platform->revokeClipboard(clipboardRevision);
                publication.succeeded();
            } else {
                const auto result = platform->publishIfUnchanged(
                    offer, [local, id = offer["id"].toString()](const QString& file, qint64 offset, int length) {
                        return local.call({ { "op", "client-read" }, { "offer", id }, { "file", file },
                            { "offset", offset }, { "length", length } });
                    }, clipboardRevision);
                if (result.status == NativeClipboardPublication::Published)
                    publication.succeeded();
                else if (result.status == NativeClipboardPublication::Superseded)
                    publication.localCopy();
                else
                    qWarning() << "Host OS clipboard publication failed; entries:" << offer["entries"].toArray().size();
#ifdef Q_OS_MACOS
                if (result.status != NativeClipboardPublication::Superseded)
                    clipboardRevision = result.revision;
#endif
            }
            publishing = false;
#ifdef Q_OS_MACOS
            // An external copy made during publication must remain observable.
            localClipboardChanged();
#endif
        };
        QObject::connect(&broker, &ClipboardBroker::remoteChanged, [&](QJsonObject) { publishRemote(); });
        QTimer clipboardWatch;
        clipboardWatch.setInterval(200);
        QObject::connect(&clipboardWatch, &QTimer::timeout, [&] {
#ifdef Q_OS_MACOS
            localClipboardChanged();
#endif
            publishRemote();
        });
        clipboardWatch.start();
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
            const bool exchange = request["op"] == "exchange";
#ifdef Q_OS_MACOS
            if (exchange)
                localClipboardChanged();
#endif
            const auto sentRevision = localCopyRevision;
            broker.execute(peer, request, [&, reply, exchange, sentRevision](QJsonObject result) {
                // Do not acknowledge a copy until its stable selection has
                // actually been included in a successful exchange response.
                if (exchange && result["ok"].toBool() && sentRevision == localCopyRevision
                    && localCopyReady)
                    pendingLocalCopy = false;
                reply(result);
            });
        };
        std::printf("{\"port\":%u}\n", server.serverPort());
        std::fflush(stdout);
        return QCoreApplication::exec();
    }
    if (args[1] != "--desk-clipboard-client" || args.size() != 6 || !clipboardLock.tryLock(0)) {
        qWarning() << "File clipboard helper unavailable: invalid arguments or another session owns the clipboard";
        return 3;
    }
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
    QString lease;
    ClipboardPublication publication;
    QElapsedTimer publicationClock;
    publicationClock.start();
    bool publishing = false;
    QJsonObject outgoing;
    bool changed = false, inflight = false;
    quint64 localCopyRevision = 0;
    QJsonArray responses;
#ifdef Q_OS_MACOS
    auto clipboardRevision = platform->clipboardRevision();
    bool pendingLocalCopy = false;
    qint64 localCopyReadyAt = 0;
#endif
    auto localClipboardChanged = [&] {
        // A callback during our own publication must not consume a newer copy
        // made by another application. Observe it after the write completes.
        if (publishing)
            return;
#ifdef Q_OS_MACOS
        const auto revision = platform->clipboardRevision();
        if (platform->ownsClipboard()) {
            clipboardRevision = revision;
            return;
        }
        if (revision != clipboardRevision) {
            clipboardRevision = revision;
            ++localCopyRevision;
            publication.localCopy();
            pendingLocalCopy = true;
            // Screenshot tools can append image representations without another
            // changeCount increment. Let one watcher interval elapse before
            // deciding whether this is an explicit file selection.
            localCopyReadyAt = publicationClock.elapsed() + 200;
            platform->revoke();
        }
        // Remember changes during the open handshake, but do not read their
        // contents until a session exists. Never upload the startup clipboard.
        if (lease.isEmpty() || !pendingLocalCopy || publicationClock.elapsed() < localCopyReadyAt)
            return;
        pendingLocalCopy = false;
#else
        if (lease.isEmpty() || platform->ownsClipboard())
            return;
        ++localCopyRevision;
        publication.localCopy();
#endif
        auto files = platform->copiedFiles();
        outgoing = files.isEmpty() ? QJsonObject() : local.publish(files);
        if (!outgoing.value("ok").toBool()) {
            if (!files.isEmpty())
                activity.notice(outgoing["error"].toString());
            outgoing = {};
            local.clear();
        }
        outgoingRead = activity.track(
            outgoing,
            [&local, id = outgoing.value("id").toString()](
                const QString& file, qint64 offset, int length) { return local.read(id, file, offset, length); },
            true);
        changed = true;
    };
    QObject::connect(clipboard, &QClipboard::dataChanged, &context, localClipboardChanged);
#ifdef Q_OS_MACOS
    // Qt Cocoa emits external clipboard changes when its application activates.
    // This accessory helper stays in the background, so also watch changeCount.
    QTimer clipboardWatch;
    clipboardWatch.setInterval(200);
    QObject::connect(&clipboardWatch, &QTimer::timeout, &context, localClipboardChanged);
    clipboardWatch.start();
#endif
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &context, [&] {
        if (inflight || lease.isEmpty())
            return;
#ifdef Q_OS_MACOS
        localClipboardChanged();
#endif
        inflight = true;
        QJsonObject request { { "op", "exchange" }, { "lease", lease }, { "responses", responses } };
        responses = {};
        if (changed && request["responses"].toArray().isEmpty()) {
            request["offer"] = outgoing;
            changed = false;
        }
        const auto sentRevision = localCopyRevision;
        const bool sentLocalCopy = request.contains("offer");
        remote.send(&context, request, [&, sentRevision, sentLocalCopy](QJsonObject result) {
            inflight = false;
            if (!result["ok"].toBool()) {
                activity.notice(result["error"].toString());
                platform->revoke();
                QCoreApplication::quit();
                return;
            }
            const auto offer = result.value("offer").toObject();
            const auto id = offer.value("id").toString();
#ifdef Q_OS_MACOS
            // A response can arrive before the 200 ms clipboard watcher runs.
            // Observe the current pasteboard before deciding whether to write.
            localClipboardChanged();
#endif
            const bool localCopyTakesPriority = sentLocalCopy || changed || localCopyRevision != sentRevision
#ifdef Q_OS_MACOS
                || pendingLocalCopy
#endif
                ;
            if (publication.begin(id, publicationClock.elapsed(), localCopyTakesPriority)) {
                publishing = true;
                if (offer.isEmpty()) {
#ifdef Q_OS_MACOS
                    clipboardRevision = platform->revokeClipboard(clipboardRevision);
#else
                    platform->revoke();
#endif
                    publication.succeeded();
                } else if (!NativeOfferStore::validManifest(offer)) {
#ifdef Q_OS_MACOS
                    clipboardRevision = platform->revokeClipboard(clipboardRevision);
#else
                    platform->revoke();
#endif
                    publication.rejected();
                    qWarning() << "Remote clipboard file manifest rejected; entries:" << offer["entries"].toArray().size();
                    activity.notice("The remote file list is invalid or exceeds the supported limits");
                } else {
                    const auto write = platform->publishIfUnchanged(offer,
                        activity.track(
                            offer,
                            [remote, lease, id](const QString& file, qint64 offset, int length) {
                                return remote.call({ { "op", "read" }, { "lease", lease }, { "offer", id },
                                    { "file", file }, { "offset", offset }, { "length", length } });
                            },
                            false),
#ifdef Q_OS_MACOS
                        clipboardRevision
#else
                        platform->clipboardRevision()
#endif
                    );
#ifdef Q_OS_MACOS
                    // Only acknowledge the revision generated by this write.
                    // Sampling the current count here could swallow a local
                    // screenshot copied while the write was in progress.
                    if (write.status != NativeClipboardPublication::Superseded)
                        clipboardRevision = write.revision;
#endif
                    if (write.status == NativeClipboardPublication::Superseded) {
                        publication.begin(id, publicationClock.elapsed(), true);
                        activity.clearNotice();
                    } else if (write.status == NativeClipboardPublication::Failed) {
                        qWarning() << "OS clipboard publication failed; entries:" << offer["entries"].toArray().size();
                        activity.notice("Cannot publish the remote file selection to the system clipboard");
                    } else {
                        publication.succeeded();
                        activity.clearNotice();
                        qInfo() << "Remote clipboard files published; entries:" << offer["entries"].toArray().size();
                    }
                }
                publishing = false;
#ifdef Q_OS_MACOS
                localClipboardChanged();
#endif
            }
            for (auto v : result["requests"].toArray()) {
                const auto r = v.toObject();
                auto answer = outgoingRead && r["offer"] == outgoing.value("id")
                    ? outgoingRead(r["file"].toString(), r["offset"].toVariant().toLongLong(), r["length"].toInt())
                    : QJsonObject { { "ok", false }, { "error", "Selection expired" } };
                answer["request"] = r["request"];
                responses.append(answer);
            }
        });
    });
    remote.send(&context, { { "op", "open" } }, [&](QJsonObject result) {
        lease = result["lease"].toString();
        qInfo() << "File clipboard session opened:" << !lease.isEmpty() << result["error"].toString();
        if (lease.isEmpty()) {
            activity.notice(result["error"].toString());
            QCoreApplication::quit();
        } else {
            // Establish the baseline from the open response, never from the
            // first poll: a peer may copy a new file immediately after opening.
            // Older hosts start with an empty selection and omit this field.
            publication.begin(result.value("offer").toObject().value("id").toString(),
                publicationClock.elapsed(), true);
            poll.start();
        }
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
