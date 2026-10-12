// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QLockFile>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc!=2)return 2;const auto root=QString::fromLocal8Bit(argv[1]);
    QLockFile lock(root+"/store.lock");lock.setStaleLockTime(0);if(!lock.tryLock(0))return 3;
    QSaveFile file(root+"/current.json");file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write("interrupted diagnostic fixture")<0||!file.flush())return 4;
    std::puts("ready");std::fflush(stdout);QTimer::singleShot(20000,&app,&QCoreApplication::quit);return app.exec();
}
