// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProcessIdentity.h"
#include <QFile>
#include <QRegularExpression>
#include <cmath>
#include <limits>
#ifdef Q_OS_MACOS
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <cerrno>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif
namespace PluginPackages {
namespace {
using State=ProcessIdentity::State;
State query(qint64 pid,ProcessIdentity *result) {
    result->pid=pid;
#ifdef Q_OS_MACOS
    result->platform="macos";
    int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PID,int(pid)};kinfo_proc info{};size_t size=sizeof(info);
    if(::sysctl(mib,4,&info,&size,nullptr,0)<0)return errno==ESRCH||errno==ENOENT?State::Exited:State::Unknown;
    if(!size||info.kp_proc.p_stat==SZOMB)return State::Exited;
    if(size!=sizeof(info)||info.kp_proc.p_pid!=pid||info.kp_proc.p_starttime.tv_sec<=0)return State::Unknown;
    result->start=QString::number(info.kp_proc.p_starttime.tv_sec)+':'+QString::number(info.kp_proc.p_starttime.tv_usec);
    return State::Alive;
#elif defined(Q_OS_LINUX)
    result->platform="linux";
    QFile stat(QString("/proc/%1/stat").arg(pid));
    if(!stat.open(QIODevice::ReadOnly))return stat.exists()?State::Unknown:State::Exited;
    const auto bytes=stat.read(4097);const auto end=bytes.lastIndexOf(')');
    if(bytes.size()>4096||end<1)return State::Unknown;
    const auto fields=bytes.mid(end+2).simplified().split(' ');
    if(fields.size()<20)return State::Unknown;
    if(fields[0]=="Z"||fields[0]=="X")return State::Exited;
    QFile boot("/proc/sys/kernel/random/boot_id");if(!boot.open(QIODevice::ReadOnly))return State::Unknown;
    const auto bootId=QString::fromLatin1(boot.read(65)).trimmed();const auto ticks=QString::fromLatin1(fields[19]);
    static const QRegularExpression uuid("^[a-f0-9-]{36}$"),number("^[0-9]{1,20}$");
    if(!uuid.match(bootId).hasMatch()||!number.match(ticks).hasMatch())return State::Unknown;
    result->start=bootId+':'+ticks;return State::Alive;
#elif defined(Q_OS_WIN)
    result->platform="windows";
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,DWORD(pid));
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER?State::Exited:State::Unknown;
    const auto wait=WaitForSingleObject(process,0);
    if(wait!=WAIT_TIMEOUT){CloseHandle(process);return wait==WAIT_OBJECT_0?State::Exited:State::Unknown;}
    FILETIME created,exited,kernel,user;
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user)){CloseHandle(process);return State::Unknown;}
    CloseHandle(process);const quint64 stamp=(quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    if(!stamp)return State::Unknown;
    result->start=QString::number(stamp);return State::Alive;
#else
    Q_UNUSED(pid);return State::Unknown;
#endif
}
}
QJsonObject ProcessIdentity::json() const {return {{"pid",pid},{"platform",platform},{"start",start}};}
bool ProcessIdentity::parse(const QJsonObject &json,ProcessIdentity *result) {
    const auto pid=json["pid"].toDouble(-1);
    if(json.size()!=3||!json["pid"].isDouble()||!std::isfinite(pid)||pid!=std::floor(pid)||pid<1||pid>std::numeric_limits<int>::max()
        ||!json["platform"].isString()||!json["start"].isString())return false;
    const auto platform=json["platform"].toString(),start=json["start"].toString();
    static const QRegularExpression macStamp("^[0-9]{1,20}:[0-9]{1,6}$"),linuxStamp("^[a-f0-9-]{36}:[0-9]{1,20}$"),windowsStamp("^[0-9]{1,20}$");
    if(!((platform=="macos"&&macStamp.match(start).hasMatch())||(platform=="linux"&&linuxStamp.match(start).hasMatch())||(platform=="windows"&&windowsStamp.match(start).hasMatch())))return false;
    *result={qint64(pid),platform,start};return true;
}
bool ProcessIdentity::capture(qint64 pid,ProcessIdentity *result,QString *error) {
    if(pid<1||pid>std::numeric_limits<int>::max()||query(pid,result)!=State::Alive) {
        if(error)*error="Cannot capture the running plugin process identity.";return false;
    }
    return true;
}
ProcessIdentity::State ProcessIdentity::state() const {
    if(pid<1||pid>std::numeric_limits<int>::max())return State::Unknown;
    ProcessIdentity actual;const auto status=query(pid,&actual);
    if(actual.platform!=platform)return State::Unknown;
    if(status!=State::Alive)return status;
    return actual.start==start?State::Alive:State::Exited;
}
}
