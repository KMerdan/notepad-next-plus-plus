// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageStore.h"
#include "PackageLease.h"
#include "PluginMetadata.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
using namespace PluginPackages;
class ParentCrashTests:public QObject {
    Q_OBJECT
private slots:
    void parentDeathWhilePluginSpins_data() {
        QTest::addColumn<QString>("kind");QTest::newRow("node")<<QString("node");
        if(!QString(PARENT_TEST_PYTHON).isEmpty())QTest::newRow("python")<<QString("python");
    }
    void parentDeathWhilePluginSpins() {
        QFETCH(QString,kind);const bool python=kind=="python";
        QTemporaryDir temp;const auto root=temp.path()+"/store",input=temp.path()+"/input",marker=temp.path()+"/spinning",childFile=temp.path()+"/descendant.json",pythonFile=temp.path()+"/python.json";QString error;
        const auto quote=[](const QString &s){auto json=QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);return json.mid(1,json.size()-2);};
        const QJsonObject manifest{{"manifestVersion",1},{"id","tests.parent-crash"},{"name","Parent crash fixture"},{"version","1.0.0"},{"list",false},
            {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.1.0"}}},{"runtime",QJsonObject{{"kind",kind},{"entry",python?"dist/index.py":"dist/index.cjs"}}},
            {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1"}},{"optional",QJsonArray{}}}},{"activationEvents",QJsonArray{"onCommand:tests.parent-crash.open"}},
            {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id","tests.parent-crash.open"},{"title","Parent crash"}}}}}}};
        QVERIFY(QDir().mkpath(input+"/dist"));
        const auto write=[](const QString &file,const QByteArray &data){QFile f(file);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        QVERIFY(write(input+"/notepadnext-plugin.json",QJsonDocument(manifest).toJson()));QVERIFY(write(input+"/LICENSE","Test fixture license\n"));
        const QByteArray plugin=python?QByteArray("import asyncio, os, subprocess, sys\nfrom pathlib import Path\ndef spin():\n    Path(")+quote(marker)+").write_text('spinning')\n    while True:\n        pass\nasync def activate(ctx):\n    await ctx.commands.register('tests.parent-crash.open', lambda: None)\n    child = subprocess.Popen([sys.executable, '-I', '-c', 'import time; time.sleep(60)'])\n    Path("+quote(childFile)+").write_text(str(child.pid))\n    Path("+quote(pythonFile)+").write_text(str(os.getpid()))\n    asyncio.get_running_loop().call_later(0.1, spin)\n":QByteArray("const fs=require('node:fs');exports.activate=async ctx=>{await ctx.commands.register('tests.parent-crash.open',()=>null);const child=require('node:child_process').spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore'});fs.writeFileSync(")+quote(childFile)+",JSON.stringify(child.pid));setTimeout(()=>{fs.writeFileSync("+quote(marker)+",'spinning');while(true){}},100);};\n";
        QVERIFY(write(input+(python?"/dist/index.py":"/dist/index.cjs"),plugin));PluginSources::SourceSnapshot source;
        QVERIFY2(PluginSources::PluginMetadata::parse(QJsonDocument(manifest).toJson(),"https://github.com/tests/parent-crash",{},&source,&error),qPrintable(error));
        PackagePolicy policy;policy.capabilities={"commands@1"};PreparedPackage package;PackageStore store(root);
        QVERIFY2(store.prepareDirectory(input,source.resources[0],policy,&package,&error),qPrintable(error));package.lease.reset();
        QStringList arguments{root,package.digest,PARENT_TEST_NODE,python?QFileInfo(PARENT_TEST_HOST).dir().filePath("python-host.cjs"):QString(PARENT_TEST_HOST)};if(python)arguments.append(PARENT_TEST_PYTHON);
        QProcess owner;owner.start(QCoreApplication::applicationDirPath()+"/ParentCrashProbe",arguments);
        QVERIFY(owner.waitForStarted(3000));QByteArray ready;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{ready+=owner.readAllStandardOutput();return ready.startsWith("ready ")&&ready.endsWith('\n');})(),5000);
        bool valid=false;const auto pid=ready.mid(6).trimmed().toLongLong(&valid);QVERIFY(valid&&pid>0);
        ProcessIdentity runtime,descendant,interpreter;QVERIFY2(ProcessIdentity::capture(pid,&runtime,&error),qPrintable(error));
        const auto cleanup=qScopeGuard([&]{
#ifdef Q_OS_UNIX
            if(runtime.state()==ProcessIdentity::State::Alive){::kill(-int(runtime.pid),SIGKILL);::kill(int(runtime.pid),SIGKILL);}
            if(descendant.pid>0&&descendant.state()==ProcessIdentity::State::Alive)::kill(int(descendant.pid),SIGKILL);
#endif
        });
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker)&&QFileInfo::exists(childFile),5000);QFile child(childFile);QVERIFY(child.open(QIODevice::ReadOnly));const auto childPid=child.readAll().toLongLong(&valid);QVERIFY(valid&&childPid>0);QVERIFY(ProcessIdentity::capture(childPid,&descendant,&error));
        if(python){QFile file(pythonFile);QVERIFY(file.open(QIODevice::ReadOnly));const auto pythonPid=file.readAll().toLongLong(&valid);QVERIFY(valid&&pythonPid>0);QVERIFY(ProcessIdentity::capture(pythonPid,&interpreter,&error));}
#ifdef Q_OS_UNIX
        // Freeze the entire helper before killing its application owner. Its
        // watcher cannot run yet, so retention is an observable requirement,
        // rather than a race with an immediately exiting child process.
        QVERIFY(::kill(int(pid),SIGSTOP)==0);owner.kill();QVERIFY(owner.waitForFinished(3000));
        QCOMPARE(runtime.state(),ProcessIdentity::State::Alive);auto result=store.collect(temp.path()+"/installed.json");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,0);QVERIFY(QFileInfo::exists(package.root));
        QVERIFY(::kill(int(pid),SIGCONT)==0);
        QTRY_COMPARE_WITH_TIMEOUT(runtime.state(),ProcessIdentity::State::Exited,5000);
        QTRY_COMPARE_WITH_TIMEOUT(descendant.state(),ProcessIdentity::State::Exited,5000);
        if(python)QTRY_COMPARE_WITH_TIMEOUT(interpreter.state(),ProcessIdentity::State::Exited,5000);
        result=store.collect(temp.path()+"/installed.json");QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,1);QCOMPARE(result.staleLeases,1);QVERIFY(!QFileInfo::exists(package.root));
#endif
    }
};
QTEST_GUILESS_MAIN(ParentCrashTests)
#include "ParentCrashTests.moc"
