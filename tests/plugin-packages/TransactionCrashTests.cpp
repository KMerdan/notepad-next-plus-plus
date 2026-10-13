// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstalledPluginRegistry.h"
#include "ProcessIdentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
using namespace PluginPackages;
namespace {
constexpr auto PluginId="tests.transaction";
constexpr auto Repository="https://github.com/tests/transaction";
bool write(const QString &path,const QByteArray &bytes) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.flush();
}
QByteArray read(const QString &path) {
    QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
}
QJsonObject metadata(const QString &version) {
    return {{"manifestVersion",1},{"id",PluginId},{"name","Transaction fixture"},{"version",version},{"list",false},
        {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.3.0"}}},
        {"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.cjs"}}},
        {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1"}},{"optional",QJsonArray{}}}},
        {"activationEvents",QJsonArray{"onCommand:tests.transaction.open"}},
        {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id","tests.transaction.open"},{"title","Open"}}}}}}};
}
bool fixture(const QString &path,const QString &version,const QString &executionMarker) {
    const auto marker=QJsonDocument(QJsonArray{executionMarker}).toJson(QJsonDocument::Compact);
    return write(path+"/notepadnext-plugin.json",QJsonDocument(metadata(version)).toJson())
        &&write(path+"/LICENSE","Transaction fixture license\n")
        &&write(path+"/dist/index.cjs","require('node:fs').writeFileSync("+marker+"[0],'unexpected execution');throw Error('Package code must not execute.');\n");
}
QString probe() {
    auto path=QCoreApplication::applicationDirPath()+"/PackageTransactionProbe";
#ifdef Q_OS_WIN
    path+=".exe";
#endif
    return path;
}
QStringList arguments(const QString &phase,const QString &store,const QString &index,const QString &input) {
    return {phase,store,index,input,Repository};
}
}
class TransactionCrashTests:public QObject {
    Q_OBJECT
    QJsonArray results;
private slots:
    void processDeath_data() {
        QTest::addColumn<QString>("phase");QTest::addColumn<bool>("enabled");
        for(const auto &phase:QStringList{"copying","published","installed"})
            QTest::newRow(qPrintable(phase))<<phase<<false;
        for(const auto &phase:QStringList{"update-pending","update-deferred","validation-started","update-confirmed","rollback-pending","recovery-busy"})
            for(bool enabled:{false,true})QTest::newRow(qPrintable(phase+(enabled?"-enabled":"-disabled")))<<phase<<enabled;
    }
    void processDeath() {
        QFETCH(QString,phase);QFETCH(bool,enabled);
        QTemporaryDir temp(QDir::tempPath()+"/NotepadNext 安装中断-XXXXXX");QVERIFY(temp.isValid());
        const auto storePath=temp.path()+"/store",indexPath=temp.path()+"/installed.json";
        const auto input=temp.path()+"/candidate",executionMarker=temp.path()+"/executed";
        const bool install=phase=="copying"||phase=="published"||phase=="installed";
        PackageStore store(storePath);QString error;PackagePolicy policy;policy.capabilities={"commands@1"};
        if(!install) {
            InstalledPluginRegistry index(indexPath,storePath);QVERIFY(index.load(&error));
            for(const auto &version:QStringList{"1.2.2","1.2.3"}) {
                const auto path=temp.path()+"/"+version;QVERIFY(fixture(path,version,executionMarker));
                PluginSources::SourceSnapshot source;
                QVERIFY2(PluginSources::PluginMetadata::parse(QJsonDocument(metadata(version)).toJson(),Repository,{},&source,&error),qPrintable(error));
                PreparedPackage p;QVERIFY2(store.prepareDirectory(path,source.resources[0],policy,&p,&error),qPrintable(error));
                if(version=="1.2.2")QVERIFY(index.add(p,{"original-source"},{{"kind","local"}},&error));
                else {QVERIFY(index.beginUpdate(p,{"original-source"},{{"kind","local"}},index.plugin(PluginId).package.digest,&error));QVERIFY(index.confirmUpdate(PluginId,&error));}
            }
            QVERIFY(index.setEnabled(PluginId,enabled,&error));
        }
        QVERIFY(fixture(input,"1.2.4",executionMarker));
        // Progress parks inside the production copy loop after a partial file.
        QVERIFY(write(input+"/payload.bin",QByteArray(4*1024*1024,'p')));
        QProcess owner;
        const auto cleanup=qScopeGuard([&]{if(owner.state()!=QProcess::NotRunning){owner.kill();owner.waitForFinished(3000);}});
        owner.start(probe(),arguments(phase,storePath,indexPath,input));QVERIFY(owner.waitForStarted(3000));
        QByteArray ready;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{ready+=owner.readAllStandardOutput();return ready.endsWith('\n');})(),5000);
        const auto checkpoint=QJsonDocument::fromJson(ready).object();
        QVERIFY2(!checkpoint.contains("error"),ready.constData());QCOMPARE(checkpoint["phase"].toString(),phase);
        ProcessIdentity identity;QVERIFY2(ProcessIdentity::capture(owner.processId(),&identity,&error),qPrintable(error));
        QCOMPARE(identity.state(),ProcessIdentity::State::Alive);
        const auto stageDirectories=QDir(storePath).entryList({".stage-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot);
        if(phase=="copying") {
            QCOMPARE(stageDirectories.size(),1);QVERIFY(checkpoint["copiedBytes"].toDouble()>=64*1024);
            const QFileInfo partial(storePath+"/"+stageDirectories[0]+"/payload.bin");
            QVERIFY(partial.size()>0);QVERIFY(partial.size()<4*1024*1024);
            QVERIFY(!QFileInfo::exists(indexPath));QVERIFY(store.collect(indexPath).deferred);
            QVERIFY(QFileInfo::exists(storePath+"/"+stageDirectories[0]));
        } else if(phase=="published") {
            QVERIFY(!QFileInfo::exists(indexPath));
            auto live=store.collect(indexPath);QVERIFY2(live.error.isEmpty(),qPrintable(live.error));QCOMPARE(live.packages,0);
            QVERIFY(QFileInfo::exists(storePath+"/"+checkpoint["digest"].toString()));
        }
        owner.kill();QVERIFY(owner.waitForFinished(3000));QCOMPARE(owner.exitStatus(),QProcess::CrashExit);
        QTRY_COMPARE_WITH_TIMEOUT(identity.state(),ProcessIdentity::State::Exited,3000);
        const auto durableBeforeRecovery=read(indexPath);
        if(phase=="recovery-busy") {
            QLockFile lock(indexPath+".lock");lock.setStaleLockTime(0);QVERIFY(lock.tryLock(0));
            QProcess blocked;blocked.start(probe(),arguments("inspect",storePath,indexPath,input));QVERIFY(blocked.waitForFinished(5000));
            QCOMPARE(blocked.exitCode(),3);QVERIFY(blocked.readAllStandardOutput().contains("storage is busy"));
            QCOMPARE(read(indexPath),durableBeforeRecovery);
            InstalledPluginRegistry failed(indexPath,storePath);QVERIFY(!failed.load(&error));QVERIFY(!failed.loadError().isEmpty());
            QVERIFY(!failed.setEnabled(PluginId,!enabled,&error));QVERIFY(!failed.remove(PluginId,&error));
            QCOMPARE(read(indexPath),durableBeforeRecovery);lock.unlock();
        }
        // Recovery itself runs in a fresh native process, not just a new object.
        QProcess restarted;restarted.start(probe(),arguments("inspect",storePath,indexPath,input));QVERIFY(restarted.waitForFinished(5000));
        const auto raw=restarted.readAllStandardOutput();QVERIFY2(restarted.exitCode()==0,raw.constData());
        const auto recovered=QJsonDocument::fromJson(raw).object();QCOMPARE(recovered["verified"],QJsonValue(true));
        const auto plugins=recovered["plugins"].toArray();
        if(phase=="copying"||phase=="published")QVERIFY(plugins.isEmpty());
        else {
            QCOMPARE(plugins.size(),1);const auto p=plugins[0].toObject();
            const auto expected=phase=="installed"||phase=="update-confirmed"||phase=="update-deferred"?"1.2.4":"1.2.3";
            QCOMPARE(p["package"].toObject()["version"].toString(),QString(expected));
            QCOMPARE(p["enabled"].toBool(),enabled);
            QCOMPARE(p["updatePending"].toBool(),phase=="update-deferred");
            QCOMPARE(p["validationDeferred"].toBool(),phase=="update-deferred");
            if(phase=="installed")QVERIFY(!p.contains("previous"));
            else QCOMPARE(p["previous"].toObject()["version"].toString(),QString(expected)==QString("1.2.3")?QString("1.2.2"):QString("1.2.3"));
            if(phase=="update-deferred")QCOMPARE(p["pendingPrevious"].toObject()["version"].toString(),QString("1.2.2"));
            else QVERIFY(!p.contains("pendingPrevious"));
            QCOMPARE(p["sourceIds"].toArray(),QJsonArray{phase=="installed"?"probe-source":(QString(expected)=="1.2.3"?"original-source":"candidate-source")});
        }
        const auto recoveredBytes=read(indexPath);
        QProcess twice;twice.start(probe(),arguments("inspect",storePath,indexPath,input));QVERIFY(twice.waitForFinished(5000));
        QCOMPARE(twice.exitCode(),0);QCOMPARE(QJsonDocument::fromJson(twice.readAllStandardOutput()).object(),recovered);
        QCOMPARE(read(indexPath),recoveredBytes);
        QSet<QString> references;QVERIFY2(InstalledPluginRegistry::readPackageReferences(indexPath,storePath,&references,&error),qPrintable(error));
        const int expectedReferences=phase=="copying"||phase=="published"?0:(phase=="installed"?1:(phase=="update-deferred"?3:2));
        QCOMPARE(references.size(),expectedReferences);
        const auto collection=store.collect(indexPath);QVERIFY2(collection.error.isEmpty(),qPrintable(collection.error));
        QVERIFY(!collection.deferred);QVERIFY(!collection.more);
        for(const auto &digest:references)QVERIFY(QFileInfo::exists(storePath+"/"+digest));
        const auto expectedRemoved=phase=="published"||phase=="update-pending"||phase=="validation-started"||phase=="update-confirmed"||phase=="recovery-busy"?1:0;
        QCOMPARE(collection.packages,expectedRemoved);
        QCOMPARE(collection.temporaryDirectories,phase=="copying"?1:0);
        QCOMPARE(QDir(storePath+"/.leases").entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
        QCOMPARE(QDir(storePath).entryList({".stage-*",".unpack-*",".download-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
        // Independent integrity checks after collection also cover previous
        // and pendingPrevious, even if a shared reference scanner were wrong.
        QProcess afterCollection;afterCollection.start(probe(),arguments("inspect",storePath,indexPath,input));
        QVERIFY(afterCollection.waitForFinished(5000));const auto afterRaw=afterCollection.readAllStandardOutput();
        QVERIFY2(afterCollection.exitCode()==0,afterRaw.constData());QCOMPARE(QJsonDocument::fromJson(afterRaw).object(),recovered);
        QCOMPARE(read(indexPath),recoveredBytes);
        QVERIFY(!QFileInfo::exists(executionMarker));
        results.append(QJsonObject{{"phase",phase},{"enabled",enabled},{"terminatedIdentity",identity.json()},
            {"processExited",true},{"checkpoint",checkpoint},{"beforeRecoveryIndex",QJsonDocument::fromJson(durableBeforeRecovery).object()},
            {"recovered",recovered},{"afterCollectionIntegrityVerified",true},
            {"secondRecoveryIdentical",true},{"retainedPackageReferences",references.size()},
            {"collectedPackages",collection.packages},{"collectedTemporaryDirectories",collection.temporaryDirectories},
            {"staleLeases",collection.staleLeases},{"pluginCodeExecuted",false}});
    }
    void cleanupTestCase() {
        const auto path=qEnvironmentVariable("NNP_TRANSACTION_CRASH_OUTPUT");if(path.isEmpty())return;
        QSaveFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument(QJsonObject{{"qtVersion",qVersion()},{"rows",results},
            {"platform",QSysInfo::productType()},{"platformVersion",QSysInfo::productVersion()},
            {"architecture",QSysInfo::currentCpuArchitecture()},
            {"scope","Real native process termination at explicit production transaction checkpoints; not power-loss or an in-syscall atomic-rename/fsync proof."}}).toJson();
        QCOMPARE(output.write(bytes),qint64(bytes.size()));QVERIFY(output.commit());
    }
};
QTEST_GUILESS_MAIN(TransactionCrashTests)
#include "TransactionCrashTests.moc"
