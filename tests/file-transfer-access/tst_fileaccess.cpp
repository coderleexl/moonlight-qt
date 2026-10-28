#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include "../../third_party/sunshine/src/desk_files.h"

namespace {
using desk_files::json;
namespace fs = std::filesystem;

/** @brief Resolve test fixtures through macOS's /var alias before testing strict absolute paths. */
fs::path nativePath(const QTemporaryDir& directory)
{
    return fs::u8path(QFileInfo(directory.path()).canonicalFilePath().toStdString());
}

/** @brief Read bytes written by the protocol without involving a remote machine. */
QByteArray readFile(const fs::path& path)
{
    QFile file(QString::fromStdString(desk_files::path_utf8(path)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/** @brief Write fixture data only within a test-owned temporary directory. */
bool writeFile(const fs::path& path, const QByteArray& bytes)
{
    QFile file(QString::fromStdString(desk_files::path_utf8(path)));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

/** @brief Encode the complete expected digest in the existing upload protocol format. */
std::string digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toBase64().toStdString();
}

/** @brief Find directories created by this service beside an upload destination. */
QStringList staging(const fs::path& directory)
{
    return QDir(QString::fromStdString(desk_files::path_utf8(directory)))
        .entryList({".desk-transfers-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
}
}

class FileAccessTests : public QObject {
    Q_OBJECT
private slots:
    void explicitCapabilitiesAndLegacyBoundary()
    {
        QTemporaryDir shared, outside;
        QVERIFY(shared.isValid() && outside.isValid());
        desk_files::Service restricted(nativePath(shared));
        const auto disabled = restricted.execute({{"op", "capabilities"}});
        QVERIFY(disabled["ok"].get<bool>());
        QCOMPARE(disabled["protocol"].get<int>(), 2);
        QVERIFY(!disabled["filesystem"].get<bool>());
        QVERIFY(disabled["roots"].empty());
        QCOMPARE(disabled["defaultPath"].get<std::string>(), std::string());
        QVERIFY(!restricted.execute({{"op", "list"}, {"scope", "computer"},
            {"path", desk_files::wire_path(nativePath(outside))}})["ok"].get<bool>());

        desk_files::Service computer(nativePath(shared), true);
        const auto enabled = computer.execute({{"op", "capabilities"}});
        QVERIFY(enabled["filesystem"].get<bool>());
        QCOMPARE(enabled["defaultPath"].get<std::string>(), desk_files::wire_path(nativePath(shared)));
        QVERIFY(!enabled["roots"].empty());
#ifdef Q_OS_WIN
        QCOMPARE(enabled["platform"].get<std::string>(), std::string("windows"));
        for (const auto& root : enabled["roots"])
            QVERIFY(QString::fromStdString(root["path"].get<std::string>()).contains(QRegularExpression("^[A-Z]:/$")));
#else
        QCOMPARE(enabled["platform"].get<std::string>(), std::string("unix"));
        QCOMPARE(enabled["roots"][0]["path"].get<std::string>(), std::string("/"));
#endif
        // Enabling the capability must not broaden legacy relative requests.
        QVERIFY(!computer.execute({{"op", "list"}, {"path", desk_files::wire_path(nativePath(outside))}})["ok"].get<bool>());
        QVERIFY(!computer.execute({{"op", "list"}, {"scope", "unexpected"}, {"path", ""}})["ok"].get<bool>());
        QVERIFY(computer.execute({{"op", "list"}, {"path", ""}})["ok"].get<bool>());
    }

    void absoluteBrowsingMetadataAndParents()
    {
        QTemporaryDir shared, outside;
        const auto directory = nativePath(outside);
        QVERIFY(writeFile(directory / fs::u8path("报告.txt"), "sample"));
        fs::create_directory(directory / "nested");
        desk_files::Service service(nativePath(shared), true);
        const auto listed = service.execute({{"op", "list"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory) + "/"}});
        QVERIFY2(listed["ok"].get<bool>(), listed.dump().c_str());
        QCOMPARE(listed["path"].get<std::string>(), desk_files::wire_path(directory));
        QCOMPARE(listed["displayPath"].get<std::string>(), desk_files::wire_path(directory));
        QCOMPARE(listed["parentPath"].get<std::string>(), desk_files::wire_path(directory.parent_path()));
        QCOMPARE(listed["entries"].size(), size_t(2));
        for (const auto& entry : listed["entries"])
            QVERIFY(std::abs(entry["mtime"].get<qint64>() - QDateTime::currentMSecsSinceEpoch()) < 60000);
        const auto root = service.execute({{"op", "list"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory.root_path())}});
        QVERIFY2(root["ok"].get<bool>(), root.dump().c_str());
        QVERIFY(root["parentPath"].get<std::string>().empty());
        // A disk root can be used for browsing without creating a cache there.
        desk_files::Service rootService(directory.root_path(), true);
        QVERIFY(rootService.execute({{"op", "capabilities"}})["filesystem"].get<bool>());
        const auto created = service.execute({{"op", "mkdir"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory / "created")}});
        QVERIFY(created["ok"].get<bool>());
        QVERIFY(fs::is_directory(directory / "created"));
        const auto stat = service.execute({{"op", "stat"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory / fs::u8path("报告.txt"))}});
        QCOMPARE(stat["size"].get<int>(), 6);
        QVERIFY(!stat["dir"].get<bool>());
        const auto read = service.execute({{"op", "read"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory / fs::u8path("报告.txt"))},
            {"version", stat["version"]}, {"offset", 0}});
        QCOMPARE(desk_files::decode(read["data"].get<std::string>()), std::string("sample"));
    }

    void traversalAndLinksRemainRejected()
    {
        QTemporaryDir shared, outside;
        desk_files::Service service(nativePath(shared), true);
        const auto directory = nativePath(outside);
        for (const auto& path : {std::string("relative"), desk_files::wire_path(directory) + "/../escape",
                 desk_files::wire_path(directory) + "/./file", desk_files::wire_path(directory) + "/.DESK-TRANSFERS/a",
                 desk_files::wire_path(directory) + "/.desk-transfers-secret/a", std::string("//server/share"),
                 std::string("\\\\?\\C:\\Windows"), desk_files::wire_path(directory) + std::string("/a\0b", 4)}) {
            const auto result = service.execute({{"op", "stat"}, {"scope", "computer"}, {"path", path}});
            QVERIFY2(!result["ok"].get<bool>(), path.c_str());
        }
#ifdef Q_OS_WIN
        for (const auto& path : {"C:relative", "C:/NUL.txt", "C:/CONIN$", "C:/COM¹.log", "C:/LPT²", "C:/NUL .txt", "C:/file:stream", "C:/bad?name"})
            QVERIFY(!service.execute({{"op", "stat"}, {"scope", "computer"}, {"path", path}})["ok"].get<bool>());
#endif
        std::error_code ec;
        fs::create_directory_symlink(nativePath(shared), directory / "linked", ec);
        if (!ec) {
            QVERIFY(!service.execute({{"op", "list"}, {"scope", "computer"},
                {"path", desk_files::wire_path(directory / "linked")}})["ok"].get<bool>());
            const auto listed = service.execute({{"op", "list"}, {"scope", "computer"},
                {"path", desk_files::wire_path(directory)}});
            QVERIFY(listed["ok"].get<bool>());
            QVERIFY(listed["entries"].empty());
        }
    }

    void destinationLocalStagingAndAtomicCommit()
    {
        QTemporaryDir shared, outside;
        const auto directory = nativePath(outside);
        const auto destination = directory / "received.bin";
        const QByteArray bytes(600000, 'x');
        desk_files::Service service(nativePath(shared), true);
        const auto started = service.execute({{"op", "begin"}, {"scope", "computer"},
            {"path", desk_files::wire_path(destination)}, {"size", bytes.size()}, {"expected", "missing"}});
        QVERIFY2(started["ok"].get<bool>(), started.dump().c_str());
        QCOMPARE(staging(directory).size(), 1);
        QVERIFY(!fs::exists(nativePath(shared) / ".desk-transfers"));
        QVERIFY(!fs::exists(destination));
        const auto listed = service.execute({{"op", "list"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory)}});
        QVERIFY(listed["entries"].empty());
        for (int offset = 0; offset < bytes.size(); offset += int(desk_files::chunk_size)) {
            const auto chunk = bytes.mid(offset, int(desk_files::chunk_size));
            QVERIFY(service.execute({{"op", "write"}, {"token", started["token"]}, {"offset", offset},
                {"data", chunk.toBase64().toStdString()}})["ok"].get<bool>());
            QVERIFY(!fs::exists(destination));
        }
        QVERIFY(service.execute({{"op", "finish"}, {"token", started["token"]},
            {"sha256", digest(bytes)}})["ok"].get<bool>());
        QCOMPARE(readFile(destination), bytes);
        QVERIFY(staging(directory).isEmpty());
    }

    void cancelChecksumAndConcurrentEditsPreserveUserFiles()
    {
        QTemporaryDir shared, outside;
        const auto directory = nativePath(outside);
        const auto destination = directory / "keep.txt";
        QVERIFY(writeFile(destination, "original"));
        fs::create_directory(directory / ".desk-transfers-existing");
        QVERIFY(writeFile(directory / ".desk-transfers-existing" / "keep", "not an upload"));
        desk_files::Service service(nativePath(shared), true);
        auto start = [&] {
            return service.execute({{"op", "begin"}, {"scope", "computer"},
                {"path", desk_files::wire_path(destination)}, {"size", 3}, {"expected", desk_files::version(destination)}});
        };
        auto upload = start();
        QVERIFY(upload["ok"].get<bool>());
        QVERIFY(service.execute({{"op", "cancel"}, {"token", upload["token"]}})["ok"].get<bool>());
        QCOMPARE(readFile(destination), QByteArray("original"));
        upload = start();
        QVERIFY(service.execute({{"op", "write"}, {"token", upload["token"]}, {"offset", 0}, {"data", "YWJj"}})["ok"].get<bool>());
        QVERIFY(!service.execute({{"op", "finish"}, {"token", upload["token"]}, {"sha256", "wrong"}})["ok"].get<bool>());
        QCOMPARE(readFile(destination), QByteArray("original"));
        upload = start();
        QVERIFY(service.execute({{"op", "write"}, {"token", upload["token"]}, {"offset", 0}, {"data", "YWJj"}})["ok"].get<bool>());
        QVERIFY(writeFile(destination, "new user contents"));
        const auto conflict = service.execute({{"op", "finish"}, {"token", upload["token"]}, {"sha256", digest("abc")}});
        QVERIFY(!conflict["ok"].get<bool>());
        QCOMPARE(conflict["code"].get<std::string>(), std::string("conflict"));
        QCOMPARE(readFile(destination), QByteArray("new user contents"));
        QCOMPARE(staging(directory), QStringList{".desk-transfers-existing"});
        QCOMPARE(readFile(directory / ".desk-transfers-existing" / "keep"), QByteArray("not an upload"));
        upload = start();
        QVERIFY(service.execute({{"op", "write"}, {"token", upload["token"]}, {"offset", 0}, {"data", "YWJj"}})["ok"].get<bool>());
        QVERIFY(service.execute({{"op", "finish"}, {"token", upload["token"]}, {"sha256", digest("abc")}})["ok"].get<bool>());
        QCOMPARE(readFile(destination), QByteArray("abc"));
    }

    void existingStagingNamesAreNeverSwept()
    {
        QTemporaryDir shared;
        const auto directory = nativePath(shared) / ".desk-transfers";
        fs::create_directory(directory);
        const auto existing = directory / (std::string(64, 'a') + ".part");
        QVERIFY(writeFile(existing, "preexisting contents"));
        {
            desk_files::Service service(nativePath(shared), true);
            const auto upload = service.execute({{"op", "begin"}, {"path", "empty"}, {"size", 0}, {"expected", "missing"}});
            QVERIFY(upload["ok"].get<bool>());
            // Service destruction only cancels the upload it created itself.
        }
        QCOMPARE(readFile(existing), QByteArray("preexisting contents"));
        QCOMPARE(std::distance(fs::directory_iterator(directory), fs::directory_iterator()), std::ptrdiff_t(1));
    }

#ifndef Q_OS_WIN
    void destinationPermissionsAreReportedWithoutChangingFiles()
    {
        QTemporaryDir shared, outside;
        const auto directory = nativePath(outside) / "readonly";
        fs::create_directory(directory);
        desk_files::Service service(nativePath(shared), true);
        fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec);
        const auto started = service.execute({{"op", "begin"}, {"scope", "computer"},
            {"path", desk_files::wire_path(directory / "blocked")}, {"size", 0}, {"expected", "missing"}});
        fs::permissions(directory, fs::perms::owner_all);
        if (started["ok"].get<bool>()) {
            service.execute({{"op", "cancel"}, {"token", started["token"]}});
            QSKIP("Current account bypasses filesystem write permissions");
        }
        QVERIFY(!fs::exists(directory / "blocked"));
        QVERIFY(fs::is_empty(directory));
    }
#endif
};

QTEST_APPLESS_MAIN(FileAccessTests)
#include "tst_fileaccess.moc"
