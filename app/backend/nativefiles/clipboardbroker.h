#ifndef DESK_CLIPBOARDBROKER_H
#define DESK_CLIPBOARDBROKER_H
#include "offerstore.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>

// Lives in the host user's helper process. Each lease has exactly one TLS peer.
class ClipboardBroker : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(QJsonObject)>;
    explicit ClipboardBroker(QObject* parent = nullptr, int leaseTimeoutMs = 10000);
    void execute(const QString& peer, const QJsonObject& request, Reply reply);
    void publish(const QStringList& files);
    void requestClient(const QString& offer, const QString& file, qint64 offset, int length, Reply reply);
    void expire();
    QString owner() const { return m_Owner; }
    QJsonObject remoteOffer() const { return m_Remote; }
    void reset();
signals:
    void remoteChanged(QJsonObject offer);
    void sessionChanged(bool active);

private:
    struct Pending {
        QJsonObject request;
        Reply reply;
        qint64 deadline;
    };
    QString m_Owner, m_Lease;
    QElapsedTimer m_Clock;
    qint64 m_Expires = 0;
    int m_LeaseTimeoutMs;
    NativeOfferStore m_Local;
    QJsonObject m_Remote;
    QHash<QString, Pending> m_Pending;
};
#endif
