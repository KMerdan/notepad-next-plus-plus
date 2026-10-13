// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageLease.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc,char **argv) {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout),_O_BINARY); // Readiness markers have a fixed wire spelling.
#endif
    QCoreApplication app(argc,argv);
    if(argc!=4)return 2;
    if(QString::fromLocal8Bit(argv[3])=="linger") {
        std::puts("ready");std::fflush(stdout);QTimer::singleShot(15000,&app,&QCoreApplication::quit);return app.exec();
    }
    QString error;auto lease=PluginPackages::PackageLease::acquire(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),&error);
    if(!lease){std::fprintf(stderr,"%s\n",qPrintable(error));return 3;}
    std::puts("ready");std::fflush(stdout);
    // Simulate an interrupted owner: retain its on-disk lock and record.
    if(QString::fromLocal8Bit(argv[3])=="crash")std::_Exit(0);
    char line[16];if(!std::fgets(line,sizeof(line),stdin))return 4;
    return 0;
}
