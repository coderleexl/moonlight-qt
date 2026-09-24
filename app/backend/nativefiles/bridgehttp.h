#ifndef DESK_BRIDGEHTTP_H
#define DESK_BRIDGEHTTP_H
#include <QJsonObject>
#include <QTcpServer>
#include <functional>
class BridgeHttp : public QTcpServer {
public:
    using Reply = std::function<void(QJsonObject)>;
    std::function<void(QJsonObject, Reply)> handler;
    explicit BridgeHttp(QByteArray token, QObject* parent = nullptr);

private:
    QByteArray m_Token;
};
#endif
