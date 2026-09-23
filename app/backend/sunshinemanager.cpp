#include "sunshinemanager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUrl>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

SunshineManager::SunshineManager(QObject* parent) : QObject(parent)
{
    m_Process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    m_Process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_Probe.setProxy(QNetworkProxy::NoProxy);
    m_HealthTimer.setInterval(500);
    m_StartTimeout.setSingleShot(true);
    m_StartTimeout.setInterval(60000);
    m_StopTimeout.setSingleShot(true);
    m_StopTimeout.setInterval(5000);
    connect(&m_Process, &QProcess::readyReadStandardOutput, this, &SunshineManager::readOutput);
    connect(&m_Process, &QProcess::started, this, [this] {
        m_HealthTimer.start();
        m_StartTimeout.start();
    });
    connect(&m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_HealthTimer.stop();
            m_StartTimeout.stop();
            setState(Failed, tr("Unable to start the bundled Sunshine: %1").arg(m_Process.errorString()));
        }
    });
    connect(&m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        readOutput();
        m_HealthTimer.stop();
        m_StartTimeout.stop();
        m_StopTimeout.stop();
        m_Probe.abort();
        if (m_State == Stopping) {
            setState(Stopped);
        } else if (m_State != Failed) {
            setState(Failed, tr("Sunshine exited (code %1). See the log for details.").arg(code));
        }
    });
    connect(&m_HealthTimer, &QTimer::timeout, this, [this] {
        if (m_Probe.state() == QAbstractSocket::UnconnectedState)
            m_Probe.connectToHost(QHostAddress::LocalHost, basePort() + 1);
    });
    connect(&m_Probe, &QTcpSocket::connected, this, [this] {
        m_Probe.abort();
        if (m_State == Starting && m_Process.state() == QProcess::Running) {
            m_StartTimeout.stop();
            m_HealthTimer.stop();
            setState(Running);
        }
    });
    connect(&m_StartTimeout, &QTimer::timeout, this, [this] {
        setState(Failed, tr("Sunshine did not become ready. Check permissions and the log, then retry."));
        m_HealthTimer.stop();
        m_Process.terminate();
        m_StopTimeout.start();
    });
    connect(&m_StopTimeout, &QTimer::timeout, this, [this] {
        if (m_Process.state() != QProcess::NotRunning)
            m_Process.kill();
    });
}

SunshineManager::~SunshineManager()
{
    if (m_Process.state() != QProcess::NotRunning) {
        m_Process.terminate();
        if (!m_Process.waitForFinished(3000)) {
            m_Process.kill();
            m_Process.waitForFinished(1000);
        }
    }
}

bool SunshineManager::supported() const
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool SunshineManager::needsMacPermissions() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

int SunshineManager::basePort() const
{
#ifdef MOONLIGHT_HOST_PREVIEW
    return 48989;
#else
    return 47989;
#endif
}

QString SunshineManager::localAddress() const
{
    return QString("127.0.0.1:%1").arg(basePort());
}

QString SunshineManager::executablePath() const
{
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("host/sunshine/sunshine.exe");
#elif defined(Q_OS_LINUX)
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../lib/desk/host/sunshine/sunshine");
#else
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        "../Helpers/Sunshine.app/Contents/MacOS/Sunshine");
#endif
}

bool SunshineManager::installed() const
{
    return supported() && QFileInfo(executablePath()).isExecutable();
}

QString SunshineManager::configDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/sunshine";
}

void SunshineManager::setLocalOnly(bool localOnly)
{
    if (m_Process.state() != QProcess::NotRunning || m_LocalOnly == localOnly)
        return;
    m_LocalOnly = localOnly;
    emit localOnlyChanged();
}

void SunshineManager::setState(State state, const QString& error)
{
    m_State = state;
    m_Error = error;
    emit stateChanged();
}

bool SunshineManager::prepareConfiguration()
{
    QDir directory(configDirectory());
    if (!directory.mkpath("credentials"))
        return false;
    QFile::setPermissions(directory.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString configPath = directory.filePath("sunshine.conf");
    if (QFileInfo::exists(configPath))
        return true;
    QSaveFile config(configPath);
    if (!config.open(QIODevice::WriteOnly))
        return false;
    config.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray defaults = "# Managed by Moonlight. Settings from the local web UI are preserved.\n"
                               "sunshine_name = Moonlight Host\n"
                               "upnp = disabled\n"
                               "origin_web_ui_allowed = pc\n";
    return config.write(defaults) == defaults.size() && config.commit();
}

void SunshineManager::start()
{
    if (m_Process.state() != QProcess::NotRunning)
        return;
    if (!installed()) {
        setState(Failed, tr("The bundled Sunshine is missing. Build the integrated app first."));
        return;
    }
    // Never attach to, replace, or terminate a separately installed Sunshine.
    QTcpServer portCheck;
    portCheck.setProxy(QNetworkProxy::NoProxy);
    if (!portCheck.listen(QHostAddress::LocalHost, basePort() + 1)) {
        setState(Failed, tr("Port %1 is already in use. Stop the other host service before starting the bundled one.").arg(basePort() + 1));
        return;
    }
    portCheck.close();
    if (!prepareConfiguration()) {
        setState(Failed, tr("Unable to create the Sunshine configuration directory."));
        return;
    }
    const QDir config(configDirectory());
    m_Log.clear();
    emit logChanged();
    // Windows resolves its assets relative to the executable directory.
    m_Process.setWorkingDirectory(QFileInfo(executablePath()).absolutePath());
    m_Process.setProgram(executablePath());
    m_Process.setArguments({config.filePath("sunshine.conf"),
                           QString("port=%1").arg(basePort()), "address_family=ipv4", "origin_web_ui_allowed=pc", "upnp=disabled",
                           "sunshine_name=" + QCoreApplication::applicationName() + " Host",
                           QString("bind_address=%1").arg(m_LocalOnly ? "127.0.0.1" : "0.0.0.0"),
                           "file_apps=" + config.filePath("apps.json"),
                           "file_state=" + config.filePath("sunshine_state.json"),
                           "credentials_file=" + config.filePath("sunshine_state.json"),
                           "pkey=" + config.filePath("credentials/key.pem"),
                           "cert=" + config.filePath("credentials/cert.pem"),
                           "log_path=" + config.filePath("sunshine.log")});
    setState(Starting);
    m_Process.start();
}

void SunshineManager::stop()
{
    if (m_Process.state() == QProcess::NotRunning) {
        setState(Stopped);
        return;
    }
    m_HealthTimer.stop();
    m_StartTimeout.stop();
    m_Probe.abort();
    setState(Stopping);
    m_Process.terminate();
    m_StopTimeout.start();
}

void SunshineManager::readOutput()
{
    m_Log += QString::fromUtf8(m_Process.readAllStandardOutput());
    if (m_Log.size() > 32768)
        m_Log = m_Log.right(32768);
    emit logChanged();
}

void SunshineManager::openConfiguration()
{
    if (m_State == Running)
        QDesktopServices::openUrl(QUrl(QString("https://127.0.0.1:%1").arg(basePort() + 1)));
}

void SunshineManager::openLogs()
{
    if (QFileInfo::exists(configDirectory()))
        QDesktopServices::openUrl(QUrl::fromLocalFile(configDirectory()));
}

void SunshineManager::openScreenRecordingSettings()
{
#ifdef Q_OS_MACOS
    QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"));
#endif
}

void SunshineManager::openAccessibilitySettings()
{
#ifdef Q_OS_MACOS
    QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"));
#endif
}
