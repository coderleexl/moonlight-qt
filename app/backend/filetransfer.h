#ifndef DESK_FILETRANSFER_H
#define DESK_FILETRANSFER_H
#include <QObject>
#include <QVariantList>
#include <QSslCertificate>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <functional>
#include <memory>
#include "nativefiles/nativeplatform.h"
#include "nativefiles/activity.h"

// Lives in the transfer window's own process: the streaming SDL loop never pumps Qt.
class FileTransfer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localPath READ localPath NOTIFY localChanged)
    Q_PROPERTY(QString remotePath READ remotePath NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteRoot READ remoteRoot NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteDisplayPath READ remoteDisplayPath NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteParentPath READ remoteParentPath NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteDefaultPath READ remoteDefaultPath NOTIFY remoteChanged)
    Q_PROPERTY(bool remoteFilesystem READ remoteFilesystem NOTIFY remoteChanged)
    Q_PROPERTY(bool remoteLoading READ remoteLoading NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList localPlaces READ localPlaces NOTIFY localChanged)
    Q_PROPERTY(QVariantList remotePlaces READ remotePlaces NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList localEntries READ localEntries NOTIFY localChanged)
    Q_PROPERTY(QVariantList remoteEntries READ remoteEntries NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY remoteChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(QString conflict READ conflict NOTIFY conflictChanged)
    Q_PROPERTY(int limitMiB MEMBER m_LimitMiB NOTIFY limitChanged)
    Q_PROPERTY(QVariantList folderEntries READ folderEntries NOTIFY folderChanged)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY folderChanged)
    Q_PROPERTY(QString folderDisplayPath READ folderDisplayPath NOTIFY folderChanged)
    Q_PROPERTY(QString folderParentPath READ folderParentPath NOTIFY folderChanged)
    Q_PROPERTY(QString folderError READ folderError NOTIFY folderChanged)
    Q_PROPERTY(bool folderRemote READ folderRemote NOTIFY folderChanged)
    Q_PROPERTY(bool folderLoading READ folderLoading NOTIFY folderChanged)
    Q_PROPERTY(bool folderValid READ folderValid NOTIFY folderChanged)
public:
    ~FileTransfer() override;
    FileTransfer(const QString& host, quint16 port, const QSslCertificate& certificate, QObject* parent = nullptr);
    QString localPath() const { return m_LocalPath; }
    QString remotePath() const { return m_RemotePath; }
    QString remoteRoot() const { return m_RemoteRoot; }
    QString remoteDisplayPath() const { return displayRemotePath(m_RemotePath); }
    QString remoteParentPath() const { return m_RemoteParentPath; }
    QString remoteDefaultPath() const { return m_RemoteDefaultPath; }
    bool remoteFilesystem() const { return m_RemoteFilesystem; }
    bool remoteLoading() const { return m_RemoteLoading; }
    QVariantList localPlaces() const;
    QVariantList remotePlaces() const { return m_RemotePlaces; }
    QVariantList folderEntries() const { return m_FolderEntries; }
    QString folderPath() const { return m_FolderPath; }
    QString folderDisplayPath() const { return m_FolderRemote ? displayRemotePath(m_FolderPath) : m_FolderPath; }
    QString folderParentPath() const { return m_FolderParentPath; }
    QString folderError() const { return m_FolderError; }
    bool folderRemote() const { return m_FolderRemote; }
    bool folderLoading() const { return m_FolderLoading; }
    bool folderValid() const { return m_FolderValid; }
    QVariantList localEntries() const { return m_LocalEntries; }
    QVariantList remoteEntries() const { return m_RemoteEntries; }
    QVariantList jobs() const;
    QString error() const { return m_Error; }
    bool ready() const { return m_Ready; }
    bool busy() const;
    QString conflict() const { return m_Conflict; }
    Q_INVOKABLE void browseLocal(const QString& path);
    Q_INVOKABLE void browseRemote(const QString& path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void parentDirectory(bool remote);
    Q_INVOKABLE void home();
    Q_INVOKABLE void enqueue(bool upload, const QVariantList& names);
    Q_INVOKABLE void enqueueTo(bool upload, const QVariantList& names,
        const QString& sourceDirectory, const QString& destinationDirectory);
    Q_INVOKABLE void uploadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void uploadFilesTo(const QList<QUrl>& urls, const QString& destinationDirectory);
    Q_INVOKABLE QString displayRemotePath(const QString& path) const;
    Q_INVOKABLE void openFolderPicker(bool remote, const QString& path);
    Q_INVOKABLE void browseFolderPicker(const QString& path);
    Q_INVOKABLE void createPickerFolder(const QString& name);
    Q_INVOKABLE void pasteFiles();
    Q_INVOKABLE void systemDrag(bool remote, const QVariantList& names, QObject* window);

    Q_INVOKABLE void cancel(int index);
    Q_INVOKABLE void retry(int index);
    Q_INVOKABLE void resolveConflict(const QString& choice);
    Q_INVOKABLE void createFolder(bool remote, const QString& name);
    Q_INVOKABLE void clearFinished();
    static bool validName(const QString& name);
    static bool launch(const QString& host, quint16 port, const QSslCertificate& certificate, const QString& name, bool dark);
    static int runWindow(const QStringList& args);
signals:
    void localChanged();
    void remoteChanged();
    void jobsChanged();
    void errorChanged();
    void conflictChanged();
    void limitChanged();
    void folderChanged();
private:
    struct Job {
        bool upload = true, directory = false, cancelled = false, committing = false;
        int depth = 0, suffix = 0;
        QString source, destination, originalDestination, name, state = QStringLiteral("queued"), detail;
        QString sourceVersion, destinationVersion, token;
        qint64 size = 0, done = 0;
        QElapsedTimer timer;
        std::unique_ptr<QFile> input;
        std::unique_ptr<QSaveFile> output;
        QCryptographicHash hash{QCryptographicHash::Sha256};
    };
    using JobPtr = std::shared_ptr<Job>;
    using Callback = std::function<void(QJsonObject)>;
    void request(QJsonObject object, Callback callback);
    void perform(JobPtr job, QJsonObject object, Callback callback);
    void fail(JobPtr job, const QString& message);
    void complete(JobPtr job, const QString& state = QStringLiteral("done"));
    void startNext();
    void inspectSource(JobPtr job);
    void inspectDestination(JobPtr job, bool keepBoth = false);
    void beginCopy(JobPtr job);
    void chunk(JobPtr job);
    void expandDirectory(JobPtr job);
    void addJob(bool upload, const QString& source, const QString& destination, int depth = 0);
    void setError(const QString& message);
    void saveHistory();
    void initializeRemote();
    QString normalizeRemotePath(const QString& path) const;
    QString remoteParent(const QString& path) const;
    static QVariantList readLocalDirectory(const QString& path, QString& canonical, QString& error, bool foldersOnly = false);
    static QVariantList remoteDirectoryEntries(const QJsonObject& result, bool foldersOnly = false);
    static QString localVersion(const QString& path);
    static QString join(const QString& path, const QString& name);
    std::unique_ptr<NativeFilePlatform> m_NativePlatform;
    std::unique_ptr<NativeActivity> m_NativeActivity;
    QVariantList m_NativeJobs;
    bool m_NativePending = false;
    void refreshNativeJobs();
    QUrl m_Url;
    QSslCertificate m_Certificate;
    QString m_LocalPath, m_RemotePath, m_RemoteRoot, m_Error, m_Conflict, m_HistoryKey;
    QVariantList m_LocalEntries, m_RemoteEntries;
    QVariantList m_RemotePlaces, m_FolderEntries;
    QString m_RemoteParentPath, m_RemoteDefaultPath, m_RequestedRemotePath;
    QString m_FolderPath, m_FolderParentPath, m_FolderError;
    bool m_RemoteFilesystem = false, m_RemoteLoading = true, m_CapabilitiesResolved = false;
    bool m_FolderRemote = false, m_FolderLoading = false, m_FolderValid = false;
    int m_FolderGeneration = 0, m_PendingEnqueues = 0;
    QList<JobPtr> m_Jobs;
    JobPtr m_Active;
    bool m_Ready = false;
    int m_LimitMiB = 4, m_BrowseGeneration = 0;
};

#endif // DESK_FILETRANSFER_H
