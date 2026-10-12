// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageStore.h"
#include "InstalledPluginRegistry.h"
#include "PackageLease.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QUuid>
namespace PluginPackages {
namespace {
bool stopping(const std::atomic_bool *cancel){return cancel&&cancel->load();}
bool entries(const QString &path,QVector<QFileInfo> *result,int limit,QString *error) {
    QDirIterator iterator(path,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
    while(iterator.hasNext()) {
        iterator.next();result->append(iterator.fileInfo());
        if(result->size()>limit){*error="Package collection exceeded its entry budget. The directory has been preserved.";return false;}
    }
    return true;
}
// Preflight before renaming. Never follow symbolic links or recurse into an
// unknown/special filesystem entry. There is no plugin execution in this pass.
bool disposableTree(const QString &root,int depth,int *count,qint64 *bytes,QString *error,const std::atomic_bool *cancel) {
    if(stopping(cancel))return false;
    if(depth>33){*error="Package collection found an overdeep directory.";return false;}
    QVector<QFileInfo> children;
    if(!entries(root,&children,PackageStore::MaxFiles+2,error))return false;
    for(const auto &entry:children) {
        if(stopping(cancel))return false;
        if(++*count>PackageStore::MaxFiles+2||entry.isSymLink()||(!entry.isDir()&&!entry.isFile())) {
            *error="Package collection found an unsafe filesystem entry.";return false;
        }
        if(entry.isDir()) {if(!disposableTree(entry.absoluteFilePath(),depth+1,count,bytes,error,cancel))return false;}
        else {
            *bytes+=entry.size();
            if(entry.size()<0||*bytes>PackageStore::MaxPackageBytes+8*1024*1024){*error="Package collection exceeded its byte budget.";return false;}
        }
    }
    return true;
}
bool retire(const QString &store,const QFileInfo &entry,QString *error,const std::atomic_bool *cancel,QString *retired=nullptr) {
    if(entry.isSymLink()||!entry.isDir()||entry.canonicalFilePath()!=entry.absoluteFilePath()) {
        *error="Package collection found an unsafe directory.";return false;
    }
    int count=0;qint64 bytes=0;
    if(!disposableTree(entry.absoluteFilePath(),0,&count,&bytes,error,cancel))return false;
    if(stopping(cancel))return false;
    // A crash during deletion leaves a recognized trash directory, even if
    // its receipt has already been removed. The next pass can finish it.
    const auto trash=store+"/.trash-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().rename(entry.absoluteFilePath(),trash)){*error="Cannot retire an unused package directory.";return false;}
    if(retired){*retired=trash;return true;}
    if(!QDir(trash).removeRecursively()){*error="Cannot completely remove a retired package directory. Collection will retry.";return false;}
    return true;
}
bool ownedReceipt(const QFileInfo &directory) {
    QFileInfo info(directory.absoluteFilePath()+"/.nnp-receipt.json");QFile receipt(info.absoluteFilePath());
    if(info.isSymLink()||!info.isFile()||!receipt.open(QIODevice::ReadOnly)||receipt.size()>8*1024*1024)return false;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(receipt.read(8*1024*1024+1),&error);const auto object=document.object();
    return error.error==QJsonParseError::NoError&&document.isObject()&&object.size()==3&&object["schemaVersion"]==QJsonValue(1)
        &&object["digest"]==directory.fileName()&&object["files"].isArray()&&object["files"].toArray().size()<=PackageStore::MaxFiles;
}
void preserved(CollectionResult *result,const QString &name,const QString &reason) {
    if(result->preserved.size()<64)result->preserved.append(name+": "+reason);
}
}
CollectionResult PackageStore::collect(const QString &index,const std::atomic_bool *cancel) const {
    CollectionResult result;
    if(stopping(cancel)){result.cancelled=true;return result;}
    const QFileInfo root(path);
    if(!root.exists())return result;
    if(root.isSymLink()||!root.isDir()){result.error="Package store must be a real directory. Collection has been skipped.";return result;}
    const auto store=root.canonicalFilePath();
    QLockFile storeLock(store+"/.store.lock");storeLock.setStaleLockTime(0);
    if(!storeLock.tryLock(0)){result.deferred=true;return result;}
    QSet<QString> keep;
    static const QRegularExpression digest("^[a-f0-9]{64}$"),uuid("^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$");
    const auto leases=store+"/.leases";const QFileInfo leasesInfo(leases);bool blockAll=false;
    if(leasesInfo.isSymLink()||(leasesInfo.exists()&&!leasesInfo.isDir())){result.error="Invalid package lifetime lease directory. Collection has been skipped.";return result;}
    QVector<QFileInfo> leaseEntries;
    if(leasesInfo.exists()&&!entries(leases,&leaseEntries,4096,&result.error))return result;
    for(const auto &entry:leaseEntries) {
        if(stopping(cancel)){result.cancelled=true;return result;}
        if(entry.isSymLink()||!entry.isDir()||!uuid.match(entry.fileName()).hasMatch()||QFileInfo(entry.absoluteFilePath()+"/owner.lock").isSymLink()) {
            result.error="Invalid package lifetime lease. Collection has been skipped.";return result;
        }
        QLockFile owner(entry.absoluteFilePath()+"/owner.lock");owner.setStaleLockTime(0);
        const bool inactive=owner.tryLock(0);
        if(!inactive&&owner.error()!=QLockFile::LockFailedError){result.error="Cannot inspect a package lifetime lease. Collection has been skipped.";return result;}
        if(inactive)owner.unlock();
        QFileInfo info(entry.absoluteFilePath()+"/metadata.json");QFile metadata(info.absoluteFilePath());
        if(info.isSymLink()||!info.isFile()||!metadata.open(QIODevice::ReadOnly)||metadata.size()>1024) {
            result.error="Cannot read a package lifetime lease. Collection has been skipped.";return result;
        }
        QJsonParseError error;const auto document=QJsonDocument::fromJson(metadata.read(1025),&error);const auto object=document.object();
        metadata.close(); // Release the Windows handle before retiring its directory.
        ProcessIdentity process;
        if(error.error!=QJsonParseError::NoError||!document.isObject()||object.size()!=(object.contains("process")?3:2)||object["schemaVersion"]!=QJsonValue(1)
            ||!object["digest"].isString()||(!object["digest"].toString().isEmpty()&&!digest.match(object["digest"].toString()).hasMatch())) {
            result.error="Invalid package lifetime lease metadata. Collection has been skipped.";return result;
        }
        if(object.contains("process")&&(object["digest"].toString().isEmpty()||!object["process"].isObject()||!ProcessIdentity::parse(object["process"].toObject(),&process))) {
            result.error="Invalid plugin process lifetime identity. Collection has been skipped.";return result;
        }
        if(inactive&&(!object.contains("process")||process.state()==ProcessIdentity::State::Exited)) {
            QString error;
            if(result.staleLeases>=8){result.more=true;continue;}
            if(retire(store,entry,&error,cancel))++result.staleLeases;
            else {result.error=error.isEmpty()?QStringLiteral("Package collection cancelled."):error;result.cancelled=stopping(cancel);return result;}
            continue;
        }
        if(object["digest"].toString().isEmpty())blockAll=true;
        else keep.insert(object["digest"].toString());
    }
    if(blockAll){result.deferred=true;return result;}
    // Lease inspection MUST precede the disk snapshot. A new reference is
    // either covered by a lease observed above, or its lease was released
    // after commit and this later atomic read observes the durable reference.
    // The store lock prevents creating a previously unseen lease meanwhile.
    QSet<QString> installed;
    if(!InstalledPluginRegistry::readPackageReferences(index,store,&installed,&result.error))return result;
    keep.unite(installed);
    QVector<QFileInfo> children;
    if(!entries(store,&children,4096,&result.error))return result;
    static const QRegularExpression temporary("^\\.(stage|unpack|download)-[A-Za-z0-9]{6}$");
    QVector<QPair<QString,bool>> retired;
    std::shared_ptr<PackageLease> purgeLease;
    for(const auto &entry:children) {
        if(stopping(cancel)){result.cancelled=true;return result;}
        const auto name=entry.fileName();const bool package=digest.match(name).hasMatch();
        const bool transient=temporary.match(name).hasMatch()||(name.startsWith(".trash-")&&uuid.match(name.mid(7)).hasMatch());
        if(!package&&!transient)continue;
        if(package&&keep.contains(name))continue;
        if(retired.size()>=8){result.more=true;break;}
        if(entry.isSymLink()||!entry.isDir()){preserved(&result,name,"Unsafe or non-directory store entry.");continue;}
        if(package&&!ownedReceipt(entry)){preserved(&result,name,"Missing or invalid ownership receipt.");continue;}
        if(!purgeLease){purgeLease=PackageLease::acquireLocked(store,{},&result.error);if(!purgeLease)return result;}
        QString error,trash;
        if(!retire(store,entry,&error,cancel,&trash)) {
            if(stopping(cancel)){result.cancelled=true;return result;}
            preserved(&result,name,error);continue;
        }
        retired.append({trash,package});
    }
    // The publication lock is needed only to choose and rename
    // garbage. File deletion runs after releasing them, so ordinary registry
    // commits and new installations can proceed. The activity lease prevents
    // a second collector from renaming these in-progress trash directories.
    storeLock.unlock();
    for(const auto &[trash,package]:retired) {
        if(stopping(cancel)){result.cancelled=true;return result;}
        if(!QDir(trash).removeRecursively()){preserved(&result,QFileInfo(trash).fileName(),"Deletion incomplete; collection will retry.");result.more=true;continue;}
        if(package)++result.packages;else ++result.temporaryDirectories;
    }
    return result;
}
}
