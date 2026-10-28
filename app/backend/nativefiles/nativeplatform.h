#ifndef DESK_NATIVEPLATFORM_H
#define DESK_NATIVEPLATFORM_H
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QWindow>
#include <functional>
#include <memory>
#include <utility>
using NativeRead = std::function<QJsonObject(const QString&, qint64, int)>;

struct NativeClipboardPublication {
    enum Status { Published, Failed, Superseded };
    Status status;
    // Revision produced by this operation, not a later external clipboard write.
    qint64 revision;
};

class NativeFilePlatform : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setAgentMode() { }
    virtual qint64 clipboardRevision() const { return 0; }
    virtual bool pendingDrag() const { return false; }
    virtual bool supported() const = 0;
    virtual QStringList copiedFiles() const = 0;
    virtual bool ownsClipboard() const = 0;
    virtual bool publish(const QJsonObject& offer, NativeRead read) = 0;
    virtual NativeClipboardPublication publishIfUnchanged(
        const QJsonObject& offer, NativeRead read, qint64 expectedRevision)
    {
        if (clipboardRevision() != expectedRevision)
            return { NativeClipboardPublication::Superseded, expectedRevision };
        const bool written = publish(offer, std::move(read));
        return { written ? NativeClipboardPublication::Published : NativeClipboardPublication::Failed,
            clipboardRevision() };
    }
    virtual void revoke() = 0;
    virtual qint64 revokeClipboard(qint64 expectedRevision)
    {
        if (clipboardRevision() == expectedRevision)
            revoke();
        return expectedRevision;
    }
    virtual bool drag(const QJsonObject& offer, NativeRead read, QWindow* window) = 0;
    static std::unique_ptr<NativeFilePlatform> create();
signals:
    void error(QString message);
    void progress(QString file, qint64 done, qint64 total);
};
#ifdef DESK_NATIVE_TESTS
QJsonObject nativeAdapterProbe(const QJsonObject& offer, NativeRead read, const QString& destination);
#ifdef Q_OS_MACOS
QJsonObject nativeClipboardProbe(const QJsonObject& offer, NativeRead read);
QJsonObject nativeImageClipboardProbe(const QString& file);
QJsonObject nativePasteWatcherProbe(const QJsonObject& offer, NativeRead read, const QString& destination);
#endif
#endif
#endif
