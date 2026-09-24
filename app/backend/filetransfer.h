#ifndef DESK_FILETRANSFER_H
#define DESK_FILETRANSFER_H
#include <QObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QSslCertificate>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <functional>
#include <memory>

// Lives in the transfer window's own process: the streaming SDL loop never pumps Qt.
class FileTransfer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localPath READ localPath NOTIFY localChanged)
    Q_PROPERTY(QString remotePath READ remotePath NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteRoot READ remoteRoot NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList localEntries READ localEntries NOTIFY localChanged)
    Q_PROPERTY(QVariantList remoteEntries READ remoteEntries NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY remoteChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(QString conflict READ conflict NOTIFY conflictChanged)
    Q_PROPERTY(int limitMiB MEMBER m_LimitMiB NOTIFY limitChanged)
public:
    FileTransfer(const QString& host, quint16 port, const QSslCertificate& certificate, QObject* parent = nullptr);
    QString localPath() const { return m_LocalPath; }
    QString remotePath() const { return m_RemotePath; }
    QString remoteRoot() const { return m_RemoteRoot; }
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
    static QString localVersion(const QString& path);
    static QString join(const QString& path, const QString& name);
    QNetworkAccessManager m_Network;
    QUrl m_Url;
    QSslCertificate m_Certificate;
    QString m_LocalPath, m_RemotePath, m_RemoteRoot, m_Error, m_Conflict, m_HistoryKey;
    QVariantList m_LocalEntries, m_RemoteEntries;
    QList<JobPtr> m_Jobs;
    JobPtr m_Active;
    bool m_Ready = false;
    int m_LimitMiB = 4, m_BrowseGeneration = 0;
};

#endif // DESK_FILETRANSFER_H
