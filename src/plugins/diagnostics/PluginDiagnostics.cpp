// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginDiagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <cmath>
#include <memory>
namespace PluginDiagnostics {
namespace {
const QStringList categories={"","package-invalid","capability-missing","runtime-unavailable","initialization-failed","activation-failed","protocol-fault","process-crash","health-timeout","command-failed","update-failed","ownership-failure"};
const char *labels[]={"","Invalid package","Missing capability","Runtime unavailable","Initialization failed","Activation failed","Protocol fault","Process crashed","Health check timed out","Command failed","Update failed","Process ownership failed"};
QString bounded(const QByteArray &bytes,int limit) {
    auto text=QString::fromUtf8(bytes);
    for(auto &c:text)if(c.unicode()<32&&c!='\n'&&c!='\t')c=QChar(0xfffd);
    if(text.toUtf8().size()<=limit)return text;
    // Cut on a UTF-8 boundary, then retain an explicit truncation marker.
    auto prefix=text.toUtf8().left(limit-32);
    while(!prefix.isEmpty()&&(quint8(prefix.back())&0xc0)==0x80)prefix.chop(1);
    if(!prefix.isEmpty()&&quint8(prefix.back())>=0xc0)prefix.chop(1);
    return QString::fromUtf8(prefix)+"\n[truncated]";
}
bool safe(const QString &path,bool directory=false) {
    const QFileInfo info(path);return !info.isSymLink()&&(!info.exists()||(directory?info.isDir():info.isFile()));
}
QJsonObject json(const Snapshot &snapshot) {return {{"schemaVersion",1},{"events",snapshot.events},{"failures",snapshot.failures}};}
Snapshot readFile(const QString &path) {
    Snapshot result;if(!safe(path)){result.error="Plugin diagnostic file is unsafe; it has been preserved.";return result;}
    QFile file(path);if(!file.exists())return result;
    if(!file.open(QIODevice::ReadOnly)||file.size()>Store::MaxFileBytes){result.error="Cannot read bounded plugin diagnostics; the file has been preserved.";return result;}
    const auto bytes=file.read(Store::MaxFileBytes+1);QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(bytes,&error);const auto object=doc.object();
    if(bytes.size()>Store::MaxFileBytes||error.error!=QJsonParseError::NoError||!doc.isObject()||object.size()!=3||object["schemaVersion"]!=1||!object["events"].isArray()||!object["failures"].isArray()) {result.error="Plugin diagnostic format is invalid; the file has been preserved.";return result;}
    result.events=object["events"].toArray();result.failures=object["failures"].toArray();
    if(result.events.size()>Store::MaxEvents||result.failures.size()>Store::MaxFailures){result.error="Plugin diagnostics exceed the record budget; the file has been preserved.";return result;}
    for(const auto &event:result.events)if(!event.isObject()||!Store::validEvent(event.toObject())){result.error="Plugin diagnostic event is invalid; the file has been preserved.";return result;}
    QSet<QString> origins;
    for(const auto &value:result.failures) {
        const auto event=value.toObject();const auto origin=event["pluginId"].toString()+'\n'+event["repository"].toString();
        if(!value.isObject()||!Store::validEvent(event)||event["event"]!="failure"||event["category"].toString().isEmpty()||origins.contains(origin)){result.error="Plugin failure summary is invalid; the file has been preserved.";return result;}
        origins.insert(origin);
    }
    return result;
}
bool save(const QString &path,const QByteArray &bytes) {
    if(!safe(path))return false;
    QSaveFile file(path);file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly)&&file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)&&file.write(bytes)==bytes.size()&&file.commit();
}
QString retireInterruptedWrites(const QString &root) {
    // QSaveFile uses <destination>.XXXXXX for named staging files. The store
    // lock excludes every live writer; interrupted files have no reader owner.
    // Keep unknown names, links, directories and oversized records intact.
    static const QRegularExpression temporary("\\A(?:current|previous)\\.json\\.[A-Za-z0-9]{6}\\z");
    QDirIterator entries(root,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot);int inspected=0;
    while(entries.hasNext()) {
        if(++inspected>512)return "Plugin diagnostic directory exceeds its inspection budget.";
        entries.next();const auto file=entries.fileInfo();if(!temporary.match(file.fileName()).hasMatch())continue;
        if(file.isSymLink()||!file.isFile()||file.size()>Store::MaxFileBytes)return "Unsafe interrupted diagnostic file has been preserved.";
        if(!QFile::remove(file.filePath()))return "Cannot retire an interrupted diagnostic write.";
    }
    return {};
}
}
QString category(Failure failure) {const auto index=int(failure);return index>=0&&index<categories.size()?categories[index]:QString();}
QString label(Failure failure) {const auto index=int(failure);return index>=0&&index<categories.size()?QCoreApplication::translate("PluginDiagnostics",labels[index]):QString();}
QString displayMessage(const QString &message) {
    if(message.size()<=2048)return message;
    auto text=message.left(2048);if(!text.isEmpty()&&text.at(text.size()-1).isHighSurrogate())text.chop(1);
    return text+"\n[truncated]";
}
QJsonObject Store::event(const QString &id,const QString &repository,const QString &session,const QString &kind,const QByteArray &message,Failure failure,const QJsonObject &details) {
    QJsonObject result{{"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"pluginId",id},{"repository",repository},{"sessionId",session},{"event",kind},{"category",category(failure)},{"message",bounded(message,8*1024)}};
    for(auto it=details.begin();it!=details.end();++it)if(it.key()=="method"||it.key()=="durationMs"||it.key()=="exitCode")result[it.key()]=it.value();
    return result;
}
bool Store::validEvent(const QJsonObject &event) {
    static const QRegularExpression id("\\A[a-z0-9][a-z0-9._-]{0,127}\\z"),repo("\\Ahttps://github\\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\\z");
    const QStringList keys={"timestamp","pluginId","repository","sessionId","event","category","message"};
    if(event.size()<7||event.size()>10)return false;
    for(const auto &key:keys)if(!event[key].isString())return false;
    for(auto it=event.begin();it!=event.end();++it)if(!keys.contains(it.key())&&it.key()!="method"&&it.key()!="durationMs"&&it.key()!="exitCode")return false;
    if(!id.match(event["pluginId"].toString()).hasMatch()||!repo.match(event["repository"].toString()).hasMatch()||event["repository"].toString().size()>512||event["sessionId"].toString().size()>128)return false;
    const auto date=event["timestamp"].toString();
    if(date.size()>40||!date.endsWith('Z')||!QDateTime::fromString(date,Qt::ISODateWithMs).isValid())return false;
    if(!QStringList{"state","stderr","diagnostic","failure","command","exit","update","recovery"}.contains(event["event"].toString())||!categories.contains(event["category"].toString())||event["message"].toString().toUtf8().size()>8*1024)return false;
    if(event["event"]=="failure"&&event["category"].toString().isEmpty())return false;
    if(event.contains("method")&&(!event["method"].isString()||event["method"].toString().size()>192))return false;
    for(const auto &key:QStringList{"durationMs","exitCode"})if(event.contains(key)) {
        const auto number=event[key].toDouble();if(!event[key].isDouble()||!std::isfinite(number)||number!=std::floor(number)||number<-2147483648.0||number>2147483647.0||(key=="durationMs"&&number<0))return false;
    }
    return QJsonDocument(event).toJson(QJsonDocument::Compact).size()<=MaxEventBytes;
}
QString Store::format(const QJsonObject &event) {
    QString prefix=event["timestamp"].toString()+" ["+event["event"].toString()+']';
    if(!event["category"].toString().isEmpty())prefix+=" ["+event["category"].toString()+']';
    if(!event["sessionId"].toString().isEmpty())prefix+=" ["+event["sessionId"].toString()+']';
    if(event.contains("method"))prefix+=' '+event["method"].toString();
    if(event.contains("durationMs"))prefix+=QString(" (%1 ms)").arg(event["durationMs"].toInteger());
    if(event.contains("exitCode"))prefix+=QString(" exit=%1").arg(event["exitCode"].toInteger());
    return prefix+'\n'+event["message"].toString()+'\n';
}
Snapshot Store::read() const {
    if(!QDir::isAbsolutePath(root)||!safe(root,true))return {{},{},"Plugin diagnostic directory is unavailable; the path has been preserved."};
    const auto previous=readFile(QDir(root).filePath("previous.json"));if(!previous.error.isEmpty())return previous;
    const auto current=QDir(root).filePath("current.json");return QFileInfo::exists(current)||QFileInfo(current).isSymLink()?readFile(current):previous;
}
Snapshot Store::append(const QJsonArray &events) const {
    if(events.size()>128)return {{},{},"Plugin diagnostic batch exceeds its record budget."};
    for(const auto &event:events)if(!event.isObject()||!validEvent(event.toObject()))return {{},{},"Invalid plugin diagnostic event."};
    if(!QDir::isAbsolutePath(root)||!safe(root,true)||!QDir().mkpath(root))return {{},{},"Cannot create the plugin diagnostic directory."};
    const auto lockPath=QDir(root).filePath("store.lock");QLockFile lock(lockPath);lock.setStaleLockTime(0);
    if(!safe(lockPath)||!lock.tryLock(0))return {{},{},"Plugin diagnostic store is busy or unavailable."};
    auto result=read();if(!result.error.isEmpty())return result;
    result.error=retireInterruptedWrites(root);if(!result.error.isEmpty())return result;
    const auto previous=QJsonDocument(json(result)).toJson(QJsonDocument::Compact);
    for(const auto &value:events) {
        const auto event=value.toObject();result.events.append(event);
        if(event["event"]=="failure") {
            for(int i=result.failures.size()-1;i>=0;--i) {const auto old=result.failures[i].toObject();if(old["pluginId"]==event["pluginId"]&&old["repository"]==event["repository"])result.failures.removeAt(i);}
            auto summary=event;summary["message"]=bounded(event["message"].toString().toUtf8(),1024);result.failures.append(summary);
        }
    }
    while(result.events.size()>MaxEvents)result.events.removeFirst();
    while(result.failures.size()>MaxFailures)result.failures.removeFirst();
    auto bytes=QJsonDocument(json(result)).toJson(QJsonDocument::Compact);
    while(bytes.size()>MaxFileBytes) {
        if(!result.events.isEmpty())result.events.removeFirst();else if(!result.failures.isEmpty())result.failures.removeFirst();else return {{},{},"Plugin diagnostic budget cannot be satisfied."};
        bytes=QJsonDocument(json(result)).toJson(QJsonDocument::Compact);
    }
    // An interrupted writer between commits leaves the old current file intact.
    // Each file is independently bounded and atomically published.
    if(!save(QDir(root).filePath("previous.json"),previous)||!save(QDir(root).filePath("current.json"),bytes))return {{},{},"Cannot atomically save plugin diagnostics."};
    return result;
}
Service::Service(QString directory,QObject *parent):QObject(parent),root(std::move(directory)) {
    timer.setSingleShot(true);timer.setInterval(250);connect(&timer,&QTimer::timeout,this,&Service::pump);
    QTimer::singleShot(0,this,&Service::pump);
}
void Service::append(const QJsonObject &event) {
    if(!Store::validEvent(event))return;
    if(pending.size()>=128) {
        // Failure summaries have priority over a noisy stderr stream.
        if(event["event"]=="failure") {int discard=-1;for(int i=0;i<pending.size();++i)if(pending[i].toObject()["event"]!="failure"){discard=i;break;}if(discard>=0)pending.removeAt(discard);else {++dropped;return;}}
        else {++dropped;return;}
        ++dropped;
    }
    pending.append(event);if(flushing)pump();else if(!running&&!timer.isActive())timer.start();
}
void Service::flush() {flushing=true;timer.stop();pump();}
void Service::pump() {
    if(running)return;
    if(restoredOnce&&pending.isEmpty()){emit idle();return;}
    timer.stop();running=true;
    const bool load=!restoredOnce;QJsonArray batch;
    if(!load) {
        for(int i=0;i<64&&!pending.isEmpty();++i){batch.append(pending.first());pending.removeFirst();}
        if(dropped&&!batch.isEmpty()) {const auto origin=batch.first().toObject();batch.append(Store::event(origin["pluginId"].toString(),origin["repository"].toString(),{},"diagnostic",QString("Skipped %1 diagnostic records because the queue reached its budget.").arg(dropped).toUtf8()));dropped=0;}
    }
    const auto directory=root;auto result=std::make_shared<Snapshot>();
    auto *worker=QThread::create([directory,batch,load,result]{const Store store(directory);*result=load?store.read():store.append(batch);});
    connect(worker,&QThread::finished,this,[this,result,load,batch] {
        running=false;
        if(!result->error.isEmpty()) {
            if(lastError!=result->error){lastError=result->error;emit unavailable(lastError);}
            if(!load&&retries++<2) {
                QJsonArray next=batch;for(const auto &event:pending)next.append(event);
                while(next.size()>128){int discard=0;for(int i=0;i<next.size();++i)if(next[i].toObject()["event"]!="failure"){discard=i;break;}next.removeAt(discard);++dropped;}
                pending=next;timer.start(100);return;
            }
            retries=0;
        }else {if(!lastError.isEmpty()){lastError.clear();emit available();}retries=0;}
        if(load){restoredOnce=true;if(result->error.isEmpty())emit restored(result->events,result->failures);}
        if(pending.isEmpty())emit idle();else if(flushing)pump();else timer.start(250);
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
}
}
