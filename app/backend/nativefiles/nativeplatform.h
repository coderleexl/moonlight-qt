#ifndef DESK_NATIVEPLATFORM_H
#define DESK_NATIVEPLATFORM_H
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QWindow>
#include <functional>
#include <memory>
using NativeRead = std::function<QJsonObject(const QString&, qint64, int)>;

class NativeFilePlatform : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setAgentMode() { }
    virtual bool pendingDrag() const { return false; }
    virtual bool supported() const = 0;
    virtual QStringList copiedFiles() const = 0;
    virtual bool ownsClipboard() const = 0;
    virtual bool publish(const QJsonObject& offer, NativeRead read) = 0;
    virtual void revoke() = 0;
    virtual bool drag(const QJsonObject& offer, NativeRead read, QWindow* window) = 0;
    static std::unique_ptr<NativeFilePlatform> create();
signals:
    void error(QString message);
    void progress(QString file, qint64 done, qint64 total);
};
#ifdef DESK_NATIVE_TESTS
QJsonObject nativeAdapterProbe(const QJsonObject& offer, NativeRead read, const QString& destination);
#endif
#endif
