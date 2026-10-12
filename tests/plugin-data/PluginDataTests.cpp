#include "../PluginTestFilesystem.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginDataStore.h"
#include "PluginDataService.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace PluginData;
class PluginDataTests:public QObject {
    Q_OBJECT
private slots:
    void compareAndExchangePreservesConcurrentWriters() {
        QTemporaryDir temp;const QString id="tests.compare",key="state";Store first(temp.path()),second(temp.path());
        const QJsonObject old{{"schemaVersion",1},{"values",QJsonObject{{"one",1}}}},next{{"schemaVersion",1},{"values",QJsonObject{{"one",1},{"two",2}}}};
        QVERIFY(first.access(id,"storage",key,true,old,QJsonValue(QJsonValue::Null)).error.isEmpty());
        QCOMPARE(second.access(id,"storage",key,true,next,QJsonValue(QJsonValue::Null)).error["code"].toInt(),1001);
        QCOMPARE(second.access(id,"storage",key).value,QJsonValue(old));
        QVERIFY(second.access(id,"storage",key,true,next,old).error.isEmpty());
        QCOMPARE(first.access(id,"storage",key,true,QJsonValue::Undefined,old).error["code"].toInt(),1001);
        QCOMPARE(first.access(id,"storage",key).value,QJsonValue(next));
        QVERIFY(first.access(id,"storage",key,true,QJsonValue::Undefined,next).error.isEmpty());
        QVERIFY(first.access(id,"storage",key).value.isNull());
        QCOMPARE(first.access(id,"storage",key,false,QJsonValue::Undefined,next).error["code"].toInt(),-32602);
        Service service(temp.path());QVector<Result> results;const auto callback=[&](const Result &result){results.append(result);};
        for(int i=0;i<8;++i)service.access(id,"configuration",key,true,i,callback,{},QJsonValue(QJsonValue::Null));
        QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),5000);QCOMPARE(results.size(),8);QVERIFY(results[0].error.isEmpty());
        for(int i=1;i<8;++i)QCOMPARE(results[i].error["code"].toInt(),1001);
        QCOMPARE(first.access(id,"configuration",key).value,QJsonValue(0));
    }
    void persistenceAndIsolation() {
        QTemporaryDir temp;const QString root=temp.path()+"/data";Store store(root);
        QVERIFY(store.access("tests.one","storage","missing").value.isNull());QVERIFY(!QFileInfo::exists(root));
        const QJsonObject value{{"text","😀中"},{"items",QJsonArray{true,12,QJsonValue(QJsonValue::Null)}}};
        QVERIFY(store.access("tests.one","storage","view.settings",true,value).error.isEmpty());
        QCOMPARE(Store(root).access("tests.one","storage","view.settings").value,QJsonValue(value));
        QVERIFY(store.access("tests.two","storage","view.settings").value.isNull());QVERIFY(store.access("tests.one","configuration","view.settings").value.isNull());
        QVERIFY(store.access("tests.one","configuration","theme",true,"dark").error.isEmpty());QVERIFY(store.access("tests.one","storage","null",true,QJsonValue(QJsonValue::Null)).error.isEmpty());
        QVERIFY(store.access("tests.one","storage","view.settings",true).error.isEmpty());QVERIFY(store.access("tests.one","storage","view.settings").value.isNull());
        QCOMPARE(store.access("tests.one","configuration","theme").value,QJsonValue("dark"));
        QVERIFY(store.access("tests.two","storage","keep",true,42).error.isEmpty());QVERIFY(store.erase("tests.one").error.isEmpty());
        QVERIFY(!QFileInfo::exists(store.filePath("tests.one")));QCOMPARE(store.access("tests.two","storage","keep").value,QJsonValue(42));QVERIFY(store.erase("tests.one").error.isEmpty());
    }
    void boundsAndPreservation() {
        QTemporaryDir temp;Store store(temp.path());
        QCOMPARE(store.access("../../escape","storage","x",true,1).error["code"].toInt(),-32602);
        for(const auto &key:QStringList{"","a/b","a\\b","../x",QString(129,'a'),"😀"})QCOMPARE(store.access("tests.one","storage",key,true,1).error["code"].toInt(),-32602);
        QVERIFY(store.access("tests.one","storage","safe",true,"old").error.isEmpty());
        QCOMPARE(store.access("tests.one","storage","safe",true,QString(65537,'x')).error["code"].toInt(),1008);
        QJsonValue nested(1);for(int i=0;i<34;++i)nested=QJsonArray{nested};QCOMPARE(store.access("tests.one","storage","safe",true,nested).error["code"].toInt(),1008);
        QCOMPARE(store.access("tests.one","storage","safe").value,QJsonValue("old"));
        for(int i=0;i<255;++i)QVERIFY(store.access("tests.one","storage",QString("k%1").arg(i),true,i).error.isEmpty());
        QCOMPARE(store.access("tests.one","storage","overflow",true,0).error["code"].toInt(),1007);QVERIFY(store.access("tests.one","storage","overflow").value.isNull());
        QLockFile lock(store.filePath("tests.one")+".lock");QVERIFY(lock.tryLock(0));QCOMPARE(store.access("tests.one","storage","safe",true,0).error["code"].toInt(),1010);QCOMPARE(store.erase("tests.one").error["code"].toInt(),1010);lock.unlock();
        QFile corrupt(store.filePath("tests.one"));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("{broken");corrupt.close();
        QCOMPARE(store.access("tests.one","storage","safe",true,0).error["code"].toInt(),1010);QVERIFY(corrupt.open(QIODevice::ReadOnly));QCOMPARE(corrupt.readAll(),QByteArray("{broken"));corrupt.close();
        QVERIFY(store.erase("tests.one").error.isEmpty()); // Explicit deletion permits a corrupt regular file.
        const auto outside=temp.path()+"/outside";QFile file(outside);QVERIFY(file.open(QIODevice::WriteOnly));file.write("untouched");file.close();QVERIFY(PluginTest::link(outside,store.filePath("tests.one")));
        QCOMPARE(store.access("tests.one","storage","safe",true,0).error["code"].toInt(),1010);QCOMPARE(store.erase("tests.one").error["code"].toInt(),1010);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("untouched"));
    }
    void serializedWorkersAndErase() {
        QTemporaryDir temp;Service service(temp.path());QSignalSpy idle(&service,&Service::idle);QVector<Result> completed;
        const auto callback=[&](const Result &result){completed.append(result);};
        for(int i=0;i<20;++i)service.access("tests.one","storage","count",true,i,callback);
        service.access("tests.one","storage","count",false,{},callback);service.erase("tests.one",callback);service.access("tests.one","storage","count",false,{},callback);
        QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),10000);QCOMPARE(completed.size(),23);for(const auto &result:completed)QVERIFY(result.error.isEmpty());
        QCOMPARE(completed[20].value,QJsonValue(19));QVERIFY(completed[22].value.isNull());QVERIFY(!QFileInfo::exists(Store(temp.path()).filePath("tests.one")));QVERIFY(!idle.isEmpty());
    }
    void totalFileBudget() {
        QTemporaryDir temp;Store store(temp.path());const auto value=QString(65530,'x');
        for(int i=0;i<15;++i)QVERIFY(store.access("tests.budget","storage",QString("key%1").arg(i),true,value).error.isEmpty());
        QCOMPARE(store.access("tests.budget","configuration","overflow",true,value).error["code"].toInt(),1008);
        QCOMPARE(store.access("tests.budget","storage","key0").value,QJsonValue(value));QVERIFY(store.access("tests.budget","configuration","overflow").value.isNull());
    }
    void repositoryIdentityIsolation() {
        QTemporaryDir temp;const QString id="tests.same-id";
        Store original(temp.path(),"https://github.com/author/original"),replacement(temp.path(),"https://github.com/another/replacement");
        QVERIFY(original.access(id,"storage","secret",true,"original data").error.isEmpty());QVERIFY(replacement.access(id,"storage","secret").value.isNull());
        QVERIFY(replacement.access(id,"storage","secret",true,"replacement data").error.isEmpty());QVERIFY(original.filePath(id)!=replacement.filePath(id));
        QVERIFY(replacement.erase(id).error.isEmpty());QCOMPARE(original.access(id,"storage","secret").value,QJsonValue("original data"));
        QVERIFY(QFile::copy(original.filePath(id),replacement.filePath(id)));QCOMPARE(replacement.access(id,"storage","secret").error["code"].toInt(),1010);QVERIFY(replacement.erase(id).error.isEmpty());
        Service service(temp.path());QVector<Result> results;const auto callback=[&](const Result &result){results.append(result);};
        service.access(id,"storage","secret",false,{},callback,"https://github.com/author/original");
        service.access(id,"storage","secret",false,{},callback,"https://github.com/another/replacement");
        QTRY_VERIFY_WITH_TIMEOUT(service.isIdle(),5000);QCOMPARE(results.size(),2);
        QVERIFY((results[0].value==QJsonValue("original data")&&results[1].value.isNull())||(results[1].value==QJsonValue("original data")&&results[0].value.isNull()));
    }
};
QTEST_GUILESS_MAIN(PluginDataTests)
#include "PluginDataTests.moc"
