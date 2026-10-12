// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstalledPluginRegistry.h"
#include "ProcessIdentity.h"
#include "PluginDataStore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
using namespace PluginPackages;
class ApplicationExitTests:public QObject {
    Q_OBJECT
    QJsonArray results;
private slots:
    void lastWindowClose_data() {
        QTest::addColumn<QString>("scenario");
        for(const auto &value:QStringList{"normal-close","unresponsive-deactivate","reload-pending","uninstall-pending","update-activating","update-preparation-pending"})
            QTest::newRow(qPrintable(value))<<value;
    }
    void lastWindowClose() {
        QFETCH(QString,scenario);
        QTemporaryDir profile(QDir::tempPath()+"/NotepadNext 退出验证-XXXXXX");QVERIFY(profile.isValid());
        auto environment=QProcessEnvironment::systemEnvironment();
        for(const auto &key:environment.keys())if(key.startsWith("QT_")||key.startsWith("QML_")||key.startsWith("DYLD_")||key.startsWith("NODE_")||key.startsWith("PYTHON"))environment.remove(key);
        const auto resultPath=profile.path()+"/result.json";
        environment.insert("NNP_APPLICATION_EXIT_SCENARIO",scenario);environment.insert("NNP_APPLICATION_EXIT_PROFILE",profile.path());environment.insert("NNP_APPLICATION_EXIT_RESULT",resultPath);
        QProcess application;application.setProcessEnvironment(environment);application.setWorkingDirectory(profile.path());
        const auto cleanup=qScopeGuard([&]{if(application.state()!=QProcess::NotRunning){application.kill();application.waitForFinished(3000);}});
        // No positional QtTest arguments: Cocoa would interpret them as files.
        application.start(QString(EXIT_APPLICATION),{});QVERIFY(application.waitForStarted(5000));QVERIFY(application.waitForFinished(25000));
        QFile file(resultPath);QVERIFY2(file.open(QIODevice::ReadOnly),application.readAllStandardError().constData());
        auto report=QJsonDocument::fromJson(file.readAll()).object();file.close();
        QVERIFY2(application.exitStatus()==QProcess::NormalExit&&application.exitCode()==0,QJsonDocument(report).toJson().constData());
        QCOMPARE(report["status"],QJsonValue("passed"));QVERIFY(report["idleAtExecReturn"].toBool());
        QCOMPARE(report["sessionsAtExecReturn"].toInt(),0);QCOMPARE(report["resourcesAtExecReturn"].toInt(),0);
        QVERIFY(!report["installerBusyAtExecReturn"].toBool());
        if(scenario=="update-preparation-pending") {
            QVERIFY(report["beforeExit"].toObject()["installerBusy"].toBool());
            QVERIFY(report["storeLockHeldAtExecReturn"].toBool());QVERIFY(report["preparationCancelled"].toBool());
        }
        QVERIFY(report["quitElapsedMs"].toDouble()<8000);
        QCOMPARE(report["processes"].toArray().size(),scenario=="update-activating"?4:2);
        for(const auto &value:report["processes"].toArray()) {
            ProcessIdentity identity;auto object=value.toObject();object.remove("exitedAtExecReturn");
            QVERIFY(ProcessIdentity::parse(object,&identity));
            QTRY_COMPARE_WITH_TIMEOUT(identity.state(),ProcessIdentity::State::Exited,3000);
        }
        const auto indexPath=profile.path()+"/data/plugins/installed.json",storePath=profile.path()+"/data/plugins/packages";
        InstalledPluginRegistry index(indexPath,storePath);QString error;QVERIFY2(index.load(&error),qPrintable(error));
        const auto installed=index.plugin("tests.application-exit");QVERIFY(installed.enabled);QVERIFY(!installed.updatePending);
        QCOMPARE(installed.package.metadata["version"].toString(),QString("1.0.0"));QVERIFY2(PackageStore(storePath).verify(installed.package,&error),qPrintable(error));
        const auto data=PluginData::Store(profile.path()+"/data/plugins/data","https://github.com/tests/application-exit").access("tests.application-exit","storage","retained");
        QVERIFY2(data.error.isEmpty(),QJsonDocument(data.error).toJson().constData());QCOMPARE(data.value,QJsonValue("exit fixture data"));
        QFile history(profile.path()+"/markers/activations.jsonl");QVERIFY(history.open(QIODevice::ReadOnly));
        QJsonArray activations;
        for(const auto &line:history.readAll().split('\n'))if(!line.isEmpty()) {
            const auto entry=QJsonDocument::fromJson(line).object();QVERIFY(!entry.isEmpty());activations.append(entry);
        }
        QCOMPARE(activations.size(),scenario=="update-activating"?2:1);
        report["activationHistory"]=activations;
        report["parentConfirmedAllIdentitiesExited"]=true;report["recoveredVersion"]="1.0.0";report["retainedDataVerified"]=true;
        if(scenario=="update-activating"){QVERIFY(report["pendingAtExit"].toBool());QCOMPARE(report["installedVersionAtExit"],QJsonValue("1.0.1"));}
        else QVERIFY(!report["pendingAtExit"].toBool());
        results.append(report);
    }
    void cleanupTestCase() {
        const auto path=qEnvironmentVariable("NNP_APPLICATION_EXIT_OUTPUT");if(path.isEmpty())return;
        QSaveFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument(QJsonObject{{"rows",results},
            {"scope","Production Application/MainWindow last-window close with owned Node/descendant processes and cancellation of pending preparation while a real store lock stays held; not an uninterruptible-file-system or outer 5s/8s deadline fault injection."}}).toJson();
        QCOMPARE(file.write(bytes),qint64(bytes.size()));QVERIFY(file.commit());
    }
};
QTEST_GUILESS_MAIN(ApplicationExitTests)
#include "ApplicationExitTests.moc"
