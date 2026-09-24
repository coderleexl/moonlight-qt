#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

// Owns only the Sunshine helper shipped in this application bundle.
// The first integration keeps its lifecycle tied to Moonlight.
class SunshineManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool needsMacPermissions READ needsMacPermissions CONSTANT)
    Q_PROPERTY(QString localAddress READ localAddress CONSTANT)
    Q_PROPERTY(bool installed READ installed CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool localOnly READ localOnly WRITE setLocalOnly NOTIFY localOnlyChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QString configDirectory READ configDirectory CONSTANT)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY accessChanged)
    Q_PROPERTY(QString accessPassword READ accessPassword NOTIFY accessChanged)
    Q_PROPERTY(QString sharedDirectory READ sharedDirectory WRITE setSharedDirectory NOTIFY sharedDirectoryChanged)
    Q_PROPERTY(QStringList lanAddresses READ lanAddresses NOTIFY stateChanged)

public:
    enum State { Stopped, Starting, Running, Stopping, Failed };
    Q_ENUM(State)
    explicit SunshineManager(QObject* parent = nullptr);
    ~SunshineManager() override;

    bool supported() const;
    bool needsMacPermissions() const;
    QString localAddress() const;
    bool installed() const;
    State state() const { return m_State; }
    bool localOnly() const { return m_LocalOnly; }
    void setLocalOnly(bool localOnly);
    QString error() const { return m_Error; }
    QString logText() const { return m_Log; }
    QString configDirectory() const;
    QString deviceId() const { return m_DeviceId; }
    QString accessPassword() const { return m_AccessPassword; }
    QStringList lanAddresses() const;

    QString sharedDirectory() const;
    void setSharedDirectory(const QString& directory);
    Q_INVOKABLE void openSharedDirectory();

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openConfiguration();
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void openScreenRecordingSettings();
    Q_INVOKABLE void openAccessibilitySettings();
    Q_INVOKABLE bool resetAccess();
    Q_INVOKABLE bool setAccessPassword(const QString& password);

signals:
    void stateChanged();
    void localOnlyChanged();
    void logChanged();
    void accessChanged();
    void sharedDirectoryChanged();

private:
    QString executablePath() const;
    int basePort() const;
    bool prepareConfiguration();
    void setState(State state, const QString& error = QString());
    void readOutput();
    bool prepareAccess();
    bool saveAccess(const QString& deviceId, const QString& password);

    QProcess m_Process;
    QTcpSocket m_Probe;
    QTimer m_HealthTimer;
    QTimer m_StartTimeout;
    QTimer m_StopTimeout;
    State m_State = Stopped;
    bool m_LocalOnly = false;
    QString m_Error;
    QString m_Log;
    QString m_DeviceId;
    QString m_AccessPassword;
};
