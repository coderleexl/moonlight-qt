#ifndef DESK_LOCALHOSTIDENTITY_H
#define DESK_LOCALHOSTIDENTITY_H
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStandardPaths>

namespace DeskLocalHost {
inline bool isLocalAddress(const QString& value)
{
    const QHostAddress address(value);
    if (address.isNull()) return false;
    if (address.isLoopback()) return true;
    for (const auto& local : QNetworkInterface::allAddresses()) {
        if (address.isEqual(local, QHostAddress::ConvertV4MappedToIPv4)) return true;
    }
    return false;
}
inline QString deviceId()
{
    QFile access(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                 + "/sunshine/desk-access.json");
    if (!access.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(access.readAll()).object().value("deviceId").toString();
}
inline bool matchesDeviceId(const QString& remoteId, const QString& localId)
{
    return !localId.isEmpty() && remoteId == localId;
}
}
#endif
