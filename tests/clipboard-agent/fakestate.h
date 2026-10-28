#ifndef CLIPBOARD_AGENT_FAKESTATE_H
#define CLIPBOARD_AGENT_FAKESTATE_H

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <functional>

inline QJsonObject readFakeClipboard(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

inline QJsonObject changeFakeClipboard(
    const QString& path, const std::function<void(QJsonObject&)>& change)
{
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(3000))
        qFatal("Cannot lock isolated clipboard test state");
    auto state = readFakeClipboard(path);
    change(state);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(state).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        qFatal("Cannot save isolated clipboard test state");
    return state;
}

#endif
