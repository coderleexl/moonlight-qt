#ifndef DESK_NATIVE_OFFERSTORE_H
#define DESK_NATIVE_OFFERSTORE_H
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QStringList>

// A snapshot of explicitly selected files. Only opaque IDs cross the connection.
class NativeOfferStore {
public:
    QJsonObject publish(const QStringList& paths);
    QJsonObject manifest() const;
    QJsonObject read(const QString& offer, const QString& file, qint64 offset, int length);
    void clear();
    qint64 bytesRead() const { return m_BytesRead; }
    static bool validManifest(const QJsonObject& offer);
    static bool safePath(const QString& relative);

private:
    struct Entry {
        QString path, version;
        qint64 size;
        bool directory;
    };
    mutable QMutex m_Mutex;
    QHash<QString, Entry> m_Entries;
    QJsonObject m_Manifest;
    qint64 m_BytesRead = 0;
};
#endif
