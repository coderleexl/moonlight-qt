#include "bridgehttp.h"
#include <QJsonDocument>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
BridgeHttp::BridgeHttp(QByteArray token, QObject* parent)
    : QTcpServer(parent)
    , m_Token(token)
{
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto socket = nextPendingConnection()) {
            auto bytes = std::make_shared<QByteArray>();
            auto handled = std::make_shared<bool>(false);
            socket->setReadBufferSize(1024 * 1024);
            QTimer::singleShot(35000, socket, [socket] { socket->abort(); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes, handled] {
                if (*handled)
                    return;
                bytes->append(socket->readAll());
                if (bytes->size() > 1024 * 1024) {
                    socket->abort();
                    return;
                }
                int split = bytes->indexOf("\r\n\r\n");
                if (split < 0)
                    return;
                int length = -1;
                bool authorized = false;
                for (const auto& line : bytes->left(split).split('\n')) {
                    auto lower = line.toLower();
                    if (lower.startsWith("content-length:")) {
                        bool ok = false;
                        length = line.mid(15).trimmed().toInt(&ok);
                        if (!ok)
                            length = -1;
                    }
                    if (lower.startsWith("authorization:"))
                        authorized = line.mid(14).trimmed() == "Bearer " + m_Token;
                }
                if (!authorized || length < 0 || length > 768 * 1024 || !bytes->startsWith("POST / ")) {
                    socket->abort();
                    return;
                }
                if (bytes->size() < split + 4 + length)
                    return;
                *handled = true;
                QPointer<QTcpSocket> guard(socket);
                auto reply = [guard](QJsonObject object) {
                    if (!guard)
                        return;
                    auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
                    guard->write(
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(data.size()) + "\r\n\r\n" + data);
                    guard->disconnectFromHost();
                };
                if (handler)
                    handler(QJsonDocument::fromJson(bytes->mid(split + 4, length)).object(), reply);
                else
                    reply({ { "ok", false }, { "error", "Bridge unavailable" } });
            });
        }
    });
}
