#include "clipboardbroker.h"
#include <QJsonArray>
#include <QTimer>
#include <QUuid>
#include <utility>
namespace {
QJsonObject failure(const char* reason) { return { { "ok", false }, { "error", QString::fromLatin1(reason) } }; }
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
ClipboardBroker::ClipboardBroker(QObject* parent, int leaseTimeoutMs)
    : QObject(parent)
    , m_LeaseTimeoutMs(leaseTimeoutMs)
{
    m_Clock.start();
    auto timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &ClipboardBroker::expire);
    timer->start();
}
void ClipboardBroker::reset()
{
    m_Owner.clear();
    m_Lease.clear();
    m_Local.clear();
    m_Remote = {};
    const auto pending = std::exchange(m_Pending, {});
    for (auto p : pending)
        p.reply(failure("Clipboard session ended"));
    emit remoteChanged({});
    emit sessionChanged(false);
}
void ClipboardBroker::expire()
{
    if (!m_Owner.isEmpty() && m_Clock.elapsed() > m_Expires) {
        reset();
        return;
    }
    QStringList expired;
    for (auto it = m_Pending.cbegin(); it != m_Pending.cend(); ++it)
        if (it->deadline < m_Clock.elapsed())
            expired.append(it.key());
    for (const auto& id : expired)
        m_Pending.take(id).reply(failure("Read timed out"));
}
void ClipboardBroker::publish(const QStringList& files)
{
    if (m_Owner.isEmpty())
        return;
    if (files.isEmpty())
        m_Local.clear();
    else
        m_Local.publish(files);
}
void ClipboardBroker::requestClient(const QString& offer, const QString& file, qint64 offset, int length, Reply reply)
{
    expire();
    if (m_Owner.isEmpty() || offer.isEmpty() || offer != m_Remote["id"].toString() || m_Pending.size() >= 2
        || offset < 0 || length < 0 || length > 256 * 1024) {
        reply(failure("Selection unavailable"));
        return;
    }
    bool valid = false;
    for (auto v : m_Remote["entries"].toArray()) {
        const auto e = v.toObject();
        if (e["id"] == file && !e["directory"].toBool() && offset <= e["size"].toDouble())
            valid = true;
    }
    if (!valid) {
        reply(failure("Invalid file or offset"));
        return;
    }
    auto id = token();
    m_Pending.insert(id,
        { { { "request", id }, { "offer", offer }, { "file", file }, { "offset", offset }, { "length", length } },
            reply, m_Clock.elapsed() + 30000 });
}
void ClipboardBroker::execute(const QString& peer, const QJsonObject& r, Reply reply)
{
    expire();
    const auto op = r["op"].toString();
    if (peer.isEmpty()) {
        reply(failure("Missing client identity"));
        return;
    }
    if (op == "capabilities") {
        reply({ { "ok", true }, { "protocol", 1 }, { "fileClipboard", true } });
        return;
    }
    if (op == "open") {
        if (!m_Owner.isEmpty()) {
            reply(failure("Another clipboard session is active"));
            return;
        }
        m_Owner = peer;
        m_Lease = token() + token();
        m_Expires = m_Clock.elapsed() + m_LeaseTimeoutMs;
        emit sessionChanged(true);
        reply({ { "ok", true }, { "lease", m_Lease } });
        return;
    }
    if (peer != m_Owner || r["lease"].toString() != m_Lease || m_Lease.isEmpty()) {
        reply(failure("Invalid clipboard session"));
        return;
    }
    m_Expires = m_Clock.elapsed() + m_LeaseTimeoutMs;
    if (op == "close") {
        reset();
        reply({ { "ok", true } });
        return;
    }
    if (op == "read") {
        reply(m_Local.read(
            r["offer"].toString(), r["file"].toString(), r["offset"].toVariant().toLongLong(), r["length"].toInt()));
        return;
    }
    if (op != "exchange") {
        reply(failure("Unknown clipboard operation"));
        return;
    }
    if (r.contains("offer")) {
        auto offer = r["offer"].toObject();
        if (!offer.isEmpty() && !NativeOfferStore::validManifest(offer)) {
            reply(failure("Invalid file manifest"));
            return;
        }
        if (offer["id"] != m_Remote["id"]) {
            m_Remote = offer;
            const auto pending = std::exchange(m_Pending, {});
            for (auto p : pending)
                p.reply(failure("Selection replaced"));
            emit remoteChanged(offer);
        }
    }
    for (auto value : r["responses"].toArray()) {
        const auto response = value.toObject();
        const auto id = response["request"].toString();
        if (!m_Pending.contains(id))
            continue;
        const auto pending = m_Pending.take(id);
        auto encoded = response["data"].toString().toLatin1();
        const auto bytes = QByteArray::fromBase64(encoded);
        if (response["ok"].toBool()
            && (encoded.size() > 350000 || bytes.toBase64() != encoded
                || bytes.size() > pending.request["length"].toInt()))
            pending.reply(failure("Invalid read response"));
        else
            pending.reply(response);
    }
    QJsonArray requests;
    for (auto p : m_Pending)
        requests.append(p.request);
    auto offer = m_Local.manifest();
    if (!offer["ok"].toBool())
        offer = {};
    reply({ { "ok", true }, { "offer", offer }, { "requests", requests } });
}
