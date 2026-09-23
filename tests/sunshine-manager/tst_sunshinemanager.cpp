#include "sunshinemanager.h"
#include "deskaccess.h"
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

static int testWebPort()
{
    SunshineManager manager;
    return QUrl("http://" + manager.localAddress()).port() + 1;
}

// This same binary acts as a controlled helper in the test bundle. It verifies
// process ownership and configuration; it does not emulate or test streaming.
static int fakeHost(QCoreApplication& app)
{
    const auto args = app.arguments().mid(1);
    QFile config(args.first());
    if (!config.open(QIODevice::ReadOnly)) return 20;
    const auto content = config.readAll();
    QFile record(QFileInfo(config).dir().filePath("test-arguments.json"));
    if (!record.open(QIODevice::WriteOnly)) return 21;
    record.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson());
    record.close();
    QFile access(QFileInfo(config).dir().filePath("desk-access.json"));
    if (!access.open(QIODevice::ReadOnly)) return 23;
    const auto credentials = QJsonDocument::fromJson(access.readAll()).object();
    const auto password = qEnvironmentVariable("DESK_ACCESS_PASSWORD");
    if (!DeskAccess::validPassword(password) || password != credentials.value("password").toString()) return 24;
    if (qEnvironmentVariable("DESK_DEVICE_ID") != credentials.value("deviceId").toString()) return 25;
    if (args.join(' ').contains(password)) return 26;
    if (content.contains("test_exit")) return 42;
    QTcpServer listener;
    listener.setProxy(QNetworkProxy::NoProxy);
    if (!listener.listen(QHostAddress::LocalHost, testWebPort())) return 22;
    return app.exec();
}

class ManagerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_Data;
    QString m_Helper;
    QString m_ConfigDir;
    void writeConfig(const QByteArray& content)
    {
        QDir().mkpath(m_ConfigDir);
        QFile file(m_ConfigDir + "/sunshine.conf");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(content), content.size());
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN) && !defined(Q_OS_LINUX)
        QSKIP("Bundled hosting is not supported on this platform");
#endif
        QVERIFY(m_Data.isValid());
        QStandardPaths::setTestModeEnabled(true);
        // Unique app name keeps configuration isolated from real Moonlight.
        QCoreApplication::setApplicationName("MoonlightHostTest-" + QFileInfo(m_Data.path()).fileName());
        SunshineManager manager;
        m_ConfigDir = manager.configDirectory();
#ifdef Q_OS_WIN
        m_Helper = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("host/sunshine/sunshine.exe");
#elif defined(Q_OS_LINUX)
        m_Helper = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../lib/desk/host/sunshine/sunshine");
#else
        m_Helper = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../Helpers/Sunshine.app/Contents/MacOS/Sunshine");
#endif
        QVERIFY(!QFile::exists(m_Helper));
        QDir().mkpath(QFileInfo(m_Helper).absolutePath());
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), m_Helper));
    }
    void cleanup()
    {
        QVERIFY(QDir(m_ConfigDir).removeRecursively() || !QDir(m_ConfigDir).exists());
    }
    void cleanupTestCase()
    {
        if (!m_Helper.isEmpty()) QFile::remove(m_Helper);
    }
    void missingHelper()
    {
        QVERIFY(QFile::rename(m_Helper, m_Helper + ".test-hidden"));
        SunshineManager manager;
        QVERIFY(!manager.installed());
        manager.start();
        QCOMPARE(manager.state(), SunshineManager::Failed);
        QVERIFY(!manager.error().isEmpty());
        QVERIFY(QFile::rename(m_Helper + ".test-hidden", m_Helper));
    }
    void occupiedPort()
    {
        QTcpServer other;
        other.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(other.listen(QHostAddress::LocalHost, testWebPort()));
        SunshineManager manager;
        manager.start();
        QCOMPARE(manager.state(), SunshineManager::Failed);
        QVERIFY(manager.error().contains(QString::number(testWebPort())));
        QVERIFY(other.isListening());
        QVERIFY(!QFile::exists(m_ConfigDir + "/sunshine.conf"));
    }
    void startStopPreservesConfig()
    {
        const QByteArray original = "# preserve user configuration\nsunshine_name = My Mac\n";
        writeConfig(original);
        SunshineManager manager;
        QVERIFY(!manager.localOnly());
        manager.setLocalOnly(true);
        manager.start();
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Running, 5000);
        manager.setLocalOnly(false);
        QVERIFY(manager.localOnly());
        QVERIFY(!manager.resetAccess());
        QVERIFY(!manager.setAccessPassword("654321"));
        QFile record(m_ConfigDir + "/test-arguments.json");
        QVERIFY(record.open(QIODevice::ReadOnly));
        const auto args = QJsonDocument::fromJson(record.readAll()).array();
        QVERIFY(args.contains("bind_address=127.0.0.1"));
        QVERIFY(args.contains("upnp=disabled"));
        QVERIFY(args.contains("file_apps=" + m_ConfigDir + "/apps.json"));
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Stopped, 7000);
        QFile config(m_ConfigDir + "/sunshine.conf");
        QVERIFY(config.open(QIODevice::ReadOnly));
        QCOMPARE(config.readAll(), original);
        manager.setLocalOnly(false);
        QVERIFY(!manager.localOnly());
        manager.start();
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Running, 5000);
        record.close();
        QVERIFY(record.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(record.readAll()).array().contains("bind_address=0.0.0.0"));
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Stopped, 7000);
    }
    void accessPersistsAndResetRevokes()
    {
        SunshineManager first;
        QVERIFY(DeskAccess::validId(first.deviceId()));
        QVERIFY(DeskAccess::validPassword(first.accessPassword()));
        QVERIFY(QRegularExpression("^[0-9]{6}$").match(first.accessPassword()).hasMatch());
        const auto id = first.deviceId();
        const auto password = first.accessPassword();
        SunshineManager second;
        QCOMPARE(second.deviceId(), id);
        QCOMPARE(second.accessPassword(), password);
#ifndef Q_OS_WIN
        const auto permissions = QFile(m_ConfigDir + "/desk-access.json").permissions();
        QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
#endif
        QFile state(m_ConfigDir + "/sunshine_state.json");
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write(R"({"username":"admin","password":"hash","root":{"uniqueid":"keep-host-uuid","named_devices":[{"cert":"old"}],"devices":[{"certs":["legacy"]}]}})");
        state.close();
        QVERIFY(second.resetAccess());
        QCOMPARE(second.deviceId(), id);
        QVERIFY(second.accessPassword() != password);
        QVERIFY(QRegularExpression("^[0-9]{6}$").match(second.accessPassword()).hasMatch());
        QVERIFY(state.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(state.readAll()).object();
        QCOMPARE(object.value("username").toString(), QString("admin"));
        QCOMPARE(object.value("password").toString(), QString("hash"));
        const auto root = object.value("root").toObject();
        QCOMPARE(root.value("uniqueid").toString(), QString("keep-host-uuid"));
        QVERIFY(root.value("named_devices").toArray().isEmpty());
        QVERIFY(!root.contains("devices"));
        SunshineManager third;
        QCOMPARE(third.accessPassword(), second.accessPassword());
    }
    void customPasswordPersistsAndRevokes()
    {
        SunshineManager manager;
        const auto id = manager.deviceId();
        const auto original = manager.accessPassword();
        for (const auto& invalid : {QString(), QString("12345"), QString("123456\n"),
                                    QString("abc def"), QString(65, 'a'), QString::fromUtf8("密码123456")}) {
            QVERIFY(!manager.setAccessPassword(invalid));
            QCOMPARE(manager.accessPassword(), original);
        }
        QFile state(m_ConfigDir + "/sunshine_state.json");
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write(R"({"root":{"uniqueid":"keep","named_devices":[{"cert":"old"}]}})");
        state.close();
        QVERIFY(manager.setAccessPassword("Desk-Office!42"));
        QCOMPARE(manager.deviceId(), id);
        SunshineManager loaded;
        QCOMPARE(loaded.accessPassword(), QString("Desk-Office!42"));
        QVERIFY(state.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(state.readAll()).object().value("root").toObject();
        QCOMPARE(root.value("uniqueid").toString(), QString("keep"));
        QVERIFY(root.value("named_devices").toArray().isEmpty());
        // Windows cannot atomically replace a file held open by this reader.
        // Release the test's handle before asking the manager to write it again.
        state.close();
        QVERIFY(loaded.setAccessPassword("012345"));
        QCOMPARE(loaded.accessPassword(), QString("012345"));
        QVERIFY(loaded.setAccessPassword(QString(64, '!')));
    }
    void legacyPasswordStillLoads()
    {
        SunshineManager manager;
        QVERIFY(manager.setAccessPassword("AbCdEfGhIjKlMnOpQrSt_-"));
        SunshineManager loaded;
        QCOMPARE(loaded.accessPassword(), QString("AbCdEfGhIjKlMnOpQrSt_-"));
    }
    void corruptAccessFailsClosed()
    {
        SunshineManager first;
        QFile access(m_ConfigDir + "/desk-access.json");
        QVERIFY(access.open(QIODevice::WriteOnly | QIODevice::Truncate));
        access.write("broken");
        access.close();
        SunshineManager manager;
        QCOMPARE(manager.state(), SunshineManager::Failed);
        manager.start();
        QCOMPARE(manager.state(), SunshineManager::Failed);
        QVERIFY(manager.accessPassword().isEmpty());
    }
    void helperExits()
    {
        writeConfig("# test_exit\n");
        SunshineManager manager;
        manager.start();
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Failed, 5000);
        QVERIFY(manager.error().contains("42"));
    }
    void destructorStopsOwnedProcess()
    {
        {
            SunshineManager manager;
            manager.start();
            QTRY_COMPARE_WITH_TIMEOUT(manager.state(), SunshineManager::Running, 5000);
            QFile config(m_ConfigDir + "/sunshine.conf");
            QVERIFY(config.open(QIODevice::ReadOnly));
            QVERIFY(config.readAll().contains("upnp = disabled"));
#ifndef Q_OS_WIN
            QVERIFY(!(config.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
#endif
        }
        QTcpServer listener;
        listener.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(listener.listen(QHostAddress::LocalHost, testWebPort()));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (QFileInfo(app.applicationFilePath()).completeBaseName().compare("Sunshine", Qt::CaseInsensitive) == 0) return fakeHost(app);
    ManagerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_sunshinemanager.moc"
