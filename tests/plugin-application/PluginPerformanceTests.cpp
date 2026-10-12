// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in measurement of production Application/MainWindow/plugin services.
#include "NotepadNextApplication.h"
#include "MainWindow.h"
#include "ScintillaNext.h"
#include "SessionManager.h"
#include "PluginEngine.h"
#include "PluginSession.h"
#include "WebviewService.h"
#include "DocumentBridge.h"
#include "RegisteredResourceRegistry.h"
#include "ProcessIdentity.h"
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QMenu>
#include <QProcess>
#include <QQueue>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <algorithm>
#include <cmath>
#include <libproc.h>
#include <mach/mach_time.h>

class MeasuredApplication:public NotepadNextApplication {
public:
    using NotepadNextApplication::NotepadNextApplication;
    bool recording=false;
    double maxDispatchMs=0;
    QJsonArray longDispatches;
    bool notify(QObject *receiver,QEvent *event) override {
        if(!recording)return NotepadNextApplication::notify(receiver,event);
        QElapsedTimer timer;timer.start();const auto type=event->type();const auto name=QString::fromLatin1(receiver->metaObject()->className());
        const auto result=NotepadNextApplication::notify(receiver,event);const auto ms=timer.nsecsElapsed()/1e6;
        maxDispatchMs=std::max(maxDispatchMs,ms);
        if(ms>50&&longDispatches.size()<32)longDispatches.append(QJsonObject{{"receiver",name},{"eventType",int(type)},{"durationMs",ms}});
        return result;
    }
    void resetMeasurements(){maxDispatchMs=0;longDispatches={};}
};
namespace {
QJsonObject wire(const QList<PluginRpc::RpcProcess*> &processes) {
    quint64 tx=0,rx=0,sent=0,received=0;
    for(auto *process:processes){const auto stats=process->wireStatistics();tx+=stats.queuedBytes;rx+=stats.receivedBytes;sent+=stats.sentFrames;received+=stats.receivedFrames;}
    return {{"queuedBytes",qint64(tx)},{"receivedBytes",qint64(rx)},{"sentFrames",qint64(sent)},{"receivedFrames",qint64(received)}};
}
QSet<qint64> descendants(qint64 root) {
    QSet<qint64> result;QQueue<qint64> pending;pending.enqueue(root);
    while(!pending.isEmpty()) {
        const auto parent=pending.dequeue();int pids[1024]{};const auto bytes=proc_listpids(PROC_PPID_ONLY,uint32_t(parent),pids,sizeof(pids));
        if(bytes<0||bytes>=int(sizeof(pids)))qFatal("Cannot enumerate benchmark child processes completely");
        for(int index=0;index<bytes/int(sizeof(int));++index)if(pids[index]>0&&!result.contains(pids[index])){result.insert(pids[index]);pending.enqueue(pids[index]);}
    }
    return result;
}
QJsonObject processSample(qint64 pid,const QString &role) {
    PluginPackages::ProcessIdentity identity;QString error;
    if(!PluginPackages::ProcessIdentity::capture(pid,&identity,&error))return {{"pid",pid},{"role",role},{"available",false}};
    proc_taskinfo info{};const auto bytes=proc_pidinfo(int(pid),PROC_PIDTASKINFO,0,&info,sizeof(info));
    if(bytes!=sizeof(info)||identity.state()!=PluginPackages::ProcessIdentity::State::Alive)return {{"pid",pid},{"role",role},{"available",false}};
    proc_fdinfo descriptors[4096]{};const auto descriptorBytes=proc_pidinfo(int(pid),PROC_PIDLISTFDS,0,descriptors,sizeof(descriptors));
    mach_timebase_info_data_t timebase{};mach_timebase_info(&timebase);
    char path[PROC_PIDPATHINFO_MAXSIZE]{};proc_pidpath(int(pid),path,sizeof(path));
    return {{"pid",pid},{"start",identity.start},{"role",role},{"available",true},{"path",QString::fromUtf8(path)},
        {"fileDescriptors",descriptorBytes>0&&descriptorBytes<int(sizeof(descriptors))?descriptorBytes/int(sizeof(proc_fdinfo)):-1},
        {"rssBytes",qint64(info.pti_resident_size)},{"cpuMs",double(info.pti_total_user+info.pti_total_system)*timebase.numer/timebase.denom/1e6},{"threads",info.pti_threadnum}};
}
QJsonArray processSamples(const QList<PluginRpc::RpcProcess*> &nodes,const QList<QWebEngineView*> &views) {
    QSet<qint64> plugins,renderers;for(auto *node:nodes)plugins.insert(node->processId());for(auto *view:views)renderers.insert(view->page()->renderProcessPid());
    const auto app=QCoreApplication::applicationPid();auto pids=descendants(app);pids.insert(app);auto ordered=pids.values();std::sort(ordered.begin(),ordered.end());QJsonArray result;
    for(const auto pid:ordered){const auto role=pid==app?"application":plugins.contains(pid)?"plugin":renderers.contains(pid)?"renderer":"shared-or-other-child";auto entry=processSample(pid,role);if(QString(role)=="shared-or-other-child"&&entry["path"].toString().contains("/QtWebEngineProcess.app/"))entry["role"]="webengine-helper";result.append(entry);}
    return result;
}
QJsonObject cpuInterval(const QJsonArray &before,const QJsonArray &after,double elapsedMs) {
    QHash<qint64,QJsonObject> first;for(const auto &value:before){const auto entry=value.toObject();first.insert(entry["pid"].toInteger(),entry);}
    QJsonArray samples;QJsonObject totals;int matched=0;
    for(const auto &value:after){const auto entry=value.toObject(),start=first.value(entry["pid"].toInteger());if(!entry["available"].toBool()||!start["available"].toBool()||entry["start"]!=start["start"])continue;
        const auto cpu=entry["cpuMs"].toDouble()-start["cpuMs"].toDouble();const auto role=entry["role"].toString();totals.insert(role,totals[role].toDouble()+100*cpu/elapsedMs);++matched;
        samples.append(QJsonObject{{"pid",entry["pid"]},{"role",role},{"cpuMs",cpu},{"cpuPercentOneCore",100*cpu/elapsedMs}});
    }
    return {{"elapsedMs",elapsedMs},{"matchedProcesses",matched},{"roleCpuPercentOneCore",totals},{"samples",samples},{"qualification","CPU deltas for matching PID and native creation time in both samples; not a system-wide idle or peak measurement."}};
}
QByteArray markdown(qsizetype size,const QString &marker) {
    QByteArray text="# "+marker.toUtf8()+"\n\n";
    const QByteArray paragraph="A paragraph with **bold**, `code`, emoji \xf0\x9f\x98\x80\xe4\xb8\xad.\n\n- first item\n- second item\n\n";
    while(text.size()+paragraph.size()<=size)text+=paragraph;text+=QByteArray(size-text.size(),' ');return text;
}
void spin(int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,2);QTest::qSleep(1);}}
QJsonObject distribution(const QList<double> &input) {
    auto values=input;std::sort(values.begin(),values.end());double total=0;QJsonArray raw;for(double value:input){total+=value;raw.append(value);}
    return {{"samples",input.size()},{"meanMs",total/input.size()},{"p95Ms",values[int(std::ceil(values.size()*.95))-1]},{"maxMs",values.last()},{"rawMs",raw}};
}
}
class PluginPerformanceTests:public QObject {
    Q_OBJECT
public:
    MeasuredApplication *app=nullptr;
    QString directory;
private:
    MainWindow *window=nullptr;
    ScintillaNext *editor=nullptr;
    PluginRuntime::PluginEngine *engine=nullptr;
    QJsonObject report;
    QJsonArray populations;
    QString marker;
    int markerSequence=0;
    QElapsedTimer inputTimer;
    QHash<QString,double> painted;
    QString nextMarker(){return QString("marker-%1").arg(++markerSequence,6,10,QChar('0'));}
    void writeReport() {
        report.insert("populations",populations);const auto output=qEnvironmentVariable("NNP_PERFORMANCE_OUTPUT",directory+"/performance.json");QSaveFile file(output);
        if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0||!file.commit())qFatal("Cannot preserve performance measurements");
    }
    void observe(PluginRuntime::PluginSession *session) {
        const auto id=session->property("benchmarkId").toString();auto *service=session->webviews();QVERIFY(service);
        connect(service,&PluginWebviews::WebviewService::event,this,[this,id](const QString &,const QString &type,const QJsonValue &payload){
            if(type=="message"&&payload.toObject()["benchmarkMarker"].toString()==marker&&!painted.contains(id))painted.insert(id,inputTimer.nsecsElapsed()/1e6);
        });
        QWebEnginePage *page=nullptr;
        // Find the page owned by this service, not a view from a prior session.
        for(auto *candidate:window->findChildren<QWebEngineView*>())if(candidate->page()->profile()->parent()==service){page=candidate->page();break;}
        QVERIFY2(page,"No WebEngine page belongs to the measured plugin session");
        connect(page,&QWebEnginePage::loadFinished,this,[this,page](bool ok){
            if(!ok)return;
            const auto value=QString::fromUtf8(QJsonDocument(QJsonArray{marker}).toJson(QJsonDocument::Compact));
            page->runJavaScript(QString("(()=>{const marker=%1[0];if(document.querySelector('h1')?.textContent!==marker)return;const ready=()=>{if(!window.notepadnext){setTimeout(ready,1);return;}requestAnimationFrame(()=>requestAnimationFrame(()=>window.notepadnext.postMessage({benchmarkMarker:marker})));};ready();})()").arg(value));
        });
    }
private slots:
    void initTestCase() {
        QVERIFY(app->isPrimary());QVERIFY(app->init());app->getSessionManager()->setSessionFileTypes(SessionManager::None);
        for(auto *widget:QApplication::topLevelWidgets())if((window=qobject_cast<MainWindow*>(widget)))break;QVERIFY(window);
        window->resize(1400,1000);window->show();window->raise();window->activateWindow();engine=app->getPluginEngine();QVERIFY(engine);
        QFile source(directory+"/benchmark.md");QVERIFY(source.open(QIODevice::WriteOnly));source.write("# initial\n");source.close();window->openFile(source.fileName());QTRY_VERIFY(window->currentEditor()->isFile());editor=window->currentEditor();
        QProcess packer;packer.start(PERF_NODE,{PERF_STAGER,PERF_MARKDOWN,directory+"/packed"});QVERIFY(packer.waitForFinished(20000));QVERIFY2(packer.exitCode()==0,packer.readAllStandardError().constData());
        mach_timebase_info_data_t timebase{};mach_timebase_info(&timebase);
        report={{"scope","Release production Application/MainWindow services; 1/5/10 independent Markdown instances, differentiated only by manifest/command ID."},{"qtVersion",qVersion()},{"machTimebaseNumerator",int(timebase.numer)},{"machTimebaseDenominator",int(timebase.denom)},
            {"previewEndpoint","Input heading replacement to verified matching DOM after two requestAnimationFrame callbacks and native message reception; includes 150ms debounce."},{"cpuUnits","PROC_PIDTASKINFO Mach CPU ticks converted using mach_timebase_info; interval percent uses one core as 100%."},{"guiEndpoint","QApplication::notify dispatch wall duration plus direct heading edit duration; includes scheduling delays, not isolated added plugin CPU."}};
        QJsonArray baseline;
        for(int size:{10240,102400,1048576}){
            editor->setText(markdown(size,nextMarker()).constData());spin(300);app->resetMeasurements();app->recording=true;double maxEdit=0;
            for(int sample=0;sample<30;++sample){const auto value=nextMarker().toUtf8();QElapsedTimer edit;edit.start();editor->setTargetStart(2);editor->setTargetEnd(15);editor->replaceTarget(value.size(),value.constData());maxEdit=std::max(maxEdit,edit.nsecsElapsed()/1e6);spin(3);}
            app->recording=false;baseline.append(QJsonObject{{"bytes",size},{"maxEditMs",maxEdit},{"maxDispatchMs",app->maxDispatchMs},{"longDispatches",app->longDispatches}});
        }
        report.insert("noPluginGuiBaseline",baseline);report.insert("noPluginResources",processSamples({},{}));writeReport();
    }
    void preview_data(){QTest::addColumn<int>("count");QTest::newRow("1")<<1;QTest::newRow("5")<<5;QTest::newRow("10")<<10;}
    void preview() {
        QFETCH(int,count);auto *installer=app->getPluginInstaller();auto *registry=app->getInstalledPluginRegistry();QString error;QStringList ids;
        for(int index=0;index<count;++index) {
            const auto id=QString("bench.markdown%1").arg(index+1),root=directory+"/clone-"+id;QProcess copy;copy.start("/usr/bin/ditto",{directory+"/packed",root});QVERIFY(copy.waitForFinished(10000));QCOMPARE(copy.exitCode(),0);
            QFile file(root+"/notepadnext-plugin.json");QVERIFY(file.open(QIODevice::ReadOnly));auto metadata=file.readAll();file.close();metadata.replace("notepadnext.markdown-preview",id.toUtf8());QVERIFY(file.open(QIODevice::WriteOnly));file.write(metadata);file.close();
            QFile entry(root+"/dist/index.js");QVERIFY(entry.open(QIODevice::ReadOnly));auto code=entry.readAll();entry.close();code.replace("notepadnext.markdown-preview.open",(id+".open").toUtf8());QVERIFY(entry.open(QIODevice::WriteOnly));entry.write(code);entry.close();
            PluginSources::SourceSnapshot snapshot;QVERIFY2(PluginSources::PluginMetadata::parse(metadata,"https://github.com/bench/"+id,{},&snapshot,&error),qPrintable(error));snapshot.commit=QString(40,'e');QVERIFY(app->getRegisteredResourceRegistry()->registerSource(snapshot,&error));
            QSignalSpy installed(installer,&PluginPackages::InstallationService::finished);QVERIFY(installer->installDirectory(root,snapshot.resources[0]));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,10000);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine->setEnabled(id,true,&error));ids.append(id);
        }
        marker=nextMarker();editor->setText(markdown(10240,marker).constData());painted.clear();inputTimer.start();
        for(const auto &id:ids){engine->invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),10000);QTRY_COMPARE_WITH_TIMEOUT(window->findChildren<QWebEngineView*>().size(),ids.indexOf(id)+1,10000);
            PluginRuntime::PluginSession *session=nullptr;for(auto *candidate:engine->findChildren<PluginRuntime::PluginSession*>())if(auto *process=candidate->findChild<PluginRpc::RpcProcess*>();process&&process->processId()>0&&!candidate->property("benchmarkId").isValid()){QVERIFY(!session);session=candidate;}QVERIFY(session);session->setProperty("benchmarkId",id);observe(session);
        }
        QTRY_COMPARE_WITH_TIMEOUT(engine->sessionCount(),count,10000);spin(500);
        const auto processes=engine->findChildren<PluginRpc::RpcProcess*>();QCOMPARE(processes.size(),count);const auto views=window->findChildren<QWebEngineView*>();QCOMPARE(views.size(),count);
        QJsonObject population{{"processes",count},{"status","running"}};QJsonArray sizes;const auto populationIndex=populations.size();populations.append(population);writeReport();
        for(int size:{10240,102400,1048576}) {
            marker=nextMarker();painted.clear();inputTimer.restart();editor->setText(markdown(size,marker).constData());QTRY_COMPARE_WITH_TIMEOUT(painted.size(),count,20000);
            app->resetMeasurements();app->recording=true;QList<double> latest,all;double maxEdit=0;const auto before=processSamples(processes,views);QElapsedTimer interval;interval.start();
            const auto samples=size==102400?30:size==10240?10:6;
            for(int sample=0;sample<samples;++sample){marker=nextMarker();painted.clear();inputTimer.restart();const auto value=marker.toUtf8();QElapsedTimer edit;edit.start();editor->setTargetStart(2);editor->setTargetEnd(15);editor->replaceTarget(value.size(),value.constData());maxEdit=std::max(maxEdit,edit.nsecsElapsed()/1e6);
                QElapsedTimer deadline;deadline.start();while(painted.size()<count&&deadline.elapsed()<20000){QCoreApplication::processEvents(QEventLoop::AllEvents,2);QTest::qSleep(1);}QVERIFY2(painted.size()==count,qPrintable("Preview did not reach marker "+marker));
                double last=0;for(double ms:painted){all.append(ms);last=std::max(last,ms);}latest.append(last);
            }
            app->recording=false;const auto elapsed=interval.nsecsElapsed()/1e6;const auto after=processSamples(processes,views);
            sizes.append(QJsonObject{{"bytes",size},{"latestAllPanels",distribution(latest)},{"individualPanels",distribution(all)},{"maxEditMs",maxEdit},{"maxGuiDispatchMs",app->maxDispatchMs},{"longGuiDispatches",app->longDispatches},{"resourcesAfterWorkload",after},{"cpu",cpuInterval(before,after,elapsed)}});
            qInfo()<<"Markdown performance"<<count<<size<<"p95 ms"<<distribution(latest)["p95Ms"]<<"GUI max ms"<<app->maxDispatchMs;
            population.insert("sizes",sizes);populations[populationIndex]=population;writeReport();
        }
        population.insert("sizes",sizes);const auto before=processSamples(processes,views);const auto traffic=wire(processes);const auto docSequence=app->getPluginDocumentBridge()->eventSequence();QElapsedTimer idle;idle.start();QEventLoop loop;QTimer::singleShot(10000,&loop,&QEventLoop::quit);loop.exec();const auto elapsed=idle.nsecsElapsed()/1e6;const auto after=processSamples(processes,views);auto delta=wire(processes);for(const auto &key:delta.keys())delta[key]=delta[key].toInteger()-traffic[key].toInteger();
        population.insert("idle",QJsonObject{{"before",before},{"after",after},{"cpu",cpuInterval(before,after,elapsed)},{"wireDeltas",delta},{"documentEvents",app->getPluginDocumentBridge()->eventSequence()-docSequence}});
        QList<PluginPackages::ProcessIdentity> owned;for(auto *process:processes){PluginPackages::ProcessIdentity identity;QVERIFY(PluginPackages::ProcessIdentity::capture(process->processId(),&identity,&error));owned.append(identity);}
        QElapsedTimer stop;stop.start();for(const auto &id:ids)QVERIFY(engine->setEnabled(id,false,&error));QTRY_COMPARE_WITH_TIMEOUT(engine->resourceCount(),0,3000);QTRY_VERIFY_WITH_TIMEOUT(window->findChildren<QWebEngineView*>().isEmpty(),3000);const auto revokeMs=stop.nsecsElapsed()/1e6;QTRY_VERIFY_WITH_TIMEOUT(engine->isIdle(),5000);const auto exitMs=stop.nsecsElapsed()/1e6;
        for(const auto &identity:owned)QTRY_VERIFY_WITH_TIMEOUT(identity.state()==PluginPackages::ProcessIdentity::State::Exited,5000);
        population.insert("stop",QJsonObject{{"revokeMs",revokeMs},{"idleMs",exitMs},{"allOwnedNodeIdentitiesExited",true},{"resourcesAfter",engine->resourceCount()},{"sessionsAfter",engine->sessionCount()},{"processesAfter",processSamples({}, {})}});
        population.insert("status","measured");populations[populationIndex]=population;writeReport();
        for(const auto &value:sizes){const auto row=value.toObject();if(row["bytes"].toInt()==102400)QVERIFY2(row["latestAllPanels"].toObject()["p95Ms"].toDouble()<=500,"100 KiB Markdown preview exceeds the planned 500ms p95 budget.");QVERIFY2(row["maxGuiDispatchMs"].toDouble()<=50,"Observed GUI dispatch exceeds 50ms; baseline and raw events preserved for attribution.");QVERIFY2(row["maxEditMs"].toDouble()<=50,"Direct editor edit exceeds the planned GUI task budget.");}
        QVERIFY(revokeMs<=3000);
    }
    void cleanup() {
        app->recording=false;
        if(engine){for(const auto &plugin:app->getInstalledPluginRegistry()->plugins())engine->uninstall(plugin.id(),true);QTRY_VERIFY_WITH_TIMEOUT(engine->isIdle(),10000);}
        if(editor)editor->setSavePoint();
    }
    void cleanupTestCase() {
        writeReport();if(engine){engine->shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine->isIdle(),5000);}if(window){editor->setSavePoint();window->close();window=nullptr;}
    }
};
int main(int argc,char **argv) {
    QLockFile measurementLock(QDir::tempPath()+"/notepadnext-plugin-performance.lock");measurementLock.setStaleLockTime(0);
    if(!measurementLock.tryLock()){qCritical("Another Markdown performance measurement is running; refusing concurrent measurements.");return 73;}
    QTemporaryDir profile;QApplication::setOrganizationName("NotepadNextPluginPerformance");QApplication::setApplicationName("App-"+QUuid::createUuid().toString(QUuid::WithoutBraces));QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);PluginWebviews::registerScheme();int appArgc=1;char name[]="PluginPerformanceTests";char *appArgv[]={name,nullptr};MeasuredApplication app(appArgc,appArgv,profile.path()+"/data");
    PluginPerformanceTests tests;tests.app=&app;tests.directory=profile.path();return QTest::qExec(&tests,argc,argv);
}
#include "PluginPerformanceTests.moc"
