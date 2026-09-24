#include "jsontransport.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
void NativeTransport::send(QObject* lifetime, QJsonObject object, std::function<void(QJsonObject)> callback) const
{
    auto fail = [callback] { callback({ { "ok", false }, { "error", "Clipboard connection unavailable" } }); };
    if ((url.scheme() == "https" && peer.isNull())
        || (url.scheme() == "http" && (url.host() != "127.0.0.1" || bearer.isEmpty()))) {
        QTimer::singleShot(0, lifetime, fail);
        return;
    }
    auto manager = new QNetworkAccessManager(lifetime);
    manager->setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Connection", "close");
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + bearer);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto ssl = identity;
    ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
    request.setSslConfiguration(ssl);
    auto reply = manager->post(request, QJsonDocument(object).toJson(QJsonDocument::Compact));
    auto buffer = std::make_shared<QByteArray>();
    reply->setReadBufferSize(1024 * 1024);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply, pin = peer](const QList<QSslError>& errors) {
        if (reply->sslConfiguration().peerCertificate() != pin)
            return;
        for (const auto& e : errors)
            if (e.certificate() != pin)
                return;
        reply->ignoreSslErrors(errors);
    });
    QObject::connect(reply, &QNetworkReply::encrypted, reply, [reply, pin = peer] {
        if (reply->sslConfiguration().peerCertificate() != pin)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, buffer] {
        buffer->append(reply->readAll());
        if (buffer->size() > 1024 * 1024)
            reply->abort();
    });
    QTimer::singleShot(timeoutMs, reply, [reply] {
        if (!reply->isFinished())
            reply->abort();
    });
    QObject::connect(
        reply, &QNetworkReply::finished, lifetime, [reply, manager, buffer, pin = peer, url = url, callback] {
            buffer->append(reply->readAll());
            auto result = QJsonDocument::fromJson(*buffer).object();
            if (reply->error() != QNetworkReply::NoError
                || (url.scheme() == "https" && reply->sslConfiguration().peerCertificate() != pin)
                || buffer->size() > 1024 * 1024 || !result.contains("ok"))
                result = { { "ok", false },
                    { "error",
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
                            ? "Update Desk on both computers to use file clipboard"
                            : "Clipboard connection failed" } };
            manager->deleteLater();
            callback(result);
        });
}
QJsonObject NativeTransport::call(QJsonObject object) const
{
    QEventLoop loop;
    QJsonObject result;
    send(&loop, object, [&](QJsonObject r) {
        result = r;
        loop.quit();
    });
    loop.exec();
    return result;
}
