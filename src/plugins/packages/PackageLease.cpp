// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageLease.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QElapsedTimer>
#include <QThread>
namespace PluginPackages {
namespace { bool fail(QString *error,const QString &message){if(error)*error=message;return false;} }
PackageLease::PackageLease(QString store,QString directory,QString digest,std::unique_ptr<QLockFile> lock)
    :store(std::move(store)),directory(std::move(directory)),digest(std::move(digest)),lock(std::move(lock)) {}
bool PackageLease::protects(const QString &path,const QString &hash) const {return store==path&&digest==hash&&lock->isLocked();}
bool PackageLease::pinRuntimeProcess(qint64 pid,QString *error) {
    if(digest.isEmpty()||runtime||!lock->isLocked())return fail(error,"This lifetime lease cannot bind a runtime process.");
    ProcessIdentity identity;if(!ProcessIdentity::capture(pid,&identity,error))return false;
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"digest",digest},{"process",identity.json()}}).toJson(QJsonDocument::Compact);
    QSaveFile file(directory+"/metadata.json");file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail(error,"Cannot persist the plugin process lifetime lease.");
    runtime=identity;return true;
}
bool PackageLease::lockStore(QLockFile &lock,int waitMs,const std::atomic_bool *cancel) {
    QElapsedTimer deadline;deadline.start();const auto budget=qBound(0,waitMs,1000);
    for(;;) {
        if(cancel&&cancel->load())return false;
        if(lock.tryLock(0))return true;
        const auto remaining=qint64(budget)-deadline.elapsed();
        if(lock.error()!=QLockFile::LockFailedError||remaining<=0)return false;
        QThread::msleep(qMin<qint64>(10,remaining));
    }
}
std::shared_ptr<PackageLease> PackageLease::acquire(const QString &path,const QString &digest,QString *error,int waitMs,const std::atomic_bool *cancel) {
    if(QFileInfo(path).isSymLink()||!QDir().mkpath(path)){fail(error,"Cannot create a real package store for its lifetime lease.");return {};}
    const auto canonical=QFileInfo(path).canonicalFilePath();
    QLockFile storeLock(canonical+"/.store.lock");storeLock.setStaleLockTime(0);
    if(!lockStore(storeLock,waitMs,cancel)){fail(error,cancel&&cancel->load()?"Installation cancelled.":"The package store is busy. Retry the operation.");return {};}
    return acquireLocked(canonical,digest,error);
}
std::shared_ptr<PackageLease> PackageLease::acquireLocked(const QString &canonical,const QString &digest,QString *error) {
    static const QRegularExpression hash("^[a-f0-9]{64}$");
    if(!digest.isEmpty()&&(!hash.match(digest).hasMatch()||QFileInfo(canonical+'/'+digest).isSymLink()
        ||!QFileInfo(canonical+'/'+digest).isDir())){fail(error,"Cannot lease a missing or invalid package.");return {};}
    const auto leases=canonical+"/.leases";
    if(QFileInfo(leases).isSymLink()||!QDir().mkpath(leases)){fail(error,"Cannot create package lifetime leases.");return {};}
    const auto directory=leases+'/'+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkdir(directory)){fail(error,"Cannot create a package lifetime lease.");return {};}
    auto lock=std::make_unique<QLockFile>(directory+"/owner.lock");lock->setStaleLockTime(0);
    if(!lock->tryLock(0)){QDir(directory).removeRecursively();fail(error,"Cannot lock a package lifetime lease.");return {};}
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"digest",digest}}).toJson(QJsonDocument::Compact);
    QSaveFile file(directory+"/metadata.json");file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) {
        lock->unlock();QDir(directory).removeRecursively();fail(error,"Cannot save a package lifetime lease.");return {};
    }
    return std::shared_ptr<PackageLease>(new PackageLease(canonical,directory,digest,std::move(lock)));
}
PackageLease::~PackageLease() {
    lock->unlock();
    // A surviving helper remains a root even after its application owner
    // disappears. Its native creation stamp makes PID reuse unambiguous.
    if(runtime&&runtime->state()!=ProcessIdentity::State::Exited)return;
    // If a collector/preparation owns the store lock, leave this now-inactive
    // lease for its next pass. Never wait on the GUI thread during teardown.
    QLockFile storeLock(store+"/.store.lock");storeLock.setStaleLockTime(0);
    if(storeLock.tryLock(0))QDir(directory).removeRecursively();
}
}
