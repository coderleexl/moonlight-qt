#pragma once

#include <QObject>
#include <QProcess>
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

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openConfiguration();
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void openScreenRecordingSettings();
    Q_INVOKABLE void openAccessibilitySettings();

signals:
    void stateChanged();
    void localOnlyChanged();
    void logChanged();

private:
    QString executablePath() const;
    int basePort() const;
    bool prepareConfiguration();
    void setState(State state, const QString& error = QString());
    void readOutput();

    QProcess m_Process;
    QTcpSocket m_Probe;
    QTimer m_HealthTimer;
    QTimer m_StartTimeout;
    QTimer m_StopTimeout;
    State m_State = Stopped;
    bool m_LocalOnly = true;
    QString m_Error;
    QString m_Log;
};
