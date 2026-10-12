// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINTESTFILESYSTEM_H
#define PLUGINTESTFILESYSTEM_H
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace PluginTest {
// QFile::link creates .lnk shortcuts on Windows. Boundary tests require a
// real filesystem symlink at the exact requested file or directory path.
inline bool link(const QString &source,const QString &destination) {
#ifdef Q_OS_WIN
    const auto from=QDir::toNativeSeparators(QFileInfo(source).absoluteFilePath()).toStdWString();
    const auto to=QDir::toNativeSeparators(QFileInfo(destination).absoluteFilePath()).toStdWString();
    const DWORD flags=QFileInfo(source).isDir()?SYMBOLIC_LINK_FLAG_DIRECTORY:0;
    return CreateSymbolicLinkW(to.c_str(),from.c_str(),flags|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
        || CreateSymbolicLinkW(to.c_str(),from.c_str(),flags);
#else
    return QFile::link(source,destination);
#endif
}
}
#endif
