#ifndef DESK_NATIVEAGENT_H
#define DESK_NATIVEAGENT_H
#include <QProcess>
#include <QSslCertificate>
#include <QStringList>
class NativeAgent {
public:
    static int run(const QStringList& args);
    static bool startHost(QProcess& process, QProcessEnvironment& sunshineEnvironment);
    static void startClient(QProcess& process, const QString& host, quint16 port, const QSslCertificate& certificate);
    static void stop(QProcess& process);
};
#endif
