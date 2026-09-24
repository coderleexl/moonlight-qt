#define FUSE_USE_VERSION 31
#include "nativeplatform.h"
#include "offerstore.h"
#include <QClipboard>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QUrl>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <thread>
#include <unistd.h>
namespace {
const char* marker = "application/x-desk-file-offer";
struct Selection {
    QJsonObject offer;
    NativeRead read;
    std::atomic<bool> active { true };
};
class X11Files final : public NativeFilePlatform {
    QTemporaryDir directory;
    struct fuse* filesystem = nullptr;
    std::thread worker;
    mutable QMutex mutex;
    std::shared_ptr<Selection> selection;
    QByteArray owned;
    static X11Files* self() { return static_cast<X11Files*>(fuse_get_context()->private_data); }
    std::shared_ptr<Selection> current()
    {
        QMutexLocker lock(&mutex);
        return selection;
    }
    static QJsonObject entry(const std::shared_ptr<Selection>& s, const char* path)
    {
        if (!s || !s->active)
            return {};
        auto name = QString::fromUtf8(path);
        auto prefix = '/' + s->offer["id"].toString() + '/';
        if (!name.startsWith(prefix))
            return {};
        name = name.mid(prefix.size());
        for (auto value : s->offer["entries"].toArray())
            if (value.toObject()["path"] == name)
                return value.toObject();
        return {};
    }
    static int attributes(const char* path, struct stat* st, struct fuse_file_info*)
    {
        memset(st, 0, sizeof(*st));
        auto s = self()->current();
        QString name = QString::fromUtf8(path);
        auto e = entry(s, path);
        bool dir = name == "/" || (s && name == '/' + s->offer["id"].toString()) || e["directory"].toBool();
        if (!dir && e.isEmpty())
            return -ENOENT;
        st->st_uid = getuid();
        st->st_gid = getgid();
        st->st_mode = (dir ? S_IFDIR | 0500 : S_IFREG | 0400);
        st->st_nlink = dir ? 2 : 1;
        st->st_size = off_t(e["size"].toDouble());
        return 0;
    }
    static int list(const char* path, void* buffer, fuse_fill_dir_t fill, off_t offset, struct fuse_file_info*,
        enum fuse_readdir_flags)
    {
        auto s = self()->current();
        if (!s || !s->active || offset < 0)
            return -ENOENT;
        QStringList names { ".", ".." };
        QString parent = QString::fromUtf8(path);
        if (parent == "/")
            names.append(s->offer["id"].toString());
        else {
            auto prefix = '/' + s->offer["id"].toString();
            if (parent != prefix && !parent.startsWith(prefix + '/'))
                return -ENOENT;
            if (parent != prefix && !entry(s, path)["directory"].toBool())
                return -ENOTDIR;
            parent = parent.mid(prefix.size());
            if (parent.startsWith('/'))
                parent.remove(0, 1);
            for (auto v : s->offer["entries"].toArray()) {
                auto name = v.toObject()["path"].toString();
                auto slash = name.lastIndexOf('/');
                if ((slash < 0 ? QString() : name.left(slash)) == parent)
                    names.append(name.mid(slash + 1));
            }
        }
        for (qsizetype i = offset; i < names.size(); ++i) {
            auto name = names[i].toUtf8();
            if (fill(buffer, name.constData(), nullptr, i + 1, fuse_fill_dir_flags(0)))
                break;
        }
        return 0;
    }
    static int openFile(const char* path, struct fuse_file_info* info)
    {
        if ((info->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;
        auto e = entry(self()->current(), path);
        info->direct_io = 1;
        if (e.isEmpty() || e["directory"].toBool())
            return -ENOENT;
        auto s = self()->current();
        return s && s->read(e["id"].toString(), 0, 0)["ok"].toBool() ? 0 : -EIO;
    }
    static int readFile(const char* path, char* out, size_t size, off_t offset, struct fuse_file_info*)
    {
        auto s = self()->current();
        auto e = entry(s, path);
        if (e.isEmpty() || offset < 0)
            return -ENOENT;
        auto total = qint64(e["size"].toDouble());
        if (offset >= total)
            return 0;
        int length = int(qMin<qint64>(qMin<size_t>(size, 256 * 1024), total - offset));
        auto result = s->read(e["id"].toString(), offset, length);
        auto encoded = result["data"].toString().toLatin1();
        auto bytes = QByteArray::fromBase64(encoded);
        if (!s->active || !result["ok"].toBool() || bytes.size() != length || bytes.toBase64() != encoded)
            return -EIO;
        memcpy(out, bytes.constData(), bytes.size());
        return bytes.size();
    }
    QList<QUrl> urls(const QJsonObject& offer) const
    {
        QList<QUrl> list;
        for (auto e : offer["entries"].toArray()) {
            auto path = e.toObject()["path"].toString();
            if (!path.contains('/'))
                list.append(QUrl::fromLocalFile(directory.path() + '/' + offer["id"].toString() + '/' + path));
        }
        return list;
    }
    bool select(const QJsonObject& offer, NativeRead read)
    {
        if (!filesystem || !NativeOfferStore::validManifest(offer))
            return false;
        QMutexLocker lock(&mutex);
        if (selection)
            selection->active = false;
        selection = std::make_shared<Selection>();
        selection->offer = offer;
        selection->read = read;
        return true;
    }

public:
#ifdef DESK_NATIVE_TESTS
    QJsonObject probe(const QJsonObject& offer, NativeRead read, const QString& destination)
    {
        if (!select(offer, read))
            return { { "skip", true }, { "error", "X11/FUSE mount is unavailable on this runner" } };
        QFile input(urls(offer).first().toLocalFile()), output(destination);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            return { { "ok", false } };
        while (!input.atEnd()) {
            auto bytes = input.read(65536);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return { { "ok", false } };
        }
        return { { "ok", true } };
    }
#endif
    X11Files()
    {
        if (QGuiApplication::platformName() != "xcb" || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            || !directory.isValid())
            return;
        fuse_operations operations {};
        operations.getattr = attributes;
        operations.readdir = list;
        operations.open = openFile;
        operations.read = readFile;
        char program[] = "desk", option[] = "-o",
             value[] = "ro,auto_unmount,default_permissions,attr_timeout=0,entry_timeout=0,negative_timeout=0";
        char* argv[] = { program, option, value };
        fuse_args args = FUSE_ARGS_INIT(3, argv);
        filesystem = fuse_new(&args, &operations, sizeof(operations), this);
        fuse_opt_free_args(&args);
        if (!filesystem)
            return;
        if (fuse_mount(filesystem, QFile::encodeName(directory.path()).constData()) != 0) {
            fuse_destroy(filesystem);
            filesystem = nullptr;
            return;
        }
        worker = std::thread([this] { fuse_loop(filesystem); });
    }
    ~X11Files() override
    {
        revoke();
        if (filesystem) {
            fuse_exit(filesystem);
            fuse_unmount(filesystem);
            if (worker.joinable())
                worker.join();
            fuse_destroy(filesystem);
        }
    }
    bool supported() const override { return filesystem != nullptr; }
    bool ownsClipboard() const override
    {
        return !owned.isEmpty() && QGuiApplication::clipboard()->mimeData()->data(marker) == owned;
    }
    QStringList copiedFiles() const override
    {
        QStringList files;
        if (ownsClipboard())
            return files;
        for (auto url : QGuiApplication::clipboard()->mimeData()->urls())
            if (url.isLocalFile() && !url.toLocalFile().startsWith(directory.path() + '/'))
                files.append(url.toLocalFile());
        return files;
    }
    bool publish(const QJsonObject& offer, NativeRead read) override
    {
        if (!select(offer, read))
            return false;
        owned = offer["id"].toString().toUtf8();
        auto data = new QMimeData();
        auto list = urls(offer);
        data->setUrls(list);
        data->setData(marker, owned);
        QByteArray gnome = "copy";
        for (const auto& url : list)
            gnome += '\n' + url.toEncoded();
        data->setData("x-special/gnome-copied-files", gnome);
        data->setData("application/x-kde-cutselection", "0");
        QGuiApplication::clipboard()->setMimeData(data);
        return true;
    }
    void revoke() override
    {
        if (ownsClipboard())
            QGuiApplication::clipboard()->clear();
        owned.clear();
        QMutexLocker lock(&mutex);
        if (selection)
            selection->active = false;
        selection.reset();
    }
    bool drag(const QJsonObject& offer, NativeRead read, QWindow* window) override
    {
        if (!select(offer, read))
            return false;
        QDrag drag(window);
        auto data = new QMimeData();
        data->setUrls(urls(offer));
        drag.setMimeData(data);
        return drag.exec(Qt::CopyAction) == Qt::CopyAction;
    }
};
}
std::unique_ptr<NativeFilePlatform> NativeFilePlatform::create() { return std::make_unique<X11Files>(); }

#ifdef DESK_NATIVE_TESTS
QJsonObject nativeAdapterProbe(const QJsonObject& offer, NativeRead read, const QString& destination)
{
    X11Files platform;
    return platform.probe(offer, read, destination);
}
#endif
