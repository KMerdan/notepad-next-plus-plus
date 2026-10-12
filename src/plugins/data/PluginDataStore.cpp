// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginDataStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
namespace PluginData {
namespace {
Result fail(int code,const QString &message) { return {QJsonValue(QJsonValue::Null),{{"code",code},{"message",message}}}; }
bool depth(const QJsonValue &value,int level=0) {
    if(level>32)return false;
    if(value.isDouble()&&!std::isfinite(value.toDouble()))return false;
    if(value.isArray())for(const auto &item:value.toArray())if(!depth(item,level+1))return false;
    if(value.isObject())for(const auto &item:value.toObject())if(!depth(item,level+1))return false;
    return true;
}
Result read(const QString &path,const QString &id,const QString &repository,QJsonObject *record) {
    *record={{"schemaVersion",1},{"pluginId",id},{"repository",repository},{"storage",QJsonObject{}},{"configuration",QJsonObject{}}};
    const QFileInfo info(path);
    if(info.isSymLink()||(info.exists()&&!info.isFile()))return fail(1010,"Plugin data path is not a regular file. The path has been preserved.");
    if(!info.exists())return {};
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>Store::MaxFileBytes)return fail(1010,"Cannot read bounded plugin data. The file has been preserved.");
    const auto bytes=file.read(Store::MaxFileBytes+1);
    if(bytes.size()>Store::MaxFileBytes)return fail(1010,"Plugin data exceeds its read budget. The file has been preserved.");
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);const auto object=document.object();
    if(error.error!=QJsonParseError::NoError||!document.isObject()||object.size()!=5||object["schemaVersion"]!=1||object["pluginId"]!=id||object["repository"]!=repository)
        return fail(1010,"Plugin data format is invalid. The file has been preserved.");
    for(const auto &space:QStringList{"storage","configuration"}) {
        if(!object[space].isObject()||object[space].toObject().size()>Store::MaxKeys)return fail(1010,"Plugin data namespace is invalid. The file has been preserved.");
        const auto values=object[space].toObject();
        for(auto it=values.begin();it!=values.end();++it)if(!Store::validKey(it.key())||!Store::validValue(it.value()))return fail(1010,"Plugin data value is invalid. The file has been preserved.");
    }
    *record=object;return {};
}
bool safeRoot(const QString &root) {const QFileInfo info(root);return !info.isSymLink()&&(!info.exists()||info.isDir());}
}
bool Store::validPluginId(const QString &id) {return QRegularExpression("^[a-z0-9][a-z0-9._-]{0,127}$").match(id).hasMatch();}
bool Store::validKey(const QString &key) {return QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$").match(key).hasMatch();}
bool Store::validValue(const QJsonValue &value) {return value.isUndefined()||(depth(value)&&QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size()-2<=MaxValueBytes);}
QString Store::filePath(const QString &id) const {return QDir(root).filePath(QString::fromLatin1(QCryptographicHash::hash((id+'\n'+repository).toUtf8(),QCryptographicHash::Sha256).toHex())+".json");}
Result Store::access(const QString &id,const QString &space,const QString &key,bool update,const QJsonValue &value,const QJsonValue &expectedValue) const {
    if(!validPluginId(id)||!validKey(key)||(space!="storage"&&space!="configuration"))return fail(-32602,"Invalid plugin data namespace or key");
    if(!validValue(value)||!validValue(expectedValue))return fail(1008,"Plugin data value exceeds 64 KiB or 32 levels");
    if(!update&&!expectedValue.isUndefined())return fail(-32602,"Expected value requires an update");
    if(!safeRoot(root))return fail(1010,"Plugin data directory is unavailable. The path has been preserved.");
    if(update&&!QDir().mkpath(root))return fail(1010,"Cannot create plugin data directory");
    const auto path=filePath(id);
    // Lock the read-modify-write operation, rather than silently overwriting
    // another running application instance. Reads see atomic snapshots.
    QLockFile lock(path+".lock");
    if(update&&(QFileInfo(path+".lock").isSymLink()||!lock.tryLock(0)))return fail(1010,"Plugin data is busy");
    QJsonObject record;const auto loaded=read(path,id,repository,&record);if(!loaded.error.isEmpty())return loaded;
    auto values=record[space].toObject();
    if(!update)return {values.contains(key)?values[key]:QJsonValue(QJsonValue::Null),{}};
    // Compare and write under the same record lock. Null is the public read
    // representation of a missing key, so it also accepts an absent value.
    const auto current=values.contains(key)?values[key]:QJsonValue(QJsonValue::Null);
    if(!expectedValue.isUndefined()&&current!=expectedValue)return fail(1001,"Plugin data changed since it was read");
    if(value.isUndefined())values.remove(key);else values[key]=value;
    if(values.size()>MaxKeys)return fail(1007,"Plugin data namespace exceeds 256 keys");
    record[space]=values;const auto bytes=QJsonDocument(record).toJson(QJsonDocument::Compact);
    if(bytes.size()>MaxFileBytes)return fail(1008,"Plugin data exceeds 1 MiB");
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail(1010,"Cannot atomically save plugin data");
    return {};
}
Result Store::erase(const QString &id) const {
    if(!validPluginId(id))return fail(-32602,"Invalid plugin identity");
    if(!safeRoot(root))return fail(1010,"Plugin data directory is unavailable. The path has been preserved.");
    const auto path=filePath(id);const QFileInfo info(path);
    if(!info.exists()&&!info.isSymLink())return {};
    QLockFile lock(path+".lock");if(QFileInfo(path+".lock").isSymLink()||!lock.tryLock(0))return fail(1010,"Plugin data is busy");
    // Explicit data deletion may remove a corrupt record, but never follows a
    // directory or link supplied at its contained file path.
    if(info.isSymLink()||!info.isFile())return fail(1010,"Plugin data path is not a regular file. The path has been preserved.");
    if(!QFile::remove(path))return fail(1010,"Cannot delete plugin data");
    return {};
}
}
