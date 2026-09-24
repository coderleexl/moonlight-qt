#ifndef DESK_NATIVE_PROCESSUTILS_H
#define DESK_NATIVE_PROCESSUTILS_H
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif
inline bool nativeProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    auto handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle)
        return false;
    bool alive = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0;
#endif
}
#endif
