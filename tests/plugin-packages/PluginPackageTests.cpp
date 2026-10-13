#include "../PluginTestFilesystem.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageStore.h"
#include "PackageLease.h"
#include "PackageCollectionService.h"
#include "InstalledPluginRegistry.h"
#include "SemVer.h"
#include "ZipExtractor.h"
#include "RegisteredResourceRegistry.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <zlib.h>
using namespace PluginPackages;
using namespace PluginSources;
namespace {
void write(const QString &path,const QByteArray &bytes) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) qFatal("mkdir failed");
    QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())qFatal("write failed");
}
QJsonObject manifest() {
    return {{"manifestVersion",1},{"id","tests.sample"},{"name","Sample"},{"version","1.2.3"},{"list",false},
            {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.3.0"}}},
            {"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.js"}}},
            {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1"}},{"optional",QJsonArray{}}}},
            {"activationEvents",QJsonArray{"onCommand:tests.sample.open"}},
            {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id","tests.sample.open"},{"title","Open"}}}}}}};
}
SourceSnapshot source(const QJsonObject &m=manifest()) {
    SourceSnapshot s;QString e;
    if(!PluginMetadata::parse(QJsonDocument(m).toJson(),"https://github.com/tests/sample",{},&s,&e))qFatal("%s",qPrintable(e));
    s.commit=QString(40,'a');return s;
}
void package(const QString &root,const QJsonObject &m=manifest()) {
    write(root+"/notepadnext-plugin.json",QJsonDocument(m).toJson());
    write(root+"/dist/index.js","module.exports.activate = () => {};\n");
    write(root+"/LICENSE","Sample test license\n");
}
PackagePolicy policy() {PackagePolicy p;p.capabilities={"commands@1"};return p;}
void le16(QByteArray &b,quint16 n){char bytes[2];qToLittleEndian(n,bytes);b.append(bytes,2);}
void le32(QByteArray &b,quint32 n){char bytes[4];qToLittleEndian(n,bytes);b.append(bytes,4);}
struct ZipFile {QByteArray name,text;quint16 method=0,flags=0x800;quint32 mode=0100644;};
QByteArray zip(const QVector<ZipFile> &files) {
    QByteArray local,central;
    for(const auto &f:files) {
        QByteArray payload=f.text;if(f.method==8){const auto compressed=qCompress(f.text,9);payload=compressed.mid(6,compressed.size()-10);}
        const auto crc=quint32(crc32(0,reinterpret_cast<const Bytef*>(f.text.constData()),uInt(f.text.size())));const auto offset=local.size();
        le32(local,0x04034b50);le16(local,20);le16(local,f.flags);le16(local,f.method);le16(local,0);le16(local,0);
        le32(local,(f.flags&8)?0:crc);le32(local,(f.flags&8)?0:payload.size());le32(local,(f.flags&8)?0:f.text.size());le16(local,f.name.size());le16(local,0);local+=f.name;local+=payload;
        if(f.flags&8){le32(local,0x08074b50);le32(local,crc);le32(local,payload.size());le32(local,f.text.size());}
        le32(central,0x02014b50);le16(central,(3<<8)|20);le16(central,20);le16(central,f.flags);le16(central,f.method);le16(central,0);le16(central,0);
        le32(central,crc);le32(central,payload.size());le32(central,f.text.size());le16(central,f.name.size());le16(central,0);le16(central,0);le16(central,0);le16(central,0);le32(central,f.mode<<16);le32(central,offset);central+=f.name;
    }
    QByteArray end;le32(end,0x06054b50);le16(end,0);le16(end,0);le16(end,files.size());le16(end,files.size());le32(end,central.size());le32(end,local.size());le16(end,0);return local+central+end;
}
}
class PluginPackageTests:public QObject {
    Q_OBJECT
private slots:
    void versionsAndRanges();
    void immutableInstallationAndPersistence();
    void updateTransactionsAndRecovery();
    void registrationAndInstallationStaySeparate();
    void metadataAndCompatibilityChecks();
    void unsafePathsAndLinks();
    void budgetsCancellationAndIntegrity();
    void icons();
    void failedIndexWritesAndCorruptStorage();
    void zipRoundtripAndPackage();
    void unsafeZip_data();
    void unsafeZip();
    void corruptZipAndBudgets();
    void collectionReferencesAndPreparedLeases();
    void collectionPreservesUnsafeAndCorruptStorage();
    void collectionCrossProcessAndCrashRecovery();
    void collectionRunsOnWorker();
    void collectionBatchesAndInvalidActiveLease();
    void runtimeIdentityPinsAndPidReuse();
};
void PluginPackageTests::collectionReferencesAndPreparedLeases() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;
    QVector<PreparedPackage> versions;
    for(const auto &version:QStringList{"1.2.3","1.2.4","1.2.5"}) {
        auto m=manifest();m["version"]=version;const auto input=t.path()+'/'+version;package(input,m);PreparedPackage p;
        QVERIFY2(store.prepareDirectory(input,source(m).resources[0],policy(),&p,&error),qPrintable(error));versions.append(p);
    }
    InstalledPluginRegistry index(path,root);QVERIFY(index.load(&error));QVERIFY(index.add(versions[0],{},{{"kind","local"}},&error));
    QVERIFY(index.beginUpdate(versions[1],{},{{"kind","local"}},versions[0].digest,&error));QVERIFY(index.confirmUpdate("tests.sample",&error));
    QVERIFY(index.beginUpdate(versions[2],{},{{"kind","local"}},versions[1].digest,&error,true));
    QVERIFY(!index.plugin("tests.sample").package.lease);
    for(auto &p:versions)p.lease.reset();
    // Current candidate, its previous version and interrupted-history backup
    // are all durable roots even when their processes are not running.
    auto result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,0);
    for(const auto &p:versions)QVERIFY(QFileInfo::exists(p.root));
    QVERIFY(index.abortUpdate("tests.sample",&error));result=store.collect(path);QCOMPARE(result.packages,1);QVERIFY(!QFileInfo::exists(versions[2].root));
    auto m=manifest();m["version"]="1.2.6";package(t.path()+"/uncommitted",m);PreparedPackage prepared;
    QVERIFY(store.prepareDirectory(t.path()+"/uncommitted",source(m).resources[0],policy(),&prepared,&error));
    QCOMPARE(store.collect(path).packages,0);QVERIFY(QFileInfo::exists(prepared.root));
    prepared.lease.reset();QCOMPARE(store.collect(path).packages,1);QVERIFY(!QFileInfo::exists(prepared.root));
    auto running=PackageLease::acquire(root,versions[1].digest,&error);QVERIFY(running);
    QLockFile publication(root+"/.store.lock");publication.setStaleLockTime(0);QVERIFY(publication.tryLock(0));
    // A collector/preparer holding the publication lock must not make an
    // enable/disable/uninstall operation fail with a spurious busy error.
    QVERIFY2(index.setEnabled("tests.sample",true,&error),qPrintable(error));QVERIFY(index.setEnabled("tests.sample",false,&error));
    QVERIFY2(index.remove("tests.sample",&error),qPrintable(error));publication.unlock();
    result=store.collect(path);QCOMPARE(result.packages,1);QVERIFY(!QFileInfo::exists(versions[0].root));QVERIFY(QFileInfo::exists(versions[1].root));
    running.reset();QCOMPARE(store.collect(path).packages,1);QVERIFY(!QFileInfo::exists(versions[1].root));
    // A prepared object cannot reintroduce a collected directory into the
    // index, including a transaction whose initial disk snapshot is stale.
    QVERIFY(!index.add(versions[1],{},{{"kind","local"}},&error));
}
void PluginPackageTests::collectionPreservesUnsafeAndCorruptStorage() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;PreparedPackage p;
    package(t.path()+"/input");QVERIFY(store.prepareDirectory(t.path()+"/input",source().resources[0],policy(),&p,&error));p.lease.reset();
    InstalledPluginRegistry index(path,root);QVERIFY(index.load(&error));
    auto activity=PackageLease::acquire(root,{},&error);QVERIFY(activity);
    write(root+"/.stage-abc123/partial","stage");write(root+"/.unpack-abc123/partial","unpack");write(root+"/.download-abc123/plugin.zip","download");
    write(root+"/.trash-12345678-1234-1234-1234-123456789abc/partial","trash");
    auto result=store.collect(path);QVERIFY(result.deferred);QVERIFY(QFileInfo::exists(root+"/.stage-abc123/partial"));QVERIFY(QFileInfo::exists(p.root));activity.reset();
    write(path,"{broken index");result=store.collect(path);QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(p.root));QVERIFY(QFileInfo::exists(root+"/.download-abc123/plugin.zip"));
    QFile corrupted(path);QVERIFY(corrupted.open(QIODevice::ReadOnly));QCOMPARE(corrupted.readAll(),QByteArray("{broken index"));corrupted.close();QVERIFY(corrupted.remove());
    write(root+"/unknown/keep","user file");write(root+'/'+QString(64,'f')+"/keep","unowned hash-shaped directory");write(t.path()+"/outside/keep","outside");
    QVERIFY(PluginTest::link(t.path()+"/outside",root+"/.stage-link12"));
    // A corrupted orphan remains available for inspection; collection must
    // not follow its newly introduced link into another filesystem tree.
    QVERIFY(PluginTest::link(t.path()+"/outside",p.root+"/unexpected-link"));
    std::atomic_bool cancel=true;result=store.collect(path,&cancel);QVERIFY(result.cancelled);QVERIFY(QFileInfo::exists(root+"/.stage-abc123"));
    QLockFile storeLock(root+"/.store.lock");storeLock.setStaleLockTime(0);QVERIFY(storeLock.tryLock(0));QVERIFY(store.collect(path).deferred);storeLock.unlock();
    result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.temporaryDirectories,4);QCOMPARE(result.packages,0);QCOMPARE(result.preserved.size(),3);
    QVERIFY(QFileInfo::exists(root+"/unknown/keep"));QVERIFY(QFileInfo::exists(root+'/'+QString(64,'f')+"/keep"));QVERIFY(QFileInfo::exists(t.path()+"/outside/keep"));QVERIFY(QFileInfo::exists(p.root));
    QVERIFY(PluginTest::removeLink(p.root+"/unexpected-link"));QCOMPARE(store.collect(path).packages,1);
}
void PluginPackageTests::collectionCrossProcessAndCrashRecovery() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;PreparedPackage p;
    package(t.path()+"/input");QVERIFY(store.prepareDirectory(t.path()+"/input",source().resources[0],policy(),&p,&error));p.lease.reset();
    const auto probe=QCoreApplication::applicationDirPath()+"/PackageLeaseProbe";QProcess owner;owner.start(probe,{root,p.digest,"hold"});
    QVERIFY(owner.waitForStarted(3000));QVERIFY(owner.waitForReadyRead(3000));QCOMPARE(owner.readAllStandardOutput(),QByteArray("ready\n"));
    auto result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,0);QVERIFY(QFileInfo::exists(p.root));
    owner.write("release\n");QVERIFY(owner.waitForFinished(3000));QCOMPARE(owner.exitCode(),0);
    // A whole-store owner dies without cleanup, like an interrupted download.
    QProcess interrupted;interrupted.start(probe,{root,QString(),"crash"});QVERIFY(interrupted.waitForFinished(3000));QCOMPARE(interrupted.exitCode(),0);
    write(root+"/.download-abc123/plugin.zip","partial download");
    result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.staleLeases,1);QCOMPARE(result.packages,1);QCOMPARE(result.temporaryDirectories,1);QVERIFY(!QFileInfo::exists(p.root));
    QCOMPARE(QDir(root+"/.leases").entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
}
void PluginPackageTests::collectionRunsOnWorker() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;PreparedPackage p;
    package(t.path()+"/input");QVERIFY(store.prepareDirectory(t.path()+"/input",source().resources[0],policy(),&p,&error));p.lease.reset();
    InstalledPluginRegistry index(path,root);QVERIFY(index.load(&error));PackageCollectionService service(&index);QSignalSpy finished(&service,&PackageCollectionService::finished);
    int ticks=0;QTimer timer;timer.setInterval(5);connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start();
    service.request();service.request();QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,3000);QVERIFY(service.isIdle());QVERIFY(ticks>0);
    const auto result=qvariant_cast<CollectionResult>(finished[0][0]);QVERIFY(result.error.isEmpty());QCOMPARE(result.packages,1);QVERIFY(!QFileInfo::exists(p.root));
    service.shutdown();service.request();QTest::qWait(300);QCOMPARE(finished.size(),1);
}
void PluginPackageTests::collectionBatchesAndInvalidActiveLease() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;
    auto activity=PackageLease::acquire(root,{},&error);QVERIFY(activity);
    for(int i=0;i<20;++i)write(root+QString("/.stage-%1/partial").arg(i,6,10,QChar('0')),"staged file");
    const auto leases=QDir(root+"/.leases").entryList(QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(leases.size(),1);
    const auto metadata=root+"/.leases/"+leases[0]+"/metadata.json";write(metadata,"{corrupt active lease");
    auto result=store.collect(path);QVERIFY(!result.error.isEmpty());QCOMPARE(result.temporaryDirectories,0);QVERIFY(QFileInfo::exists(root+"/.stage-000000/partial"));
    activity.reset();
    int removed=0,passes=0;
    do {result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(result.temporaryDirectories<=8);removed+=result.temporaryDirectories;++passes;}while(result.more&&passes<8);
    QCOMPARE(removed,20);QCOMPARE(passes,3);QCOMPARE(QDir(root).entryList({".stage-*"},QDir::Dirs|QDir::Hidden).size(),0);
}
void PluginPackageTests::runtimeIdentityPinsAndPidReuse() {
    QTemporaryDir t;const auto root=t.path()+"/store",path=t.path()+"/installed.json";PackageStore store(root);QString error;PreparedPackage p;
    package(t.path()+"/input");QVERIFY(store.prepareDirectory(t.path()+"/input",source().resources[0],policy(),&p,&error));p.lease.reset();
    QProcess runtime;runtime.start(QCoreApplication::applicationDirPath()+"/PackageLeaseProbe",{root,p.digest,"linger"});
    QVERIFY(runtime.waitForStarted(3000));QVERIFY(runtime.waitForReadyRead(3000));QCOMPARE(runtime.readAllStandardOutput(),QByteArray("ready\n"));
    ProcessIdentity identity;QVERIFY2(ProcessIdentity::capture(runtime.processId(),&identity,&error),qPrintable(error));QCOMPARE(identity.state(),ProcessIdentity::State::Alive);
    ProcessIdentity parsed;QVERIFY(ProcessIdentity::parse(identity.json(),&parsed));QCOMPARE(parsed.state(),ProcessIdentity::State::Alive);
    auto reused=parsed;reused.start=identity.platform=="macos"?QString("1:0"):(identity.platform=="linux"?QString("00000000-0000-0000-0000-000000000000:1"):QString("1"));
    QCOMPARE(reused.state(),ProcessIdentity::State::Exited);
    auto malformed=identity.json();malformed["pid"]=0;QVERIFY(!ProcessIdentity::parse(malformed,&parsed));malformed=identity.json();malformed["extra"]=true;QVERIFY(!ProcessIdentity::parse(malformed,&parsed));
    auto lease=PackageLease::acquire(root,p.digest,&error);QVERIFY(lease);QVERIFY2(lease->pinRuntimeProcess(runtime.processId(),&error),qPrintable(error));QVERIFY(!lease->pinRuntimeProcess(runtime.processId(),&error));
    const auto records=QDir(root+"/.leases").entryList(QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(records.size(),1);const auto record=root+"/.leases/"+records[0]+"/metadata.json";
    QFile f(record);QVERIFY(f.open(QIODevice::ReadOnly));const auto saved=f.readAll();f.close();
    lease.reset();auto result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,0);QVERIFY(QFileInfo::exists(p.root));
    // An unsupported identity/probe outcome is uncertainty, not proof that
    // the original runtime has exited. Keep the package until it is known.
    auto unknown=QJsonDocument::fromJson(saved).object();auto foreign=identity.json();foreign["platform"]=identity.platform=="windows"?"macos":"windows";foreign["start"]=identity.platform=="windows"?"1:0":"1";unknown["process"]=foreign;
    write(record,QJsonDocument(unknown).toJson());result=store.collect(path);QVERIFY(result.error.isEmpty());QCOMPARE(result.packages,0);
    unknown["process"]=QJsonObject{{"pid",true}};write(record,QJsonDocument(unknown).toJson());result=store.collect(path);QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(p.root));
    write(record,saved);runtime.kill();QVERIFY(runtime.waitForFinished(3000));QCOMPARE(identity.state(),ProcessIdentity::State::Exited);
    result=store.collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.staleLeases,1);QCOMPARE(result.packages,1);QVERIFY(!QFileInfo::exists(p.root));
}
void PluginPackageTests::versionsAndRanges() {
    const QStringList ordered={"1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta","1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.1","1.0.0"};
    for(qsizetype i=0;i<ordered.size()-1;++i){SemVer a,b;QVERIFY(SemVer::parse(ordered[i],&a));QVERIFY(SemVer::parse(ordered[i+1],&b));QVERIFY(a.compare(b)<0);}
    for(const auto &s:QStringList{"01.2.3","1.2","1.2.3-01","1.2.3+","1.2.3-","1.2.3-a..b"})QVERIFY(!SemVer::parse(s,nullptr));
    SemVer a,b;QVERIFY(SemVer::parse("1.2.3+001",&a));QVERIFY(SemVer::parse("1.2.3+002",&b));QCOMPARE(a.compare(b),0);
    QString e;
    QVERIFY(SemVer::satisfies("0.15.0",">=0.15.0 <0.16.0",&e));
    QVERIFY(SemVer::satisfies("0.1.2","^0.1.0",&e));QVERIFY(!SemVer::satisfies("0.2.0","^0.1.0",&e));
    QVERIFY(SemVer::satisfies("0.0.3","^0.0.3",&e));QVERIFY(!SemVer::satisfies("0.0.4","^0.0.3",&e));
    QVERIFY(SemVer::satisfies("1.2.9","~1.2.3",&e));QVERIFY(!SemVer::satisfies("1.3.0","~1.2.3",&e));
    QVERIFY(SemVer::satisfies("2.1.0","^1.0.0 || >=2.0.0 <3.0.0",&e));
    QVERIFY(!SemVer::satisfies("2.1.0","^1.0.0 || broken",&e));
    QVERIFY(!SemVer::satisfies("1.2.3-beta","*",&e));QVERIFY(SemVer::satisfies("1.2.3-beta",">=1.2.3-alpha <1.2.3",&e));
    QVERIFY(!SemVer::satisfies("1.2.3","^1.2",&e));
}
void PluginPackageTests::immutableInstallationAndPersistence() {
    QTemporaryDir t;const auto input=t.path()+"/input",store=t.path()+"/store";package(input);
    PackageStore s(store);PreparedPackage p;QString e;
    QVERIFY2(s.prepareDirectory(input,source().resources[0],policy(),&p,&e),qPrintable(e));
    QVERIFY(s.verify(p,&e));QCOMPARE(p.files,4);QVERIFY(p.bytes>0);
    PreparedPackage again;QVERIFY(s.prepareDirectory(input,source().resources[0],policy(),&again,&e));QCOMPARE(again.root,p.root);
    InstalledPluginRegistry index(t.path()+"/installed.json",store);QVERIFY(index.load(&e));QSignalSpy changed(&index,&InstalledPluginRegistry::changed);
    QVERIFY2(index.add(p,source().resources[0].sourceIds,{{"kind","local"}},&e),qPrintable(e));QCOMPARE(changed.size(),1);
    QVERIFY(!index.plugin("tests.sample").enabled);QVERIFY(!index.add(p,{}, {},&e));
    QVERIFY(index.setEnabled("tests.sample",true,&e));
    InstalledPluginRegistry reloaded(t.path()+"/installed.json",store);QVERIFY(reloaded.load(&e));QCOMPARE(reloaded.plugins().size(),1);QVERIFY(reloaded.plugin("tests.sample").enabled);
    QCOMPARE(reloaded.plugin("tests.sample").package.digest,p.digest);QVERIFY(reloaded.remove("tests.sample",&e));QVERIFY(reloaded.plugins().isEmpty());QVERIFY(QFileInfo::exists(p.root));
    // Uninstall/reinstall can reuse the immutable package without executing it.
    QVERIFY(reloaded.add(p,{},{{"kind","local"}},&e));QVERIFY(!reloaded.plugin("tests.sample").enabled);
}
void PluginPackageTests::updateTransactionsAndRecovery() {
    QTemporaryDir t;const auto store=t.path()+"/store",path=t.path()+"/installed.json";QString e;PackageStore packages(store);
    auto m=manifest();package(t.path()+"/v1",m);PreparedPackage v1,v2;
    QVERIFY(packages.prepareDirectory(t.path()+"/v1",source(m).resources[0],policy(),&v1,&e));
    m["version"]="1.2.4";package(t.path()+"/v2",m);
    QVERIFY(packages.prepareDirectory(t.path()+"/v2",source(m).resources[0],policy(),&v2,&e));
    InstalledPluginRegistry index(path,store);QVERIFY(index.load(&e));QVERIFY(index.add(v1,{"old-source"},{{"kind","local"}},&e));QVERIFY(index.setEnabled("tests.sample",true,&e));
    QVERIFY(!index.beginUpdate(v2,{}, {},QString(64,'a'),&e));QCOMPARE(index.plugin("tests.sample").package.digest,v1.digest);
    QVERIFY(index.beginUpdate(v2,{"new-source"},{{"kind","github-release"}},v1.digest,&e));
    auto p=index.plugin("tests.sample");QVERIFY(p.enabled);QVERIFY(p.updatePending);QVERIFY(p.previous);QCOMPARE(p.previous->package.digest,v1.digest);
    QVERIFY(!index.setEnabled(p.id(),false,&e));QVERIFY(!index.remove(p.id(),&e));QVERIFY(!index.beginUpdate(v2,{}, {},v2.digest,&e));
    QVERIFY(index.confirmUpdate(p.id(),&e));QVERIFY(!index.plugin(p.id()).updatePending);QVERIFY(index.plugin(p.id()).previous);
    InstalledPluginRegistry confirmed(path,store);QVERIFY(confirmed.load(&e));QCOMPARE(confirmed.plugin(p.id()).package.digest,v2.digest);QVERIFY(confirmed.plugin(p.id()).enabled);
    QVERIFY(confirmed.beginRollback(p.id(),v2.digest,&e));QCOMPARE(confirmed.plugin(p.id()).package.digest,v1.digest);QVERIFY(confirmed.abortUpdate(p.id(),&e));QCOMPARE(confirmed.plugin(p.id()).package.digest,v2.digest);
    QVERIFY(confirmed.beginRollback(p.id(),v2.digest,&e));QVERIFY(confirmed.confirmUpdate(p.id(),&e));QCOMPARE(confirmed.plugin(p.id()).package.digest,v1.digest);
    QVERIFY(!confirmed.beginUpdate(v1,{}, {},v1.digest,&e));
    QVERIFY(confirmed.beginUpdate(v2,{"new-source"},{{"kind","github-release"}},v1.digest,&e));
    // A fresh process observes the atomic pending marker and rolls back before
    // runtime activation. A second load proves the recovery was persisted.
    InstalledPluginRegistry recovery(path,store);QVERIFY2(recovery.load(&e),qPrintable(e));
    QCOMPARE(recovery.plugin(p.id()).package.digest,v1.digest);QVERIFY(recovery.plugin(p.id()).enabled);QVERIFY(!recovery.plugin(p.id()).updatePending);
    QCOMPARE(recovery.plugin(p.id()).sourceIds,QStringList{"old-source"});
    InstalledPluginRegistry twice(path,store);QVERIFY(twice.load(&e));QCOMPARE(twice.plugin(p.id()).package.digest,v1.digest);
    QVERIFY(twice.setEnabled(p.id(),false,&e));QVERIFY(twice.beginUpdate(v2,{}, {},v1.digest,&e));QVERIFY(twice.confirmUpdate(p.id(),&e));QVERIFY(!twice.plugin(p.id()).enabled);
    QVERIFY(twice.beginRollback(p.id(),v2.digest,&e,true));QVERIFY(twice.plugin(p.id()).validationDeferred);
    InstalledPluginRegistry deferred(path,store);QVERIFY(deferred.load(&e));QVERIFY(deferred.plugin(p.id()).validationDeferred);QCOMPARE(deferred.plugin(p.id()).package.digest,v1.digest);QVERIFY(!deferred.plugin(p.id()).enabled);
    QVERIFY(deferred.setEnabled(p.id(),true,&e));QVERIFY(deferred.startValidation(p.id(),&e));QVERIFY(!deferred.plugin(p.id()).validationDeferred);
    InstalledPluginRegistry interrupted(path,store);QVERIFY(interrupted.load(&e));QCOMPARE(interrupted.plugin(p.id()).package.digest,v2.digest);QVERIFY(interrupted.plugin(p.id()).enabled);
    // Optimistic concurrency protects a prepared update against another app.
    QVERIFY(!recovery.beginUpdate(v2,{}, {},v1.digest,&e));QVERIFY(e.contains("another process"));
    QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));auto object=QJsonDocument::fromJson(f.readAll()).object();f.close();
    auto array=object["plugins"].toArray();auto record=array[0].toObject();auto previous=record["previous"].toObject();previous["previous"]=previous;record["previous"]=previous;array[0]=record;object["plugins"]=array;
    const auto corrupt=QJsonDocument(object).toJson();write(path,corrupt);InstalledPluginRegistry damaged(path,store);QVERIFY(!damaged.load(&e));QVERIFY(!damaged.remove(p.id(),&e));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),corrupt);
}
void PluginPackageTests::registrationAndInstallationStaySeparate() {
    QTemporaryDir t;QString e;RegisteredResourceRegistry resources(t.path()+"/sources.json");QVERIFY(resources.load(&e));
    InstalledPluginRegistry installed(t.path()+"/installed.json",t.path()+"/store");QVERIFY(installed.load(&e));
    const auto snapshot=source();QVERIFY(resources.registerSource(snapshot,&e));QVERIFY(installed.plugins().isEmpty());QVERIFY(!QFileInfo::exists(t.path()+"/store"));
    package(t.path()+"/input");PreparedPackage p;QVERIFY(PackageStore(t.path()+"/store").prepareDirectory(t.path()+"/input",resources.resources()[0],policy(),&p,&e));
    QVERIFY(installed.add(p,resources.resources()[0].sourceIds,{{"kind","local"}},&e));QVERIFY(resources.removeSource(snapshot.key(),&e));
    QVERIFY(resources.resources().isEmpty());QCOMPARE(installed.plugins().size(),1);QCOMPARE(installed.plugins()[0].package.repository,snapshot.repository);
    QVERIFY(resources.registerSource(snapshot,&e));QVERIFY(installed.remove(p.metadata["id"].toString(),&e));QCOMPARE(resources.resources().size(),1);
}
void PluginPackageTests::metadataAndCompatibilityChecks() {
    QTemporaryDir t;const auto input=t.path()+"/input";package(input);PackageStore s(t.path()+"/store");PreparedPackage p;QString e;
    auto m=manifest();m["capabilities"]=QJsonObject{{"required",QJsonArray{"documents.write@1"}},{"optional",QJsonArray{}}};package(input,m);
    QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));QVERIFY(e.contains("differs"));
    package(input);auto incompatible=policy();incompatible.apiVersion="0.2.0";QVERIFY(!s.prepareDirectory(input,source().resources[0],incompatible,&p,&e));
    incompatible=policy();incompatible.capabilities.clear();QVERIFY(!s.prepareDirectory(input,source().resources[0],incompatible,&p,&e));
    QVERIFY(QFile::remove(input+"/dist/index.js"));QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));
    m=manifest();m["version"]="1.2.4";package(input,m);QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));
}
void PluginPackageTests::unsafePathsAndLinks() {
    QTemporaryDir t;const auto input=t.path()+"/input";package(input);PackageStore s(t.path()+"/store");PreparedPackage p;QString e;
    for(const auto &name:QStringList{"bad:name","CON.txt","trailing."})QVERIFY(!PackageStore::isSafeRelativePath(name));
    for(const auto &name:QStringList{"bad:name","CON.txt","trailing.",".nnp-receipt.json","addon.node"}) {
#ifdef Q_OS_WIN
        // Win32 cannot create these literal names; portable-name and ZIP
        // validation above/below still exercise their rejection.
        if(name=="CON.txt"||name=="trailing.")continue;
#endif
        write(input+'/'+name,"x");QVERIFY2(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e),qPrintable(name));QVERIFY(QFile::remove(input+'/'+name));
    }
    write(t.path()+"/outside","outside");QVERIFY(PluginTest::link(t.path()+"/outside",input+"/linked"));
    QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));QVERIFY(QFile::remove(input+"/linked"));
    QVERIFY(PluginTest::link(input,t.path()+"/linked-root"));QVERIFY(!s.prepareDirectory(t.path()+"/linked-root",source().resources[0],policy(),&p,&e));
    // Case aliases must be rejected also on case-sensitive filesystems.
    write(input+"/a","a");write(input+"/A","b");
    if(QDir(input).entryList().contains("a")&&QDir(input).entryList().contains("A"))QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));
}
void PluginPackageTests::budgetsCancellationAndIntegrity() {
    QTemporaryDir t;const auto input=t.path()+"/input";package(input);PackageStore s(t.path()+"/store");PreparedPackage p;QString e;
    QFile large(input+"/large");QVERIFY(large.open(QIODevice::WriteOnly));QVERIFY(large.resize(PackageStore::MaxFileBytes+1));large.close();
    QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));QVERIFY(QFile::remove(large.fileName()));
    std::atomic_bool cancel=true;QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e,&cancel));QVERIFY(e.contains("cancelled"));
    cancel=false;QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e,&cancel,[&](qint64){cancel=true;}));
    QVERIFY(QDir(t.path()+"/store").entryList({".stage-*"},QDir::Dirs|QDir::Hidden).isEmpty());
    QVERIFY(s.prepareDirectory(input,source().resources[0],policy(),&p,&e));write(p.root+"/dist/index.js","tampered");QVERIFY(!s.verify(p,&e));
    QVERIFY(!s.prepareDirectory(input,source().resources[0],policy(),&p,&e));
}
void PluginPackageTests::icons() {
    QTemporaryDir t;const auto input=t.path()+"/input";auto m=manifest();auto contributes=m["contributes"].toObject();auto commands=contributes["commands"].toArray();auto command=commands[0].toObject();command["icon"]="icon.png";commands[0]=command;contributes["commands"]=commands;m["contributes"]=contributes;package(input,m);
    PackageStore s(t.path()+"/store");PreparedPackage p;QString e;const auto expected=source(m).resources[0];
    write(input+"/icon.png","not png");QVERIFY(!s.prepareDirectory(input,expected,policy(),&p,&e));
    QImage good(32,32,QImage::Format_ARGB32);good.fill(Qt::blue);QVERIFY(good.save(input+"/icon.png"));QVERIFY(s.prepareDirectory(input,expected,policy(),&p,&e));
    QImage huge(513,20,QImage::Format_ARGB32);huge.fill(Qt::blue);QVERIFY(huge.save(input+"/icon.png"));QVERIFY(!s.prepareDirectory(input,expected,policy(),&p,&e));
}
void PluginPackageTests::failedIndexWritesAndCorruptStorage() {
    QTemporaryDir t;QString e;package(t.path()+"/input");PreparedPackage p;QVERIFY(PackageStore(t.path()+"/store").prepareDirectory(t.path()+"/input",source().resources[0],policy(),&p,&e));
    write(t.path()+"/blocked","file");InstalledPluginRegistry blocked(t.path()+"/blocked/index.json",t.path()+"/store");QVERIFY(blocked.load(&e));QVERIFY(!blocked.add(p,{}, {},&e));QVERIFY(blocked.plugins().isEmpty());
    write(t.path()+"/corrupt.json","corrupt");InstalledPluginRegistry bad(t.path()+"/corrupt.json",t.path()+"/store");QVERIFY(!bad.load(&e));QVERIFY(!bad.add(p,{}, {},&e));QFile corrupt(t.path()+"/corrupt.json");QVERIFY(corrupt.open(QIODevice::ReadOnly));QCOMPARE(corrupt.readAll(),QByteArray("corrupt"));
    InstalledPluginRegistry first(t.path()+"/index.json",t.path()+"/store"),second(t.path()+"/index.json",t.path()+"/store");QVERIFY(first.load(&e));QVERIFY(second.load(&e));QVERIFY(first.add(p,{}, {},&e));QVERIFY(!second.add(p,{}, {},&e));QVERIFY(e.contains("another process"));QVERIFY(second.load(&e));QCOMPARE(second.plugins().size(),1);
}

void PluginPackageTests::zipRoundtripAndPackage() {
    QTemporaryDir t;QString e;const auto archive=t.path()+"/plugin.zip",root=t.path()+"/unpack";QVERIFY(QDir().mkpath(root));
    const auto metadata=QJsonDocument(manifest()).toJson();const auto entry=QByteArray("exports.activate=()=>{};\n");
    write(archive,zip({{"notepadnext-plugin.json",metadata,8},{"dist/",{},0,0x800,0040755},{"dist/index.js",entry,8,0x808},{"LICENSE","test"},{QStringLiteral("resources/日本語.txt").toUtf8(),QStringLiteral("😀中").toUtf8(),8}}));
    QVERIFY2(ZipExtractor::extract(archive,root,&e),qPrintable(e));QFile file(root+"/dist/index.js");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),entry);
    PreparedPackage p;PackageStore store(t.path()+"/store");QVERIFY2(store.prepareZip(archive,source().resources[0],policy(),&p,&e),qPrintable(e));QVERIFY(store.verify(p,&e));
    QVERIFY(QDir(t.path()+"/store").entryList({".unpack-*"},QDir::Dirs|QDir::Hidden).isEmpty());
    std::atomic_bool cancel=false;const auto second=t.path()+"/cancel-store";
    QVERIFY(!PackageStore(second).prepareZip(archive,source().resources[0],policy(),&p,&e,&cancel,[&](qint64){cancel=true;}));QVERIFY(e.contains("cancelled"));QVERIFY(QDir(second).entryList({".unpack-*"},QDir::Dirs|QDir::Hidden).isEmpty());
}
void PluginPackageTests::unsafeZip_data() {
    QTest::addColumn<QByteArray>("bytes");
    for(const auto &path:QList<QByteArray>{"../escape","/absolute","C:/drive","a\\b","a//b","./a","a/../b","CON.txt","aux","a.","a ","a:b","a?b",".nnp-receipt.json"})QTest::newRow(path.constData())<<zip({{path,"x"}});
    QTest::newRow("case aliases")<<zip({{"a","x"},{"A","y"}});
    QTest::newRow("duplicate file")<<zip({{"same","x"},{"same","y"}});
    QTest::newRow("symlink")<<zip({{"link","../outside",0,0x800,0120777}});
    QTest::newRow("device")<<zip({{"device","",0,0x800,0020600}});
    QTest::newRow("fifo")<<zip({{"pipe","",0,0x800,0010644}});
    QTest::newRow("encryption")<<zip({{"file","x",0,0x801}});
    QTest::newRow("compression method")<<zip({{"file","x",99}});
    QTest::newRow("invalid utf8")<<zip({{QByteArray("\xff.txt",5),"x"}});
    QTest::newRow("undeclared utf8")<<zip({{QStringLiteral("日本.txt").toUtf8(),"x",0,0}});
    QTest::newRow("directory data")<<zip({{"dir/","x",0,0x800,0040755}});
    QTest::newRow("file prefix")<<zip({{"a","x"},{"a/b","y"}});
    QTest::newRow("directory prefix")<<zip({{"a/b","y"},{"a","x"}});
    QTest::newRow("depth")<<zip({{QByteArray("a/").repeated(33)+"file","x"}});
    QTest::newRow("unicode aliases")<<zip({{QStringLiteral("é.txt").toUtf8(),"x"},{QStringLiteral("e\u0301.txt").toUtf8(),"y"}});
}
void PluginPackageTests::unsafeZip() {
    QFETCH(QByteArray,bytes);QTemporaryDir t;QString e;write(t.path()+"/plugin.zip",bytes);QVERIFY(QDir().mkpath(t.path()+"/out"));
    QVERIFY2(!ZipExtractor::extract(t.path()+"/plugin.zip",t.path()+"/out",&e),qPrintable(e));QVERIFY(!QFileInfo::exists(t.path()+"/escape"));
}
void PluginPackageTests::corruptZipAndBudgets() {
    const auto valid=zip({{"data.txt",QByteArray("payload ").repeated(100),8,0x808}});QTemporaryDir t;QString error;
    const auto check=[&](const QByteArray &bytes){QTemporaryDir output;write(t.path()+"/input.zip",bytes);return ZipExtractor::extract(t.path()+"/input.zip",output.path(),&error);};
    QVERIFY(check(valid));
    const auto central=valid.indexOf(QByteArray("PK\x01\x02",4)),descriptor=valid.indexOf(QByteArray("PK\x07\x08",4));QVERIFY(central>0);QVERIFY(descriptor>0);
    auto bad=valid;bad[30]='X';QVERIFY(!check(bad)); // different local name
    bad=valid;qToLittleEndian<quint32>(0,bad.data()+central+42); // first offset already zero
    qToLittleEndian<quint32>(1,bad.data()+central+42);QVERIFY(!check(bad));
    bad=valid;bad[descriptor+4]^=1;QVERIFY(!check(bad));
    bad=valid;bad[central+16]^=1;QVERIFY(!check(bad));
    bad=valid;bad[38]^=1;QVERIFY(!check(bad)); // corrupt deflate data
    QVERIFY(!check(valid.left(valid.size()-1)));QVERIFY(!check("prefix"+valid));QVERIFY(!check(valid+"hidden"));
    bad=valid;qToLittleEndian<quint16>(1,bad.data()+bad.size()-22+4);QVERIFY(!check(bad));
    bad=valid;qToLittleEndian<quint16>(45,bad.data()+central+6);QVERIFY(!check(bad));
    bad=valid;qToLittleEndian<quint32>(PackageStore::MaxFileBytes+1,bad.data()+central+24);QVERIFY(!check(bad));
    QVector<ZipFile> many;for(int i=0;i<PackageStore::MaxFiles+1;++i)many.append({QByteArray::number(i),{}});QVERIFY(!check(zip(many)));
    QFile huge(t.path()+"/huge.zip");QVERIFY(huge.open(QIODevice::WriteOnly));QVERIFY(huge.resize(ZipExtractor::MaxArchiveBytes+1));huge.close();QTemporaryDir output;QVERIFY(!ZipExtractor::extract(huge.fileName(),output.path(),&error));
}
QTEST_MAIN(PluginPackageTests)
#include "PluginPackageTests.moc"
