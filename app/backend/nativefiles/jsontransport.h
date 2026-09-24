#ifndef DESK_JSONTRANSPORT_H
#define DESK_JSONTRANSPORT_H
#include <QJsonObject>
#include <QObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>
#include <functional>
struct NativeTransport {
    QUrl url;
    QSslConfiguration identity;
    QSslCertificate peer;
    QByteArray bearer;
    int timeoutMs = 30000;
    void send(QObject* lifetime, QJsonObject request, std::function<void(QJsonObject)> reply) const;
    QJsonObject call(QJsonObject request) const;
};
#endif
