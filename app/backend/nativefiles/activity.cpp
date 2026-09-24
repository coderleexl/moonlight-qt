#include "activity.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
QString NativeActivity::directory()
{
    auto path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/native-transfers";
    QDir().mkpath(path);
    return path;
}
QString NativeActivity::peerKey(const QUrl& url, const QByteArray& certificate)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toEncoded() + certificate, QCryptographicHash::Sha256).toHex());
}
NativeActivity::NativeActivity(const QString& key, QObject* parent)
    : QObject(parent)
    , m_Session(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_Path = directory() + '/' + key + '-' + m_Session + ".json";
    m_Timer.setInterval(200);
    connect(&m_Timer, &QTimer::timeout, this, [this] {
        QFile file(m_Path + ".cancel");
        if (!file.open(QIODevice::ReadOnly))
            return;
        auto request = QJsonDocument::fromJson(file.read(65536)).object();
        file.close();
        file.remove();
        if (request["session"] != m_Session)
            return;
        auto id = request["id"].toString();
        if (!m_State.contains(id))
            return;
        if (auto state = m_State[id].lock())
            state->cancelled = true;
        if (m_Jobs.contains(id)) {
            m_Jobs[id]["state"] = "cancelled";
            save();
        }
    });
    m_Timer.start();
}
NativeActivity::~NativeActivity()
{
    for (auto weak : m_State)
        if (auto state = weak.lock())
            state->cancelled = true;
    for (auto it = m_Jobs.begin(); it != m_Jobs.end(); ++it)
        if ((*it)["state"] == "running")
            (*it)["state"] = "cancelled";
    save();
}
void NativeActivity::save()
{
    if (m_Jobs.isEmpty())
        return;
    QJsonArray jobs;
    for (auto job : m_Jobs)
        jobs.append(job);
    QSaveFile file(m_Path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject { { "session", m_Session }, { "pid", QCoreApplication::applicationPid() },
                                 { "updated", QDateTime::currentMSecsSinceEpoch() }, { "jobs", jobs } })
            .toJson(QJsonDocument::Compact));
    file.commit();
}
NativeRead NativeActivity::track(const QJsonObject& offer, NativeRead reader, bool upload)
{
    for (auto it = m_State.begin(); it != m_State.end();)
        if (it.value().expired())
            it = m_State.erase(it);
        else
            ++it;
    QMap<QString, std::shared_ptr<State>> states;
    QMap<QString, QJsonObject> entries;
    for (auto v : offer["entries"].toArray()) {
        auto e = v.toObject();
        auto id = e["id"].toString();
        states[id] = std::make_shared<State>();
        m_State[id] = states[id];
        entries[id] = e;
    }
    QPointer<NativeActivity> guard(this);
    return [guard, states, entries, reader, upload](const QString& id, qint64 offset, int length) {
        if (!guard || !states.contains(id) || states[id]->cancelled)
            return QJsonObject { { "ok", false }, { "error", "Transfer cancelled" } };
        auto result = reader(id, offset, length);
        if (guard)
            QMetaObject::invokeMethod(
                guard,
                [guard, id, offset, entries, result, upload] {
                    if (!guard)
                        return;
                    auto e = entries[id];
                    auto size = qint64(e["size"].toDouble());
                    auto& job = guard->m_Jobs[id];
                    if (offset == 0 && job["state"] == "done")
                        guard->m_Ranges.remove(id);
                    job = { { "id", id }, { "name", e["path"] }, { "size", size }, { "done", 0 }, { "upload", upload },
                        { "native", true }, { "state", "running" }, { "detail", "" }, { "speed", 0 },
                        { "committing", false } };
                    if (!result["ok"].toBool()) {
                        job["state"] = "failed";
                        job["detail"] = result["error"].toString("File transfer failed");
                    } else {
                        auto bytes = QByteArray::fromBase64(result["data"].toString().toLatin1()).size();
                        auto& ranges = guard->m_Ranges[id];
                        qint64 begin = offset, end = offset + bytes;
                        for (auto it = ranges.begin(); it != ranges.end();)
                            if (it.key() <= end && it.value() >= begin) {
                                begin = qMin(begin, it.key());
                                end = qMax(end, it.value());
                                it = ranges.erase(it);
                            } else
                                ++it;
                        ranges[begin] = end;
                        qint64 done = 0;
                        for (auto it = ranges.cbegin(); it != ranges.cend(); ++it)
                            done += it.value() - it.key();
                        job["done"] = done;
                        if (done >= size)
                            job["state"] = "done";
                    }
                    if (auto state = guard->m_State[id].lock())
                        if (state->cancelled)
                            job["state"] = "cancelled";
                    while (guard->m_Jobs.size() > 100) {
                        auto remove = guard->m_Jobs.begin();
                        while (remove != guard->m_Jobs.end()
                            && (remove.key() == id || remove.value()["state"] == "running"))
                            ++remove;
                        if (remove == guard->m_Jobs.end())
                            break;
                        guard->m_Ranges.remove(remove.key());
                        guard->m_Jobs.erase(remove);
                    }
                    guard->save();
                },
                Qt::QueuedConnection);
        return result;
    };
}

void NativeActivity::notice(const QString& message)
{
    m_Jobs["clipboard"]
        = { { "id", "clipboard" }, { "name", QCoreApplication::translate("FileTransfer", "File clipboard") },
              { "size", 0 }, { "done", 0 }, { "upload", false }, { "native", true }, { "state", "failed" },
              { "detail", message }, { "speed", 0 }, { "committing", false } };
    save();
}
