#ifndef DESK_MATERIALIZE_H
#define DESK_MATERIALIZE_H
#include "nativeplatform.h"
#include "offerstore.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
inline bool nativeDestinationSafe(QString path)
{
    // macOS exposes its temporary directory through /var -> /private/var.
    const auto temporary = QDir::cleanPath(QDir::tempPath());
    if (path.startsWith(temporary + '/'))
        path = QFileInfo(temporary).canonicalFilePath() + path.mid(temporary.size());
    QFileInfo info(path);
    for (;;) {
        if (info.isSymLink())
            return false;
        const auto parent = info.dir().absolutePath();
        if (parent == info.absoluteFilePath())
            return true;
        info.setFile(parent);
    }
}
// The caller supplies an OS-authorized exact destination for one top-level item.
inline QString materializeNative(
    const QJsonObject& offer, const NativeRead& read, const QString& root, const QString& destination)
{
    if (!NativeOfferStore::validManifest(offer) || root.contains('/') || !NativeOfferStore::safePath(root)
        || !QFileInfo(destination).isAbsolute())
        return "Invalid file destination";
    for (auto value : offer["entries"].toArray()) {
        auto e = value.toObject();
        auto path = e["path"].toString();
        if (path != root && !path.startsWith(root + '/'))
            continue;
        auto target = destination + path.mid(root.size());
        QFileInfo info(target);
        if (!nativeDestinationSafe(target))
            return "Destination is a symbolic link";
        if (e["directory"].toBool()) {
            if (info.exists() && !info.isDir())
                return "Destination already exists";
            if (!QDir().mkpath(target))
                return "Cannot create destination directory";
            continue;
        }
        if (info.exists())
            return "Destination already exists";
        auto ready = read(e["id"].toString(), 0, 0);
        if (!ready["ok"].toBool())
            return ready["error"].toString("Remote read failed");
        QSaveFile file(target);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            return file.errorString();
        qint64 offset = 0, total = qint64(e["size"].toDouble());
        while (offset < total) {
            int size = int(qMin<qint64>(256 * 1024, total - offset));
            auto r = read(e["id"].toString(), offset, size);
            auto encoded = r["data"].toString().toLatin1();
            auto bytes = QByteArray::fromBase64(encoded);
            if (!r["ok"].toBool())
                return r["error"].toString("Remote read failed");
            if (bytes.size() != size || bytes.toBase64() != encoded)
                return "Invalid file data";
            if (file.write(bytes) != bytes.size())
                return file.errorString();
            offset += bytes.size();
        }
        if (!nativeDestinationSafe(target) || QFileInfo::exists(target))
            return "Destination changed during transfer";
        if (!file.commit())
            return file.errorString();
    }
    return {};
}
#endif
