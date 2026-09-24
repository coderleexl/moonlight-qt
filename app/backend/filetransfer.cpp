#include "filetransfer.h"
#include "identitymanager.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTimer>
#include <QStandardPaths>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSettings>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <QDrag>
#include <QMimeData>
#include <QClipboard>
#include <QUuid>
#include "nativefiles/jsontransport.h"
#include "nativefiles/offerstore.h"
#include "nativefiles/processutils.h"

FileTransfer::FileTransfer(const QString& host, quint16 port, const QSslCertificate& certificate, QObject* parent)
    : QObject(parent), m_Certificate(certificate)
{
    m_Url.setScheme("https"); m_Url.setHost(host); m_Url.setPort(port); m_Url.setPath("/desk/files");
    m_Network.setProxy(QNetworkProxy::NoProxy);
    m_HistoryKey = "fileTransfer/history/" + QString::fromLatin1(QCryptographicHash::hash(m_Url.toEncoded() + certificate.toDer(), QCryptographicHash::Sha256).toHex());
    const auto history = QSettings().value(m_HistoryKey).toList();
    for (const auto& entry : history) {
        const auto saved = entry.toMap();
        if (saved["state"] != "done" && saved["state"] != "failed" && saved["state"] != "cancelled" && saved["state"] != "skipped") continue;
        auto job = std::make_shared<Job>();
        job->source = saved["source"].toString(); job->destination = saved["destination"].toString();
        job->originalDestination = job->destination; job->name = saved["name"].toString();
        job->upload = saved["upload"].toBool(); job->state = saved["state"].toString(); job->detail = saved["detail"].toString();
        job->size = saved["size"].toLongLong(); job->done = saved["done"].toLongLong();
        m_Jobs.append(job);
        if (m_Jobs.size() >= 100) break;
    }
    auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    browseLocal(QDir(downloads).exists() ? downloads : QDir::homePath());
    QTimer::singleShot(0, this, [this] { browseRemote(""); });
    auto nativeTimer=new QTimer(this); nativeTimer->setInterval(500);
    connect(nativeTimer,&QTimer::timeout,this,&FileTransfer::refreshNativeJobs);nativeTimer->start();
}

FileTransfer::~FileTransfer() = default;
void FileTransfer::uploadFiles(const QList<QUrl>& urls)
{
    if (!m_Ready) return;
    for (const auto& url : urls) {
        QFileInfo source(url.toLocalFile());
        if (!url.isLocalFile() || !source.exists() || source.isSymLink() || !validName(source.fileName())) {
            setError(tr("Only local files and folders can be uploaded.")); continue;
        }
        addJob(true, source.absoluteFilePath(), join(m_RemotePath, source.fileName()));
    }
    emit jobsChanged(); startNext();
}
void FileTransfer::pasteFiles()
{
    uploadFiles(QGuiApplication::clipboard()->mimeData()->urls());
}
void FileTransfer::systemDrag(bool remote, const QVariantList& names, QObject* object)
{
    auto window = qobject_cast<QWindow*>(object);
    if (!window || names.isEmpty()) return;
    if (!remote) {
        QDrag drag(window); auto mime = new QMimeData(); QList<QUrl> urls;
        for (const auto& value : names) if (validName(value.toString())) urls.append(QUrl::fromLocalFile(join(m_LocalPath, value.toString())));
        mime->setUrls(urls); drag.setMimeData(mime); drag.exec(Qt::CopyAction); return;
    }
    if (!m_Ready || (m_NativePlatform&&m_NativePlatform->pendingDrag())) return;
    if (!m_NativePlatform) {
        m_NativePlatform = NativeFilePlatform::create();
        connect(m_NativePlatform.get(), &NativeFilePlatform::error, this, &FileTransfer::setError);
    }
    if (!m_NativePlatform->supported()) { setError(tr("System file dragging is unavailable in this desktop session.")); return; }
    NativeTransport transport; transport.url=m_Url; transport.peer=m_Certificate; transport.identity=IdentityManager::get()->getSslConfig();
    QJsonArray entries; QHash<QString, QJsonObject> sources;
    std::function<bool(QString,QString,int)> visit = [&](QString path,QString name,int depth) {
        if (depth>64 || entries.size()>=10000 || !NativeOfferStore::safePath(name)) return false;
        auto stat=transport.call({{"op","stat"},{"path",path}});
        if (!stat["ok"].toBool() || !stat["exists"].toBool()) return false;
        auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        entries.append(QJsonObject{{"id",id},{"path",name},{"directory",stat["dir"]},{"size",stat["size"]}});
        sources.insert(id,{{"path",path},{"version",stat["version"]}});
        if (stat["dir"].toBool()) {
            auto list=transport.call({{"op","list"},{"path",path}}); if (!list["ok"].toBool()) return false;
            for (auto child:list["entries"].toArray()) {auto n=child.toObject()["name"].toString();if (!validName(n)||!visit(join(path,n),join(name,n),depth+1)) return false;}
        }
        return true;
    };
    for (const auto& value:names) if (!validName(value.toString()) || !visit(join(m_RemotePath,value.toString()),value.toString(),0)) {setError(tr("Cannot prepare selected files for dragging."));return;}
    QJsonObject offer{{"ok",true},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"entries",entries}};
    if (!NativeOfferStore::validManifest(offer)) {setError(tr("Too many files or unsupported file names."));return;}
    if(!m_NativeActivity)m_NativeActivity=std::make_unique<NativeActivity>(NativeActivity::peerKey(m_Url,m_Certificate.toDer()));
    m_NativePlatform->drag(offer,m_NativeActivity->track(offer,[transport,sources](const QString& file,qint64 offset,int length) {
        if (!sources.contains(file)) return QJsonObject{{"ok",false},{"error","Unknown selected file"}};
        auto source=sources[file];auto result=transport.call({{"op","read"},{"path",source["path"]},{"version",source["version"]},{"offset",offset}});
        if (result["ok"].toBool()) {
            auto encoded=result["data"].toString().toLatin1();auto bytes=QByteArray::fromBase64(encoded);
            if (bytes.toBase64()!=encoded || bytes.size()>256*1024) return QJsonObject{{"ok",false},{"error","Invalid file data"}};
            result["data"]=QString::fromLatin1(bytes.left(length).toBase64());
        }
        return result;
    },false),window);
}
void FileTransfer::refreshNativeJobs()
{
    QVariantList jobs; const auto key=NativeActivity::peerKey(m_Url,m_Certificate.toDer());
    const auto files=QDir(NativeActivity::directory()).entryInfoList({key+"-*.json"},QDir::Files,QDir::Time);
    for (const auto& info:files) {
        if (jobs.size()>=100) break;
        QFile file(info.absoluteFilePath()); if (!file.open(QIODevice::ReadOnly) || file.size()>512*1024) continue;
        auto snapshot=QJsonDocument::fromJson(file.readAll()).object();
        for (const auto& value:snapshot["jobs"].toArray()) {
            auto job=value.toObject().toVariantMap();
            if (job["state"]=="running" && !nativeProcessAlive(snapshot["pid"].toVariant().toLongLong())) {job["state"]="failed";job["detail"]=tr("The transfer process ended. Copy or drag the files again to retry.");}
            job["nativePath"]=info.absoluteFilePath();job["session"]=snapshot["session"].toString();jobs.append(job);
        }
    }
    const bool pending=m_NativePlatform&&m_NativePlatform->pendingDrag();
    if (jobs!=m_NativeJobs||pending!=m_NativePending) {m_NativePending=pending;m_NativeJobs=jobs;emit jobsChanged();}
}

bool FileTransfer::validName(const QString& name)
{
    if (name.isEmpty() || name == "." || name == ".." || name == ".desk-transfers" || name.endsWith('.') || name.endsWith(' ')) return false;
    if (QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"), QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) return false;
    return !name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f/\\\\:*?\"<>|]")));
}
QString FileTransfer::join(const QString& path, const QString& name) { return path.isEmpty() ? name : path + "/" + name; }
QString FileTransfer::localVersion(const QString& path)
{
    QFileInfo info(path);
    return info.exists() ? QString::number(info.size()) + ":" + QString::number(info.lastModified().toMSecsSinceEpoch()) : "missing";
}
void FileTransfer::setError(const QString& message) { m_Error = message; emit errorChanged(); }
void FileTransfer::request(QJsonObject object, Callback callback)
{
    if (m_Certificate.isNull() || m_Url.host().isEmpty() || m_Url.port() <= 0) {
        QTimer::singleShot(0, this, [callback] { callback({{"ok", false}, {"error", tr("A paired host is required.")}}); });
        return;
    }
    QNetworkRequest request(m_Url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Connection", "close");
    auto ssl = IdentityManager::get()->getSslConfig();
    ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
    request.setSslConfiguration(ssl);
    auto reply = m_Network.post(request, QJsonDocument(object).toJson(QJsonDocument::Compact));
    reply->setReadBufferSize(1024 * 1024);
    auto buffer = std::make_shared<QByteArray>();
    auto verified = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        if (reply->sslConfiguration().peerCertificate() != m_Certificate) return;
        for (const auto& e : errors) if (e.certificate() != m_Certificate) return;
        reply->ignoreSslErrors(errors);
    });
    connect(reply, &QNetworkReply::encrypted, this, [this, reply, verified] {
        *verified = reply->sslConfiguration().peerCertificate() == m_Certificate;
        if (!*verified) reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer] {
        buffer->append(reply->readAll());
        if (buffer->size() > 1024 * 1024) reply->abort();
    });
    QTimer::singleShot(30000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, verified, callback] {
        buffer->append(reply->readAll());
        QJsonObject result;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
            result = {{"ok", false}, {"error", tr("The remote host does not support file transfer. Update Desk on both computers and restart hosting.")}};
        } else if (reply->error() != QNetworkReply::NoError || reply->sslConfiguration().peerCertificate() != m_Certificate || buffer->size() > 1024 * 1024) {
            result = {{"ok", false}, {"error", tr("Connection failed: %1").arg(reply->errorString())}};
        } else {
            result = QJsonDocument::fromJson(*buffer).object();
            if (!result.contains("ok")) result = {{"ok", false}, {"error", tr("The remote host does not support file transfer. Update Desk on both computers and restart hosting.")}};
        }
        reply->deleteLater(); callback(result);
    });
}
void FileTransfer::perform(JobPtr job, QJsonObject object, Callback callback)
{
    request(object, [this, job, callback](QJsonObject result) {
        if (job->cancelled) {
            auto token = result.value("token").toString();
            if (!token.isEmpty()) request({{"op", "cancel"}, {"token", token}}, [](QJsonObject) {});
            return;
        }
        if (!result.value("ok").toBool()) { fail(job, result.value("error").toString(tr("Transfer failed."))); return; }
        callback(result);
    });
}
void FileTransfer::browseLocal(const QString& path)
{
    auto actual = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    QDir directory(actual);
    if (!directory.exists() || !QFileInfo(actual).isReadable()) { setError(tr("Cannot open this directory.")); return; }
    QVariantList entries;
    const auto infos = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    if (infos.size() > 10000) { setError(tr("This directory has too many entries.")); return; }
    for (const auto& f : infos) if (f.isDir() || f.isFile()) entries.append(QVariantMap{{"name", f.fileName()}, {"dir", f.isDir()}, {"size", f.size()}});
    m_LocalPath = QFileInfo(directory.absolutePath()).canonicalFilePath(); m_LocalEntries = entries; emit localChanged();
}
void FileTransfer::browseRemote(const QString& path)
{
    const int generation = ++m_BrowseGeneration;
    request({{"op", "list"}, {"path", path}}, [this, path, generation](QJsonObject result) {
        if (generation != m_BrowseGeneration) return;
        if (!result.value("ok").toBool()) { setError(result.value("error").toString()); return; }
        m_RemotePath = path; m_RemoteRoot = result.value("root").toString();
        m_RemoteEntries.clear();
        for (const auto& value : result.value("entries").toArray()) {
            auto e = value.toObject();
            if (validName(e.value("name").toString())) m_RemoteEntries.append(e.toVariantMap());
        }
        std::sort(m_RemoteEntries.begin(), m_RemoteEntries.end(), [](const QVariant& a, const QVariant& b) {
            auto x = a.toMap(), y = b.toMap();
            return x["dir"].toBool() != y["dir"].toBool() ? x["dir"].toBool() : QString::localeAwareCompare(x["name"].toString(), y["name"].toString()) < 0;
        });
        m_Ready = true; emit remoteChanged();
    });
}
void FileTransfer::refresh() { browseLocal(m_LocalPath); browseRemote(m_RemotePath); }
void FileTransfer::home() { browseLocal(QDir::homePath()); }
void FileTransfer::parentDirectory(bool remote)
{
    if (remote) { auto path = m_RemotePath; const int index = path.lastIndexOf('/'); browseRemote(index < 0 ? "" : path.left(index)); }
    else { QDir dir(m_LocalPath); dir.cdUp(); browseLocal(dir.absolutePath()); }
}
void FileTransfer::createFolder(bool remote, const QString& name)
{
    if (!validName(name)) { setError(tr("Invalid file name.")); return; }
    if (remote) request({{"op", "mkdir"}, {"path", join(m_RemotePath, name)}}, [this](QJsonObject r) {
        if (!r.value("ok").toBool()) setError(r.value("error").toString()); else browseRemote(m_RemotePath);
    });
    else if (!QDir(m_LocalPath).mkdir(name)) setError(tr("Cannot create directory."));
    else browseLocal(m_LocalPath);
}
void FileTransfer::addJob(bool upload, const QString& source, const QString& destination, int depth)
{
    if (m_Jobs.size() >= 10000 || depth > 64) { setError(tr("Too many files or directory levels. Transfer a smaller selection.")); return; }
    auto job = std::make_shared<Job>(); job->upload = upload; job->source = source; job->destination = destination;
    job->originalDestination = destination; job->name = source.section('/', -1); job->depth = depth;
    m_Jobs.append(job);
}
void FileTransfer::enqueue(bool upload, const QVariantList& names)
{
    if (!m_Ready) return;
    for (const auto& name : names) {
        if (!validName(name.toString())) { setError(tr("Invalid file name.")); continue; }
        addJob(upload, join(upload ? m_LocalPath : m_RemotePath, name.toString()), join(upload ? m_RemotePath : m_LocalPath, name.toString()));
    }
    emit jobsChanged(); startNext();
}
QVariantList FileTransfer::jobs() const
{
    QVariantList result;
    for (const auto& j : m_Jobs) {
        const double speed = j->timer.isValid() && j->timer.elapsed() > 0 ? double(j->done) * 1000.0 / j->timer.elapsed() : 0;
        result.append(QVariantMap{{"name", j->name}, {"upload", j->upload}, {"state", j->state}, {"detail", j->detail}, {"done", j->done}, {"size", j->size}, {"speed", speed}, {"committing", j->committing}});
    }
    result.append(m_NativeJobs);
    return result;
}
bool FileTransfer::busy() const
{
    if(m_NativePlatform&&m_NativePlatform->pendingDrag())return true;
    for (const auto& j : m_Jobs) if (j->state == "queued" || j->state == "running" || j->state == "conflict") return true;
    for (const auto& value:m_NativeJobs) if (value.toMap()["state"]=="running") return true;
    return false;
}
void FileTransfer::startNext()
{
    if (m_Active) return;
    for (const auto& j : m_Jobs) if (j->state == "queued") {
        m_Active = j; j->state = "running"; j->timer.start(); emit jobsChanged(); inspectSource(j); return;
    }
    refresh();
}
void FileTransfer::saveHistory()
{
    QVariantList history;
    for (const auto& job : m_Jobs) {
        if (job->state == "queued" || job->state == "running" || job->state == "conflict") continue;
        history.append(QVariantMap{{"source", job->source}, {"destination", job->destination}, {"name", job->name},
                                   {"upload", job->upload}, {"state", job->state}, {"detail", job->detail}, {"size", job->size}, {"done", job->done}});
    }
    while (history.size() > 100) history.removeFirst();
    QSettings().setValue(m_HistoryKey, history);
}
void FileTransfer::complete(JobPtr job, const QString& state)
{
    job->state = state; job->input.reset(); job->output.reset();
    if (m_Active == job) { m_Active.reset(); m_Conflict.clear(); emit conflictChanged(); }
    saveHistory();
    emit jobsChanged(); QTimer::singleShot(0, this, [this] { startNext(); });
}
void FileTransfer::fail(JobPtr job, const QString& message)
{
    job->detail = message;
    if (!job->token.isEmpty()) { request({{"op", "cancel"}, {"token", job->token}}, [](QJsonObject) {}); job->token.clear(); }
    complete(job, "failed");
}
void FileTransfer::cancel(int index)
{
    if (index>=m_Jobs.size() && index<m_Jobs.size()+m_NativeJobs.size()) {
        auto job=m_NativeJobs[index-m_Jobs.size()].toMap();QSaveFile file(job["nativePath"].toString()+".cancel");
        if (file.open(QIODevice::WriteOnly)) {file.write(QJsonDocument(QJsonObject{{"session",job["session"].toString()},{"id",job["id"].toString()}}).toJson());file.commit();}
        return;
    }
    if (index < 0 || index >= m_Jobs.size()) return;
    auto job = m_Jobs[index];
    if (job->committing || (job->state != "queued" && job->state != "running" && job->state != "conflict")) return;
    job->cancelled = true;
    if (!job->token.isEmpty()) request({{"op", "cancel"}, {"token", job->token}}, [](QJsonObject) {});
    complete(job, "cancelled");
}
void FileTransfer::retry(int index)
{
    if (index < 0 || index >= m_Jobs.size()) return;
    auto job = m_Jobs[index];
    if (job->state != "failed" && job->state != "cancelled") return;
    addJob(job->upload, job->source, job->originalDestination, job->depth); emit jobsChanged(); startNext();
}
void FileTransfer::clearFinished()
{
    QSet<QString> activePaths;
    for (const auto& value:m_NativeJobs) {auto job=value.toMap();if (job["state"]=="running") activePaths.insert(job["nativePath"].toString());}
    for (const auto& value:m_NativeJobs) {auto path=value.toMap()["nativePath"].toString();if (!activePaths.contains(path)) QFile::remove(path);}
    refreshNativeJobs();
    for (auto it = m_Jobs.begin(); it != m_Jobs.end();) {
        if ((*it)->state != "queued" && (*it)->state != "running" && (*it)->state != "conflict") it = m_Jobs.erase(it); else ++it;
    }
    saveHistory();
    emit jobsChanged();
}
void FileTransfer::inspectSource(JobPtr job)
{
    if (job->upload) {
        QFileInfo info(job->source);
        if (!info.exists() || info.isSymLink() || (!info.isFile() && !info.isDir())) { fail(job, tr("Source is missing or is a symbolic link.")); return; }
        job->directory = info.isDir(); job->size = info.size(); job->sourceVersion = localVersion(job->source);
        if (job->directory) expandDirectory(job); else inspectDestination(job);
    } else perform(job, {{"op", "stat"}, {"path", job->source}}, [this, job](QJsonObject r) {
        if (!r.value("exists").toBool()) { fail(job, tr("Source is missing or is a symbolic link.")); return; }
        job->directory = r.value("dir").toBool(); job->size = qint64(r.value("size").toDouble()); job->sourceVersion = r.value("version").toString();
        if (job->directory) expandDirectory(job); else inspectDestination(job);
    });
}
void FileTransfer::expandDirectory(JobPtr job)
{
    if (job->depth >= 64) { fail(job, tr("Too many files or directory levels. Transfer a smaller selection.")); return; }
    if (job->upload) {
        perform(job, {{"op", "mkdir"}, {"path", job->destination}}, [this, job](QJsonObject) {
            const auto children = QDir(job->source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::DirsFirst | QDir::Name);
            if (children.size() + m_Jobs.size() > 10000) { fail(job, tr("Too many files or directory levels. Transfer a smaller selection.")); return; }
            for (const auto& child : children) if (validName(child.fileName()) && (child.isFile() || child.isDir())) addJob(true, child.absoluteFilePath(), join(job->destination, child.fileName()), job->depth + 1);
            complete(job);
        });
    } else {
        QFileInfo target(job->destination);
        if (target.isSymLink() || (target.exists() && !target.isDir()) || (!target.exists() && !QDir().mkdir(job->destination))) { fail(job, tr("Cannot create directory.")); return; }
        perform(job, {{"op", "list"}, {"path", job->source}}, [this, job](QJsonObject r) {
            auto entries = r.value("entries").toArray();
            if (entries.size() + m_Jobs.size() > 10000) { fail(job, tr("Too many files or directory levels. Transfer a smaller selection.")); return; }
            for (const auto& e : entries) {
                auto name = e.toObject().value("name").toString();
                if (!validName(name)) { fail(job, tr("Invalid file name.")); return; }
                addJob(false, join(job->source, name), join(job->destination, name), job->depth + 1);
            }
            complete(job);
        });
    }
}
void FileTransfer::inspectDestination(JobPtr job, bool keepBoth)
{
    auto checked = [this, job, keepBoth](QJsonObject r) {
        if (r.value("dir").toBool()) { fail(job, tr("A directory already uses this file name.")); return; }
        job->destinationVersion = r.value("version").toString();
        if (r.value("exists").toBool()) {
            if (keepBoth) {
                if (++job->suffix > 1000) { fail(job, tr("Cannot find an unused file name.")); return; }
                auto p = job->originalDestination; auto slash = p.lastIndexOf('/'); auto dot = p.lastIndexOf('.'); if (dot <= slash + 1) dot = p.size();
                job->destination = p.left(dot) + QString(" (%1)").arg(job->suffix) + p.mid(dot); inspectDestination(job, true); return;
            }
            job->state = "conflict"; m_Conflict = job->name; emit jobsChanged(); emit conflictChanged(); return;
        }
        beginCopy(job);
    };
    if (job->upload) perform(job, {{"op", "stat"}, {"path", job->destination}}, checked);
    else {
        QFileInfo info(job->destination);
        if (info.isSymLink()) { fail(job, tr("Destination is a symbolic link.")); return; }
        checked({{"exists", info.exists()}, {"dir", info.isDir()}, {"version", localVersion(job->destination)}});
    }
}
void FileTransfer::resolveConflict(const QString& choice)
{
    if (!m_Active || m_Active->state != "conflict") return;
    auto job = m_Active; m_Conflict.clear(); emit conflictChanged(); job->state = "running"; emit jobsChanged();
    if (choice == "overwrite") beginCopy(job);
    else if (choice == "keep") inspectDestination(job, true);
    else complete(job, "skipped");
}
void FileTransfer::beginCopy(JobPtr job)
{
    if (job->upload) {
        job->input = std::make_unique<QFile>(job->source);
        if (!job->input->open(QIODevice::ReadOnly) || localVersion(job->source) != job->sourceVersion) { fail(job, tr("Cannot read file or source changed.")); return; }
        perform(job, {{"op", "begin"}, {"path", job->destination}, {"size", job->size}, {"expected", job->destinationVersion}}, [this, job](QJsonObject r) {
            job->token = r.value("token").toString(); chunk(job);
        });
    } else {
        job->output = std::make_unique<QSaveFile>(job->destination);
        job->output->setDirectWriteFallback(false);
        if (!job->output->open(QIODevice::WriteOnly)) { fail(job, job->output->errorString()); return; }
        chunk(job);
    }
}
void FileTransfer::chunk(JobPtr job)
{
    if (job->cancelled) return;
    if (job->done == job->size) {
        job->committing = true; emit jobsChanged();
        if (job->upload) {
            if (localVersion(job->source) != job->sourceVersion) { fail(job, tr("Source changed; retry the transfer.")); return; }
            perform(job, {{"op", "finish"}, {"token", job->token}, {"sha256", QString::fromLatin1(job->hash.result().toBase64())}}, [this, job](QJsonObject) { job->token.clear(); complete(job); });
        } else {
            if (QFileInfo(job->destination).isSymLink() || localVersion(job->destination) != job->destinationVersion) { fail(job, tr("Destination changed; retry the transfer.")); return; }
            if (!job->output->commit()) { fail(job, job->output->errorString()); return; }
            complete(job);
        }
        return;
    }
    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
    auto advance = [this, job, elapsed](qint64 bytes) {
        job->done += bytes; emit jobsChanged();
        const int wait = m_LimitMiB > 0 ? qMax(0, int(bytes * 1000 / (qint64(m_LimitMiB) * 1024 * 1024) - elapsed->elapsed())) : 0;
        QTimer::singleShot(wait, this, [this, job] { chunk(job); });
    };
    if (job->upload) {
        auto bytes = job->input->read(qMin(qint64(256 * 1024), job->size - job->done));
        if (bytes.isEmpty()) { fail(job, tr("Cannot read file or source changed.")); return; }
        job->hash.addData(bytes);
        perform(job, {{"op", "write"}, {"token", job->token}, {"offset", job->done}, {"data", QString::fromLatin1(bytes.toBase64())}}, [this, job, advance, bytes](QJsonObject r) {
            if (qint64(r.value("offset").toDouble()) != job->done + bytes.size()) { fail(job, tr("Invalid transfer response.")); return; }
            advance(bytes.size());
        });
    } else perform(job, {{"op", "read"}, {"path", job->source}, {"offset", job->done}, {"version", job->sourceVersion}}, [this, job, advance](QJsonObject r) {
        auto encoded = r.value("data").toString().toLatin1(); auto bytes = QByteArray::fromBase64(encoded);
        if (bytes.toBase64() != encoded || bytes.isEmpty() || bytes.size() > 256 * 1024 || bytes.size() > job->size - job->done) { fail(job, tr("Invalid transfer response.")); return; }
        if (job->output->write(bytes) != bytes.size()) { fail(job, job->output->errorString()); return; }
        advance(bytes.size());
    });
}

bool FileTransfer::launch(const QString& host, quint16 port, const QSslCertificate& certificate, const QString& name, bool dark)
{
    // Only the public pinned certificate is passed; the private identity stays in QSettings.
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--desk-files", host, QString::number(port), QString::fromLatin1(certificate.toDer().toBase64()), name, dark ? "dark" : "light"});
}
int FileTransfer::runWindow(const QStringList& args)
{
    if (args.size() != 7) return 2;
    bool valid = false; const auto port = args[3].toUShort(&valid);
    const QSslCertificate certificate(QByteArray::fromBase64(args[4].toLatin1()), QSsl::Der);
    if (!valid || !port || certificate.isNull()) return 2;
    const auto id = QCryptographicHash::hash((QSettings().fileName() + args[2] + args[3] + args[4]).toUtf8() + IdentityManager::get()->getCertificate(), QCryptographicHash::Sha256).toHex().left(32);
    const auto socketName = QStringLiteral("desk-files-") + QString::fromLatin1(id);
    QLockFile lock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(socketName + ".lock"));
    if (!lock.tryLock(0)) {
        QLocalSocket socket; socket.connectToServer(socketName);
        if (socket.waitForConnected(2000)) { socket.write("show"); socket.waitForBytesWritten(1000); return 0; }
        return 3;
    }
    QLocalServer::removeServer(socketName);
    QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketName)) return 3;
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/res/desk.svg")));
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    QQuickStyle::setStyle("Material");
    FileTransfer transfer(args[2], port, certificate);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("transfer", &transfer);
    engine.rootContext()->setContextProperty("hostName", args[5]);
    engine.rootContext()->setContextProperty("transferDark", args[6] == "dark");
    engine.load(QUrl(QStringLiteral("qrc:/gui/FileTransferWindow.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return 1;
    auto show = [window] { window->showNormal(); window->raise(); window->requestActivate(); };
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, show] {
        while (auto socket = server.nextPendingConnection()) { show(); socket->disconnectFromServer(); socket->deleteLater(); }
    });
    QObject::connect(&transfer, &FileTransfer::conflictChanged, window, [&transfer, show] { if (!transfer.conflict().isEmpty()) show(); });
    QObject::connect(&transfer, &FileTransfer::jobsChanged, window, [&transfer, window] {
        if (!transfer.busy() && !window->isVisible()) QCoreApplication::quit();
    });
    show();
    return QCoreApplication::exec();
}
