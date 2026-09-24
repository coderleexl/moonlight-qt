#include "offerstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUuid>
#include <functional>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
QString randomId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QJsonObject error(const char* text) { return { { "ok", false }, { "error", QString::fromLatin1(text) } }; }
// Reject links in every path component, including a replaced parent directory.
bool safeLocal(const QString& path)
{
    QFileInfo f(path);
    if (!f.isAbsolute())
        return false;
    for (;;) {
        if (f.isSymLink())
            return false;
#ifdef Q_OS_WIN
        auto flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(f.absoluteFilePath().utf16()));
        if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
#endif
        auto parent = f.dir().absolutePath();
        if (parent == f.absoluteFilePath())
            break;
        f.setFile(parent);
    }
    return true;
}
QString stamp(const QString& path, int fd = -1)
{
#ifdef Q_OS_WIN
    HANDLE h = fd >= 0 ? reinterpret_cast<HANDLE>(_get_osfhandle(fd))
                       : CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    BY_HANDLE_FILE_INFORMATION i {};
    bool ok = GetFileInformationByHandle(h, &i);
    if (fd < 0)
        CloseHandle(h);
    if (!ok || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return {};
    return QString("%1:%2:%3:%4:%5:%6:%7")
        .arg(i.dwVolumeSerialNumber)
        .arg(i.nFileIndexHigh)
        .arg(i.nFileIndexLow)
        .arg(i.nFileSizeHigh)
        .arg(i.nFileSizeLow)
        .arg(i.ftLastWriteTime.dwHighDateTime)
        .arg(i.ftLastWriteTime.dwLowDateTime);
#else
    struct stat s {};
    if ((fd >= 0 ? fstat(fd, &s) : lstat(QFile::encodeName(path).constData(), &s)) != 0 || !S_ISREG(s.st_mode))
        return {};
#ifdef Q_OS_MACOS
    auto time = s.st_mtimespec;
#else
    auto time = s.st_mtim;
#endif
    return QString("%1:%2:%3:%4:%5")
        .arg(qulonglong(s.st_dev))
        .arg(qulonglong(s.st_ino))
        .arg(s.st_size)
        .arg(time.tv_sec)
        .arg(time.tv_nsec);
#endif
}
}
bool NativeOfferStore::safePath(const QString& name)
{
    if (name.isEmpty() || name.size() > 4096 || name.startsWith('/') || name.contains('\\'))
        return false;
    static const QRegularExpression reserved(
        "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression invalid("[\\x00-\\x1f:*?\"<>|]");
    for (const auto& part : name.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')
            || invalid.match(part).hasMatch() || reserved.match(part).hasMatch())
            return false;
    return true;
}
bool NativeOfferStore::validManifest(const QJsonObject& offer)
{
    const auto list = offer.value("entries").toArray();
    if (offer["id"].toString().isEmpty() || offer["id"].toString().size() > 128 || list.isEmpty() || list.size() > 10000
        || QJsonDocument(offer).toJson().size() > 512 * 1024)
        return false;
    static const QRegularExpression identifier("^[A-Za-z0-9_-]{1,128}$");
    if (!identifier.match(offer["id"].toString()).hasMatch())
        return false;
    QSet<QString> ids, paths, directories;
    for (const auto& v : list) {
        const auto e = v.toObject();
        const auto id = e["id"].toString(), path = e["path"].toString();
        const auto size = e["size"].toDouble(-1);
        if (!identifier.match(id).hasMatch() || ids.contains(id) || !safePath(path)
            || paths.contains(path.toCaseFolded()) || size < 0 || size > 9007199254740991.0 || size != qint64(size))
            return false;
        auto slash = path.lastIndexOf('/');
        if (slash >= 0 && !directories.contains(path.left(slash)))
            return false;
        ids.insert(id);
        paths.insert(path.toCaseFolded());
        if (e["directory"].toBool())
            directories.insert(path);
    }
    return true;
}
QJsonObject NativeOfferStore::publish(const QStringList& paths)
{
    QMutexLocker lock(&m_Mutex);
    m_Entries.clear();
    m_Manifest = {};
    m_BytesRead = 0;
    QHash<QString, Entry> entries;
    QJsonArray list;
    QSet<QString> names;
    std::function<bool(QString, QString, int)> visit = [&](QString path, QString name, int depth) {
        QFileInfo f(path);
        if (depth > 64 || list.size() >= 10000 || !safePath(name) || names.contains(name.toCaseFolded())
            || !safeLocal(path) || (!f.isFile() && !f.isDir()))
            return false;
        names.insert(name.toCaseFolded());
        QString id = randomId();
        bool directory = f.isDir();
        const QString version = directory ? QString() : stamp(path);
        if (!directory && version.isEmpty())
            return false;
        entries.insert(id, { path, version, directory ? 0 : f.size(), directory });
        list.append(QJsonObject {
            { "id", id }, { "path", name }, { "directory", directory }, { "size", directory ? 0 : f.size() } });
        if (directory) {
            const auto children = QDir(path).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
            for (const auto& child : children)
                if (!visit(child.absoluteFilePath(), name + '/' + child.fileName(), depth + 1))
                    return false;
        }
        return true;
    };
    if (paths.isEmpty())
        return error("No files selected");
    for (const auto& path : paths) {
#ifdef Q_OS_WIN
        if (!safeLocal(QFileInfo(path).absoluteFilePath()))
            return error("Selection contains a reparse point or an unavailable file");
#endif
        if (QFileInfo(path).isSymLink() || !visit(QFileInfo(path).canonicalFilePath(), QFileInfo(path).fileName(), 0))
            return error("Selection contains an unavailable file, link, conflicting name or too many entries");
    }
    QJsonObject offer { { "ok", true }, { "id", randomId() }, { "entries", list } };
    if (!validManifest(offer))
        return error("Selection is too large or has unsupported names");
    m_Entries = std::move(entries);
    m_Manifest = offer;
    return offer;
}
QJsonObject NativeOfferStore::manifest() const
{
    QMutexLocker lock(&m_Mutex);
    return m_Manifest.isEmpty() ? error("No active selection") : m_Manifest;
}
void NativeOfferStore::clear()
{
    QMutexLocker lock(&m_Mutex);
    m_Entries.clear();
    m_Manifest = {};
}
QJsonObject NativeOfferStore::read(const QString& offer, const QString& file, qint64 offset, int length)
{
    QMutexLocker lock(&m_Mutex);
    if (offer != m_Manifest["id"].toString() || !m_Entries.contains(file))
        return error("Selection expired");
    const auto e = m_Entries.value(file);
    if (e.directory || offset < 0 || offset > e.size || length < 0 || length > 256 * 1024)
        return error("Invalid read range");
    if (!safeLocal(e.path) || stamp(e.path) != e.version)
        return error("Source changed or became unavailable");
    QFile input;
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(e.path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return error("Cannot open source");
    BY_HANDLE_FILE_INFORMATION info {};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(handle);
        return error("Source is a reparse point");
    }
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        return error("Cannot open source");
    }
#else
    int fd = ::open(QFile::encodeName(e.path).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return error("Cannot open source");
#endif
    if (!input.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
#ifdef Q_OS_WIN
        _close(fd);
#else
        ::close(fd);
#endif
        return error("Cannot open source");
    }
    if (stamp(e.path, fd) != e.version || stamp(e.path) != e.version || !input.seek(offset))
        return error("Cannot read source");
    const auto bytes = input.read(qMin(qint64(length), e.size - offset));
    if (bytes.size() != qMin(qint64(length), e.size - offset) || stamp(e.path, fd) != e.version
        || stamp(e.path) != e.version || !safeLocal(e.path))
        return error("Source changed during read");
    m_BytesRead += bytes.size();
    return { { "ok", true }, { "data", QString::fromLatin1(bytes.toBase64()) } };
}
