// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginDiagnostics.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace PluginDiagnostics;
namespace {
QJsonObject record(const QByteArray &message="plugin log",Failure failure=Failure::None,const QString &repository="https://github.com/tests/diagnostics",const QString &id="tests.diagnostics") {
    return Store::event(id,repository,"session-test",failure==Failure::None?"stderr":"failure",message,failure);
}
QByteArray bytes(const QString &path) {QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
bool write(const QString &path,const QByteArray &data) {QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();}
}
class PluginDiagnosticTests:public QObject {
    Q_OBJECT
private slots:
    void durableOriginBoundFailures() {
        QTemporaryDir temp;const auto root=temp.path()+"/logs";Store store(root);
        QVERIFY(store.read().error.isEmpty());QVERIFY(!QFileInfo::exists(root));
        auto result=store.append({record("activation fixture",Failure::ActivationFailed),Store::event("tests.diagnostics","https://github.com/tests/diagnostics","session-test","command","Command completed.",Failure::None,{{"method","nnp.commands.invoke"},{"durationMs",42},{"params",QJsonObject{{"text","private buffer"}}}})});
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.events.size(),2);QCOMPARE(result.failures.size(),1);QVERIFY(!result.events[1].toObject().contains("params"));
        result=Store(root).read();QCOMPARE(result.events.size(),2);QCOMPARE(result.failures[0].toObject()["category"].toString(),QString("activation-failed"));
        QVERIFY(Store::format(result.events[1].toObject()).contains("nnp.commands.invoke (42 ms)"));
        result=store.append({record("different publisher",Failure::ProtocolFault,"https://github.com/other/diagnostics"),record("same publisher latest",Failure::ProcessCrash)});
        QVERIFY(result.error.isEmpty());QCOMPARE(result.failures.size(),2);QCOMPARE(result.failures.last().toObject()["message"].toString(),QString("same publisher latest"));
        QVERIFY((QFileInfo(root+"/current.json").permissions()&QFile::ReadOther)==0);
        QVERIFY((QFileInfo(root+"/current.json").permissions()&QFile::WriteOther)==0);
    }
    void rotationAndGlobalBudgets() {
        QTemporaryDir temp;Store store(temp.path());QByteArray message(7900,'"');Snapshot result;
        for(int batch=0;batch<7;++batch) {QJsonArray events;for(int i=0;i<60;++i)events.append(record(message+QByteArray::number(batch*60+i)));result=store.append(events);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));}
        QVERIFY(result.events.size()<Store::MaxEvents);QVERIFY(result.events.last().toObject()["message"].toString().endsWith("419"));
        const auto previous=bytes(temp.path()+"/previous.json");QVERIFY(!previous.isEmpty());QVERIFY(previous.size()<=Store::MaxFileBytes);QVERIFY(bytes(temp.path()+"/current.json").size()<=Store::MaxFileBytes);
        for(int batch=0;batch<2;++batch) {QJsonArray failures;for(int i=0;i<60;++i)failures.append(record(QByteArray(7000,'f'),Failure::ActivationFailed,"https://github.com/tests/diagnostics",QString("tests.failure.%1").arg(batch*60+i)));result=store.append(failures);QVERIFY(result.error.isEmpty());}
        QCOMPARE(result.failures.size(),Store::MaxFailures);for(const auto &failure:result.failures)QVERIFY(failure.toObject()["message"].toString().toUtf8().size()<=1024);
        QVERIFY(bytes(temp.path()+"/current.json").size()+bytes(temp.path()+"/previous.json").size()<=2*Store::MaxFileBytes);
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files).size(),2);
    }
    void invalidAndCorruptRecordsArePreserved() {
        QTemporaryDir temp;Store store(temp.path());QVERIFY(store.append({record()}).error.isEmpty());const auto original=bytes(temp.path()+"/current.json"),backup=bytes(temp.path()+"/previous.json");
        auto invalid=record();invalid["params"]=QJsonObject{{"text","document content"}};QVERIFY(!store.append({invalid}).error.isEmpty());QCOMPARE(bytes(temp.path()+"/current.json"),original);
        QVERIFY(write(temp.path()+"/current.json","{broken"));QVERIFY(!store.read().error.isEmpty());QVERIFY(!store.append({record()}).error.isEmpty());QCOMPARE(bytes(temp.path()+"/current.json"),QByteArray("{broken"));QCOMPARE(bytes(temp.path()+"/previous.json"),backup);
        QVERIFY(write(temp.path()+"/current.json",original));QVERIFY(write(temp.path()+"/previous.json",QByteArray(Store::MaxFileBytes+1,'x')));QVERIFY(!store.append({record()}).error.isEmpty());QCOMPARE(bytes(temp.path()+"/current.json"),original);
    }
    void interruptedAtomicWritesAreReclaimed() {
        QTemporaryDir temp;Store store(temp.path());QVERIFY(store.append({record("committed before crash")}).error.isEmpty());
        for(int cycle=0;cycle<20;++cycle) {
            const auto committed=bytes(temp.path()+"/current.json");QProcess writer;writer.start(QCoreApplication::applicationDirPath()+"/DiagnosticCrashProbe",{temp.path()});QVERIFY(writer.waitForStarted(3000));QByteArray ready;
            QTRY_VERIFY_WITH_TIMEOUT(([&]{ready+=writer.readAllStandardOutput();return ready=="ready\n";})(),3000);
#ifdef Q_OS_MACOS
            QCOMPARE(QDir(temp.path()).entryList({"current.json.*"},QDir::Files).size(),1);
#endif
            writer.kill();QVERIFY(writer.waitForFinished(3000));QCOMPARE(bytes(temp.path()+"/current.json"),committed);
            const auto result=store.append({record(QByteArray("after crash ")+QByteArray::number(cycle))});QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(QDir(temp.path()).entryList(QDir::Files).size(),2);
        }
        QVERIFY(write(temp.path()+"/unknown.keep","keep"));QVERIFY(store.append({record()}).error.isEmpty());QCOMPARE(bytes(temp.path()+"/unknown.keep"),QByteArray("keep"));
    }
    void busyAndUnsafePathsDoNotOverwrite() {
        QTemporaryDir temp,outside;Store store(temp.path());QVERIFY(store.append({record()}).error.isEmpty());const auto original=bytes(temp.path()+"/current.json");
        QLockFile lock(temp.path()+"/store.lock");lock.setStaleLockTime(0);QVERIFY(lock.tryLock(0));QVERIFY(!store.append({record("busy")}).error.isEmpty());QCOMPARE(bytes(temp.path()+"/current.json"),original);lock.unlock();
        QVERIFY(write(outside.path()+"/foreign.json","foreign"));QVERIFY(QFile::remove(temp.path()+"/current.json"));QVERIFY(QFile::link(outside.path()+"/foreign.json",temp.path()+"/current.json"));
        QVERIFY(!store.append({record()}).error.isEmpty());QCOMPARE(bytes(outside.path()+"/foreign.json"),QByteArray("foreign"));
        const auto alias=outside.path()+"/alias";QVERIFY(QFile::link(temp.path(),alias));QVERIFY(!Store(alias).read().error.isEmpty());
    }
    void unicodeAndClosedEventContract() {
        const auto value=record(QString("😀中").repeated(3000).toUtf8());QVERIFY(Store::validEvent(value));const auto message=value["message"].toString();QVERIFY(message.endsWith("[truncated]"));QVERIFY(!message.contains(QChar(0xfffd)));
        for(const auto &field:QStringList{"sessionId","timestamp","pluginId","category","event","durationMs","exitCode"}) {auto invalid=value;invalid[field]=QJsonObject{};QVERIFY2(!Store::validEvent(invalid),qPrintable(field));}
        auto invalid=value;invalid["durationMs"]=-1;QVERIFY(!Store::validEvent(invalid));invalid=value;invalid["repository"]="https://github.com/tests/diagnostics\n";QVERIFY(!Store::validEvent(invalid));
    }
    void workerQueueFailurePriorityAndFlush() {
        QTemporaryDir temp;Service service(temp.path());QSignalSpy errors(&service,&Service::unavailable);
        for(int i=0;i<300;++i)service.append(record(QByteArray::number(i)));
        service.append(record("keep the failure summary",Failure::ActivationFailed));service.flush();
        QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),5000);QVERIFY(errors.isEmpty());const auto result=Store(temp.path()).read();QVERIFY(result.error.isEmpty());QCOMPARE(result.failures.size(),1);
        QVERIFY(result.failures[0].toObject()["message"].toString().contains("keep the failure"));bool skipped=false;for(const auto &value:result.events)skipped|=value.toObject()["message"].toString().contains("Skipped");QVERIFY(skipped);
        Service restored(temp.path());QSignalSpy loaded(&restored,&Service::restored);QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,3000);QCOMPARE(loaded[0][1].value<QJsonArray>().size(),1);QTRY_VERIFY(restored.isIdle());
    }
    void boundedStorageFailureDoesNotHoldShutdown() {
        QTemporaryDir temp;QVERIFY(write(temp.path()+"/current.json","invalid"));Service service(temp.path());QSignalSpy errors(&service,&Service::unavailable);
        service.append(record("memory logging still works",Failure::ProtocolFault));service.flush();QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),3000);QVERIFY(!errors.isEmpty());QCOMPARE(bytes(temp.path()+"/current.json"),QByteArray("invalid"));
        QVERIFY(QFile::remove(temp.path()+"/current.json"));QSignalSpy recovered(&service,&Service::available);service.append(record("storage repaired"));service.flush();QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),3000);QCOMPARE(recovered.size(),1);QVERIFY(Store(temp.path()).read().error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PluginDiagnosticTests)
#include "PluginDiagnosticTests.moc"
