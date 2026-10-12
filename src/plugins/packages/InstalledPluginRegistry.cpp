// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstalledPluginRegistry.h"
#include "SemVer.h"
#include "PackageLease.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>
namespace PluginPackages {
namespace {
constexpr qsizetype MaxIndexBytes=8*1024*1024;
constexpr int MaxPlugins=256;
bool reject(QString *error,const QString &text) { if(error)*error=text;return false; }
QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256); }
QString stableStorePath(const QString &input) {
    QString current=QDir(input).absolutePath();QStringList suffix;
    while(QFileInfo(current).canonicalFilePath().isEmpty()) {
        const auto parent=QFileInfo(current).absolutePath();
        if(parent==current) break;
        suffix.prepend(QFileInfo(current).fileName());current=parent;
    }
    const auto canonical=QFileInfo(current).canonicalFilePath();
    return (canonical.isEmpty()?current:canonical)+(suffix.isEmpty()?QString():"/"+suffix.join('/'));
}
InstalledRevision revision(const InstalledPlugin &p) {return {p.package,p.sourceIds,p.origin,p.installedAt};}
void restore(InstalledPlugin &p,const InstalledRevision &r) {p.package=r.package;p.sourceIds=r.sourceIds;p.origin=r.origin;p.installedAt=r.installedAt;}
QJsonObject serializeBase(const InstalledPlugin &p) {
    return {{"metadata",p.package.metadata},{"repository",p.package.repository},{"digest",p.package.digest},
            {"bytes",p.package.bytes},{"files",p.package.files},{"enabled",p.enabled},
            {"sourceIds",QJsonArray::fromStringList(p.sourceIds)},{"origin",p.origin},{"installedAt",p.installedAt.toUTC().toString(Qt::ISODateWithMs)}};
}
QJsonObject serialize(const InstalledPlugin &p) {
    auto result=serializeBase(p);
    if(p.previous) {
        InstalledPlugin previous;restore(previous,*p.previous);
        result["previous"]=serializeBase(previous);
        result["updatePending"]=p.updatePending;
        if(p.validationDeferred)result["validationDeferred"]=true;
    }
    if(p.pendingPrevious) {
        InstalledPlugin oldHistory;restore(oldHistory,*p.pendingPrevious);result["pendingPrevious"]=serializeBase(oldHistory);
    }
    return result;
}
bool parseRecord(const QJsonObject &object,const QString &store,InstalledPlugin *result,QString *error,bool allowHistory=true) {
    static const QRegularExpression digestPattern("^[a-f0-9]{64}$");
    PluginSources::SourceSnapshot snapshot;
    if(!object["metadata"].isObject()||!object["enabled"].isBool()||!object["sourceIds"].isArray()||!object["origin"].isObject()
       ||!digestPattern.match(object["digest"].toString()).hasMatch()) return reject(error,"Invalid installed plugin record.");
    const auto number=[](const QJsonValue &v,qint64 min,qint64 max){return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()==std::floor(v.toDouble())&&v.toDouble()>=min&&v.toDouble()<=max;};
    if(!number(object["bytes"],0,PackageStore::MaxPackageBytes)||!number(object["files"],1,PackageStore::MaxFiles))return reject(error,"Invalid installed package byte or entry count.");
    if(!PluginSources::PluginMetadata::parse(QJsonDocument(object["metadata"].toObject()).toJson(QJsonDocument::Compact),object["repository"].toString(),{},&snapshot,error))return false;
    if(snapshot.isMarketplace())return reject(error,"A marketplace cannot be an installed package.");
    InstalledPlugin p;
    p.package.metadata=snapshot.metadata;p.package.repository=snapshot.repository;p.package.digest=object["digest"].toString();
    p.package.root=QDir(store).absoluteFilePath(p.package.digest);p.package.bytes=object["bytes"].toInteger();p.package.files=object["files"].toInt();
    p.enabled=object["enabled"].toBool();p.origin=object["origin"].toObject();
    p.installedAt=QDateTime::fromString(object["installedAt"].toString(),Qt::ISODateWithMs);
    if(!p.installedAt.isValid()||p.origin.size()>16||QJsonDocument(p.origin).toJson(QJsonDocument::Compact).size()>8192||object["sourceIds"].toArray().size()>64) return reject(error,"Invalid installed package provenance.");
    for(const auto &source:object["sourceIds"].toArray()) {
        if(!source.isString()||source.toString().isEmpty()||source.toString().size()>1024||p.sourceIds.contains(source.toString())) return reject(error,"Invalid installed source association.");
        p.sourceIds.append(source.toString());
    }
    if(object.contains("previous")) {
        InstalledPlugin previous;
        if(!allowHistory||!object["previous"].isObject()||!object["updatePending"].isBool()
            ||!parseRecord(object["previous"].toObject(),store,&previous,error,false))return reject(error,"Invalid installed rollback record.");
        if(previous.id()!=p.id()||previous.package.repository!=p.package.repository||previous.package.digest==p.package.digest)
            return reject(error,"Rollback package identity does not match the installed plugin.");
        p.previous=revision(previous);p.updatePending=object["updatePending"].toBool();
    }else if(object.contains("updatePending"))return reject(error,"Pending update has no rollback record.");
    if(object.contains("pendingPrevious")) {
        InstalledPlugin history;
        if(!p.updatePending||!object["pendingPrevious"].isObject()||!parseRecord(object["pendingPrevious"].toObject(),store,&history,error,false)
            ||history.id()!=p.id()||history.package.repository!=p.package.repository||history.package.digest==p.previous->package.digest)
            return reject(error,"Invalid pending rollback history.");
        p.pendingPrevious=revision(history);
    }
    if(object.contains("validationDeferred")) {
        if(!p.updatePending||object["validationDeferred"]!=QJsonValue(true))return reject(error,"Invalid deferred update validation state.");
        p.validationDeferred=true;
    }
    *result=p;return true;
}
bool readRecords(const QString &path,const QString &store,QVector<InstalledPlugin> *records,QByteArray *bytes,QString *error) {
    QFileInfo info(path);QFile file(path);
    if(info.isSymLink()||(info.exists()&&!info.isFile()))return reject(error,"Installed plugin storage must be a regular file. The entry has been preserved.");
    if(!file.exists()){records->clear();bytes->clear();return true;}
    if(!file.open(QIODevice::ReadOnly)||file.size()>MaxIndexBytes)return reject(error,"Cannot read installed plugin storage.");
    *bytes=file.read(MaxIndexBytes+1);QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(*bytes,&parseError);const auto object=document.object();
    if(bytes->size()>MaxIndexBytes||parseError.error!=QJsonParseError::NoError||!document.isObject()||object["schemaVersion"]!=QJsonValue(1)
        ||!object["plugins"].isArray()||object["plugins"].toArray().size()>MaxPlugins)
        return reject(error,"Installed plugin storage has an invalid format. The file has been preserved.");
    QVector<InstalledPlugin> candidate;QSet<QString> ids;
    for(const auto &item:object["plugins"].toArray()) {
        InstalledPlugin p;QString validationError;
        if(!item.isObject()||!parseRecord(item.toObject(),store,&p,&validationError)||ids.contains(p.id()))
            return reject(error,validationError.isEmpty()?"Duplicate installed plugin.":validationError);
        ids.insert(p.id());candidate.append(p);
    }
    *records=candidate;return true;
}
QSet<QString> references(const QVector<InstalledPlugin> &records) {
    QSet<QString> result;
    for(const auto &p:records) {
        result.insert(p.package.digest);
        if(p.previous)result.insert(p.previous->package.digest);
        if(p.pendingPrevious)result.insert(p.pendingPrevious->package.digest);
    }
    return result;
}
}
InstalledPluginRegistry::InstalledPluginRegistry(QString filePath,QString storePath,QObject *parent):QObject(parent),filePath(std::move(filePath)),storePath(stableStorePath(storePath)) {}
bool InstalledPluginRegistry::readPackageReferences(const QString &path,const QString &store,QSet<QString> *digests,QString *error) {
    QVector<InstalledPlugin> candidate;QByteArray bytes;
    if(!readRecords(path,store,&candidate,&bytes,error))return false;
    *digests=references(candidate);return true;
}
bool InstalledPluginRegistry::load(QString *error) {
    const auto fail=[&](const QString &message){readError=message;return reject(error,message);};
    QVector<InstalledPlugin> candidate;QByteArray bytes;QString validationError;
    if(!readRecords(filePath,storePath,&candidate,&bytes,&validationError))return fail(validationError);
    records=candidate;diskDigest=bytes.isEmpty()?QByteArray():hash(bytes);readError.clear();
    // The candidate and its last good revision share one atomic index commit.
    // A crash at any point before activation confirmation restores the old
    // revision before any commands or startup activation are published.
    bool recovering=false;
    for(auto &p:candidate)if(p.updatePending&&!p.validationDeferred){restore(p,*p.previous);p.previous=p.pendingPrevious;p.pendingPrevious.reset();p.updatePending=false;recovering=true;}
    if(recovering&&!commit(candidate,error))return fail(error?*error:QStringLiteral("Cannot recover an interrupted plugin update."));
    if(!recovering)emit changed();return true;
}
bool InstalledPluginRegistry::commit(const QVector<InstalledPlugin> &candidate,QString *error) {
    if(!readError.isEmpty()) return reject(error,"Resolve the installed storage error before making changes: "+readError);
    if(candidate.size()>MaxPlugins) return reject(error,"At most 256 plugins may be installed.");
    if(!QDir().mkpath(QFileInfo(filePath).absolutePath())) return reject(error,"Cannot create installed plugin storage.");
    if(QFileInfo(storePath).isSymLink()||!QDir().mkpath(storePath))return reject(error,"Cannot create a real package store.");
    // New references must be pinned before index publication and until after
    // the atomic commit. Same/subset reference changes (enable, remove,
    // confirm, abort) require no store lock, so GC never blocks normal stops.
    const auto oldReferences=references(records);
    QVector<std::shared_ptr<PackageLease>> pins;
    for(const auto &p:candidate) {
        QVector<PreparedPackage> packages{p.package};if(p.previous)packages.append(p.previous->package);if(p.pendingPrevious)packages.append(p.pendingPrevious->package);
        for(const auto &package:packages)if(!oldReferences.contains(package.digest)) {
            auto lease=package.lease;
            if(!lease||!lease->protects(storePath,package.digest))lease=PackageLease::acquire(storePath,package.digest,error);
            if(!lease)return false;
            pins.append(lease);
        }
    }
    if(QFileInfo(filePath).isSymLink()||QFileInfo(filePath+".lock").isSymLink())return reject(error,"Installed plugin storage or its lock is a symbolic link. The entry has been preserved.");
    QLockFile lock(filePath+".lock");lock.setStaleLockTime(0);if(!lock.tryLock(0)) return reject(error,"Installed plugin storage is busy.");
    QFile current(filePath);QByteArray currentDigest;
    if(current.exists()) {
        if(!current.open(QIODevice::ReadOnly)||current.size()>MaxIndexBytes) return reject(error,"Cannot check installed plugin storage.");
        currentDigest=hash(current.read(MaxIndexBytes+1));
    }
    if(currentDigest!=diskDigest) return reject(error,"Installed plugin storage changed in another process. Reload it before continuing.");
    QJsonArray items;QSet<QString> ids;
    for(const auto &p:candidate) {
        InstalledPlugin validated;
        if(!parseRecord(serialize(p),storePath,&validated,error))return false;
        if(ids.contains(p.id()))return reject(error,"Duplicate installed plugin record.");
        QVector<PreparedPackage> packages{p.package};
        if(p.previous)packages.append(p.previous->package);
        if(p.pendingPrevious)packages.append(p.pendingPrevious->package);
        for(const auto &package:packages)if(!oldReferences.contains(package.digest)&&!preparedInStore(package,error))return false;
        ids.insert(p.id());items.append(serialize(p));
    }
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"plugins",items}}).toJson(QJsonDocument::Indented);
    if(bytes.size()>MaxIndexBytes) return reject(error,"Installed plugin storage exceeds 8 MiB.");
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) return reject(error,"Cannot atomically save installed plugin storage.");
    records=candidate;
    for(auto &p:records){p.package.lease.reset();if(p.previous)p.previous->package.lease.reset();if(p.pendingPrevious)p.pendingPrevious->package.lease.reset();}
    diskDigest=hash(bytes);lock.unlock();emit changed();return true;
}
InstalledPlugin InstalledPluginRegistry::plugin(const QString &id) const {for(const auto &p:records)if(p.id()==id)return p;return {};}
bool InstalledPluginRegistry::add(const PreparedPackage &prepared,const QStringList &sources,const QJsonObject &origin,QString *error) {
    const auto id=prepared.metadata["id"].toString();
    if(!plugin(id).id().isEmpty()) return reject(error,"The plugin is already installed. Use the update transaction.");
    if(!preparedInStore(prepared,error))return false;
    auto candidate=records;candidate.append({prepared,sources,origin,QDateTime::currentDateTimeUtc(),false});
    return commit(candidate,error);
}
bool InstalledPluginRegistry::preparedInStore(const PreparedPackage &prepared,QString *error) const {
    static const QRegularExpression digestPattern("^[a-f0-9]{64}$");
    const auto expectedRoot=QDir(storePath).absoluteFilePath(prepared.digest);
    if(!digestPattern.match(prepared.digest).hasMatch()||prepared.root!=expectedRoot||QFileInfo(prepared.root).isSymLink()
        ||QFileInfo(prepared.root).canonicalFilePath()!=expectedRoot||!QFileInfo::exists(expectedRoot+"/.nnp-receipt.json"))
        return reject(error,"Package has not been prepared in this installation store.");
    return true;
}
bool InstalledPluginRegistry::beginUpdate(const PreparedPackage &prepared,const QStringList &sources,const QJsonObject &origin,const QString &expectedDigest,QString *error,bool deferValidation) {
    if(!preparedInStore(prepared,error))return false;
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==prepared.metadata.value("id").toString()) {
        if(p.updatePending||p.package.digest!=expectedDigest)return reject(error,"The installed plugin changed while preparing its update.");
        if(p.package.repository!=prepared.repository)return reject(error,"An update cannot change the plugin release repository.");
        SemVer current,next;
        if(!SemVer::parse(p.package.metadata.value("version").toString(),&current)
            ||!SemVer::parse(prepared.metadata.value("version").toString(),&next)||next.compare(current)<=0)
            return reject(error,"Choose a newer plugin version. Use Roll Back to restore a previous version.");
        p.pendingPrevious=p.previous;p.previous=revision(p);p.updatePending=true;p.validationDeferred=deferValidation;p.package=prepared;p.sourceIds=sources;p.origin=origin;p.installedAt=QDateTime::currentDateTimeUtc();
        return commit(candidate,error);
    }
    return reject(error,"The plugin is no longer installed.");
}
bool InstalledPluginRegistry::beginRollback(const QString &id,const QString &expectedDigest,QString *error,bool deferValidation) {
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==id) {
        if(p.updatePending||p.package.digest!=expectedDigest||!p.previous)return reject(error,"No previous version is available for this plugin.");
        const auto old=*p.previous;p.pendingPrevious=p.previous;p.previous=revision(p);restore(p,old);p.updatePending=true;p.validationDeferred=deferValidation;
        return commit(candidate,error);
    }
    return reject(error,"The plugin is no longer installed.");
}
bool InstalledPluginRegistry::startValidation(const QString &id,QString *error) {
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==id&&p.updatePending&&p.validationDeferred){p.validationDeferred=false;return commit(candidate,error);}
    return reject(error,"There is no deferred update to validate.");
}
bool InstalledPluginRegistry::confirmUpdate(const QString &id,QString *error) {
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==id&&p.updatePending&&!p.validationDeferred){p.updatePending=false;p.pendingPrevious.reset();return commit(candidate,error);}
    return reject(error,"There is no pending update to confirm.");
}
bool InstalledPluginRegistry::abortUpdate(const QString &id,QString *error) {
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==id&&p.updatePending&&p.previous){restore(p,*p.previous);p.previous=p.pendingPrevious;p.pendingPrevious.reset();p.updatePending=false;p.validationDeferred=false;return commit(candidate,error);}
    return reject(error,"There is no pending update to roll back.");
}
bool InstalledPluginRegistry::setEnabled(const QString &id,bool enabled,QString *error) {
    auto candidate=records;
    for(auto &p:candidate)if(p.id()==id){if(p.updatePending&&!p.validationDeferred)return reject(error,"Wait for the pending plugin update to finish.");p.enabled=enabled;return commit(candidate,error);}
    return reject(error,"The plugin is no longer installed.");
}
bool InstalledPluginRegistry::remove(const QString &id,QString *error) {
    auto candidate=records;
    for(qsizetype i=0;i<candidate.size();++i)if(candidate[i].id()==id){if(candidate[i].updatePending&&!candidate[i].validationDeferred)return reject(error,"Wait for the pending plugin update to finish.");candidate.removeAt(i);return commit(candidate,error);}
    return reject(error,"The plugin is no longer installed.");
}
}
