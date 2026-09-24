#ifndef DESK_NATIVEACTIVITY_H
#define DESK_NATIVEACTIVITY_H
#include "nativeplatform.h"
#include <QMap>
#include <QTimer>
#include <atomic>
class NativeActivity : public QObject {
public:
    explicit NativeActivity(const QString& key, QObject* parent = nullptr);
    ~NativeActivity() override;
    NativeRead track(const QJsonObject& offer, NativeRead reader, bool upload);
    void notice(const QString& message);
    static QString peerKey(const QUrl& filesUrl, const QByteArray& certificate);
    static QString directory();

private:
    struct State {
        std::atomic<bool> cancelled { false };
    };
    QString m_Path, m_Session;
    QMap<QString, std::weak_ptr<State>> m_State;
    QMap<QString, QJsonObject> m_Jobs;
    QMap<QString, QMap<qint64, qint64>> m_Ranges;
    QTimer m_Timer;
    void save();
};
#endif
