// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageStore.h"
#include "SemVer.h"
#include "ZipExtractor.h"
#include "PackageLease.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#endif
namespace PluginPackages {
namespace {
bool reject(QString *error,const QString &text) { if(error)*error=text;return false; }
bool cancelled(const std::atomic_bool *cancel,QString *error) {
    if(!cancel||!cancel->load()) return false;
    reject(error,"Installation cancelled."); return true;
}
}
bool PackageStore::isSafeRelativePath(const QString &name) {
    // Portable archive names: no aliases on case insensitive macOS/Windows,
    // NTFS streams/device names, traversal, trailing dots or spaces.
    if(name.isEmpty()||name.size()>512||name.startsWith('/')||name.contains('\\')||name.contains(':')||name.contains(QChar(0))) return false;
    static const QRegularExpression device("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    for(const auto &part:name.split('/')) {
        if(part.isEmpty()||part=="."||part==".."||part.endsWith('.')||part.endsWith(' ')||device.match(part).hasMatch()) return false;
        for(const auto c:part) if(c.unicode()<32||QStringLiteral("<>\"|?*").contains(c)) return false;
    }
    return true;
}
namespace {
bool unnamedStreamOnly(const QString &path) {
#ifdef Q_OS_WIN
    WIN32_FIND_STREAM_DATA stream{};const auto name=QDir::toNativeSeparators(path).toStdWString();
    const auto handle=FindFirstStreamW(name.c_str(),FindStreamInfoStandard,&stream,0);
    if(handle==INVALID_HANDLE_VALUE){const auto error=GetLastError();return error==ERROR_HANDLE_EOF||error==ERROR_INVALID_PARAMETER;}
    bool safe=true;
    do {if(std::wcscmp(stream.cStreamName,L"::$DATA")!=0){safe=false;break;}}while(FindNextStreamW(handle,&stream));
    if(safe&&GetLastError()!=ERROR_HANDLE_EOF)safe=false;
    FindClose(handle);return safe;
#else
    Q_UNUSED(path);return true;
#endif
}
struct Inventory { QJsonArray files; qint64 bytes=0; QSet<QString> aliases; };
bool inventory(const QString &root,const QString &relative,const QString &destination,Inventory *result,QString *error,
               const std::atomic_bool *cancel,const std::function<void(qint64)> &progress,int depth=0) {
    if(cancelled(cancel,error)) return false;
    if(depth>32) return reject(error,"Package directory nesting exceeds 32.");
    const auto directory=root+(relative.isEmpty()?QString():"/"+relative);
    if(!unnamedStreamOnly(directory))return reject(error,"Package directories cannot contain alternate data streams.");
    QVector<QFileInfo> entries;
    QDirIterator iterator(directory,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
    while(iterator.hasNext()) {
        iterator.next();entries.append(iterator.fileInfo());
        if(entries.size()+result->aliases.size()>PackageStore::MaxFiles+1) return reject(error,"Too many package entries.");
        if(cancelled(cancel,error)) return false;
    }
    std::sort(entries.begin(),entries.end(),[](const QFileInfo &a,const QFileInfo &b){return a.fileName()<b.fileName();});
    for(const auto &entry:entries) {
        const auto name=relative.isEmpty()?entry.fileName():relative+'/'+entry.fileName();
        if(!PackageStore::isSafeRelativePath(name)||entry.isSymLink()||(!entry.isDir()&&!entry.isFile())) return reject(error,"Unsafe or unsupported package entry: "+name);
        if(!unnamedStreamOnly(entry.absoluteFilePath()))return reject(error,"Package entries cannot contain alternate data streams: "+name);
        if(name==".nnp-receipt.json" && destination.isEmpty() && relative.isEmpty()) continue;
        if(name==".nnp-receipt.json") return reject(error,"Package uses a reserved receipt name.");
        const auto alias=name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if(result->aliases.contains(alias)||result->aliases.size()>=PackageStore::MaxFiles) return reject(error,"Duplicate portable path or too many package entries: "+name);
        result->aliases.insert(alias);
        if(entry.isDir()) {
            result->files.append(QJsonObject{{"path",name},{"directory",true}});
            if(!destination.isEmpty()&&!QDir().mkpath(destination+'/'+name)) return reject(error,"Cannot create package staging directory.");
            if(!inventory(root,name,destination,result,error,cancel,progress,depth+1)) return false;
            continue;
        }
        if(entry.suffix().compare("node",Qt::CaseInsensitive)==0) return reject(error,"Native Node addons are not supported by this package profile.");
        if(entry.size()<0||entry.size()>PackageStore::MaxFileBytes||result->bytes+entry.size()>PackageStore::MaxPackageBytes) return reject(error,"Package byte limit exceeded.");
        QFile input(entry.absoluteFilePath()),output(destination+'/'+name);
        if(!input.open(QIODevice::ReadOnly)||(!destination.isEmpty()&&!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))) return reject(error,"Cannot read or stage package file: "+name);
        QCryptographicHash hash(QCryptographicHash::Sha256); qint64 copied=0;
        while(!input.atEnd()) {
            if(cancelled(cancel,error)) return false;
            const auto bytes=input.read(64*1024);
            if(bytes.isEmpty()&&input.error()!=QFile::NoError) return reject(error,"Cannot read package file: "+name);
            copied+=bytes.size();
            if(copied>entry.size()||result->bytes+copied>PackageStore::MaxPackageBytes) return reject(error,"Package changed while being copied.");
            if(!destination.isEmpty()&&output.write(bytes)!=bytes.size()) return reject(error,"Cannot write package file: "+name);
            hash.addData(bytes);
            if(progress) progress(result->bytes+copied);
        }
        if(copied!=entry.size()) return reject(error,"Package changed while being copied.");
        if(!destination.isEmpty()&&!output.flush()) return reject(error,"Cannot flush package file: "+name);
        result->bytes+=copied;
        result->files.append(QJsonObject{{"path",name},{"bytes",copied},{"sha256",QString::fromLatin1(hash.result().toHex())}});
    }
    return true;
}
QString digest(const Inventory &i) { return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(i.files).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()); }
bool packageMetadata(const QString &root,const PluginSources::RegisteredResource &expected,const PackagePolicy &policy,QJsonObject *metadata,QString *error) {
    QFile file(root+"/notepadnext-plugin.json");
    if(!file.open(QIODevice::ReadOnly)||file.size()>PluginSources::PluginMetadata::MaxMetadataBytes) return reject(error,"Missing or oversized package metadata.");
    PluginSources::SourceSnapshot parsed;
    if(!PluginSources::PluginMetadata::parse(file.readAll(),expected.repository,{},&parsed,error)) return false;
    if(parsed.isMarketplace()||parsed.resources.size()!=1||parsed.metadata["id"]!=expected.id||parsed.metadata["version"]!=expected.metadata["version"]) return reject(error,"Package ID/version does not match the selected registered resource.");
    // Metadata selected during review is the authority for permissions and UI.
    if(parsed.metadata!=expected.metadata) return reject(error,"Package metadata differs from the registered resource. Refresh and review the source first.");
    const auto engines=parsed.metadata["engines"].toObject();
    if(!SemVer::satisfies(policy.applicationVersion,engines["notepadnext"].toString(),error))return false;
    bool apiMatches=SemVer::satisfies(policy.apiVersion,engines["api"].toString(),error);
    for(const auto &version:policy.compatibleApiVersions)if(SemVer::satisfies(version,engines["api"].toString(),error))apiMatches=true;
    if(!apiMatches)return reject(error,"No supported native API version matches the plugin.");
    if(parsed.metadata["runtime"].toObject()["kind"]=="python"&&!SemVer::satisfies("0.1.0",engines["api"].toString(),error))return reject(error,"The Python prototype currently supports API 0.1 only.");
    for(const auto &cap:parsed.metadata["capabilities"].toObject()["required"].toArray()) if(!policy.capabilities.contains(cap.toString())) return reject(error,"Required capability is unavailable: "+cap.toString());
    const auto entry=parsed.metadata["runtime"].toObject()["entry"].toString();
    if(!PackageStore::isSafeRelativePath(entry)||!QFileInfo(root+'/'+entry).isFile()) return reject(error,"The package runtime entry is not a regular file.");
    const QFileInfo license(root+"/LICENSE");
    if(!license.isFile()||license.size()<1||license.size()>1024*1024)return reject(error,"The package must contain a nonempty LICENSE file within 1 MiB.");
    for(const auto &command:parsed.metadata.value("contributes").toObject().value("commands").toArray()) {
        const auto icon=command.toObject()["icon"].toString();
        if(icon.isEmpty()) continue;
        QFile image(root+'/'+icon); QImageReader reader(&image,"png");
        if(!PackageStore::isSafeRelativePath(icon)||!image.open(QIODevice::ReadOnly)||image.size()>1024*1024||reader.size().isEmpty()||reader.size().width()>512||reader.size().height()>512||reader.read().isNull()) return reject(error,"Invalid package PNG icon: "+icon);
    }
    *metadata=parsed.metadata; return true;
}
}
bool PackageStore::prepareDirectory(const QString &directory,const PluginSources::RegisteredResource &expected,const PackagePolicy &policy,PreparedPackage *result,QString *error,const std::atomic_bool *cancel,const std::function<void(qint64)> &progress,int lockWaitMs) const {
    const QFileInfo source(directory),store(path);
    if(source.isSymLink()||!source.isDir()||store.isSymLink()) return reject(error,"Package/store root must be a real directory.");
    if(!QDir().mkpath(path)) return reject(error,"Cannot create package store.");
    const auto canonical=QFileInfo(path).canonicalFilePath(),input=source.canonicalFilePath();
    if(canonical==input||canonical.startsWith(input+'/')) return reject(error,"Package source must not contain the installation store.");
    QLockFile lock(canonical+"/.store.lock");
    lock.setStaleLockTime(0);
    if(!PackageLease::lockStore(lock,lockWaitMs,cancel))return cancelled(cancel,error)?false:reject(error,"Another installation transaction is in progress.");
    QTemporaryDir stage(canonical+"/.stage-XXXXXX");
    if(!stage.isValid()) return reject(error,"Cannot create package staging directory.");
    Inventory content;
    if(!inventory(input,{},stage.path(),&content,error,cancel,progress)) return false;
    PreparedPackage p; p.repository=expected.repository; p.bytes=content.bytes;p.files=content.files.size();
    if(!packageMetadata(stage.path(),expected,policy,&p.metadata,error)) return false;
    p.digest=digest(content); p.root=canonical+'/'+p.digest;
    QSaveFile receipt(stage.path()+"/.nnp-receipt.json");
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"digest",p.digest},{"files",content.files}}).toJson(QJsonDocument::Compact);
    if(!receipt.open(QIODevice::WriteOnly)||receipt.write(bytes)!=bytes.size()||!receipt.commit()) return reject(error,"Cannot save package verification receipt.");
    if(cancelled(cancel,error)) return false;
    if(QFileInfo::exists(p.root)) { if(!verify(p,error,cancel)) return false; }
    else if(!QDir().rename(stage.path(),p.root)) return reject(error,"Cannot atomically publish the staged package.");
    else stage.setAutoRemove(false);
    p.lease=PackageLease::acquireLocked(canonical,p.digest,error);
    if(!p.lease)return false;
    if(result)*result=p; return true;
}
bool PackageStore::verify(const PreparedPackage &p,QString *error,const std::atomic_bool *cancel) const {
    const auto canonical=QFileInfo(path).canonicalFilePath();
    static const QRegularExpression validDigest("^[a-f0-9]{64}$");
    if(canonical.isEmpty()||!validDigest.match(p.digest).hasMatch()||p.root!=canonical+'/'+p.digest||QFileInfo(p.root).isSymLink()||QFileInfo(p.root).canonicalFilePath()!=p.root) return reject(error,"Invalid installed package location.");
    Inventory content;
    if(!inventory(p.root,{}, {},&content,error,cancel,{})) return false;
    if(digest(content)!=p.digest||content.bytes!=p.bytes||content.files.size()!=p.files) return reject(error,"Installed package integrity verification failed.");
    QFile file(p.root+"/notepadnext-plugin.json"); PluginSources::SourceSnapshot parsed;
    if(!file.open(QIODevice::ReadOnly)||file.size()>PluginSources::PluginMetadata::MaxMetadataBytes||!PluginSources::PluginMetadata::parse(file.readAll(),p.repository,{},&parsed,error)||parsed.metadata!=p.metadata) return reject(error,"Installed package metadata verification failed.");
    return true;
}
bool PackageStore::prepareZip(const QString &archive,const PluginSources::RegisteredResource &expected,const PackagePolicy &policy,PreparedPackage *result,QString *error,const std::atomic_bool *cancel,const std::function<void(qint64)> &progress,int lockWaitMs) const {
    // Extraction occurs before prepareDirectory takes the store lock. Pin
    // staging and published-but-not-yet-indexed packages across that interval.
    const auto lease=PackageLease::acquire(path,{},error,lockWaitMs,cancel);
    if(!lease)return false;
    QTemporaryDir unpack(QFileInfo(path).canonicalFilePath()+"/.unpack-XXXXXX");
    if(!unpack.isValid())return reject(error,"Cannot create archive staging directory.");
    if(!ZipExtractor::extract(archive,unpack.path(),error,cancel,progress))return false;
    return prepareDirectory(unpack.path(),expected,policy,result,error,cancel,progress,lockWaitMs);
}
}
