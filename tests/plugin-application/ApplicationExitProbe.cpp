// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApplicationExitProbe.h"
#include "NotepadNextApplication.h"
#include "MainWindow.h"
#include "ScintillaNext.h"
#include "SessionManager.h"
#include "PluginEngine.h"
#include "PluginSession.h"
#include "DocumentBridge.h"
#include "RegisteredResourceRegistry.h"
#include "ProcessIdentity.h"
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPointer>
#include <QSaveFile>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QWebEngineView>
#include <functional>
#include <stdexcept>
using namespace PluginPackages;
namespace {
constexpr auto Id="tests.application-exit";
constexpr auto Command="tests.application-exit.open";
constexpr auto Repository="https://github.com/tests/application-exit";
void require(bool condition,const QString &message) {
    if(!condition)throw std::runtime_error(message.toStdString());
}
bool waitFor(const std::function<bool()> &predicate,int milliseconds=10000) {
    QElapsedTimer timer;timer.start();
    while(!predicate()&&timer.elapsed()<milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
        QThread::msleep(5);
    }
    return predicate();
}
QByteArray quote(const QString &text) {
    const auto json=QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return json.mid(1,json.size()-2);
}
void write(const QString &path,const QByteArray &bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create fixture directory.");
    QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.flush(),"Cannot write fixture file.");
}
QJsonObject read(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
PluginSources::SourceSnapshot package(const QString &input,const QString &markers,const QString &version,bool hangDeactivate,bool hangActivate) {
    const QJsonObject manifest{{"manifestVersion",1},{"id",Id},{"name","Application exit fixture"},{"version",version},{"list",false},
        {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.1.0"}}},
        {"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.cjs"}}},
        {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1","documents.read@1","documents.events@1","webviews@1","storage@1"}},{"optional",QJsonArray{}}}},
        {"activationEvents",QJsonArray{"onCommand:tests.application-exit.open"}},
        {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id",Command},{"title","Open exit fixture"}}}},
            {"menus",QJsonArray{QJsonObject{{"location","plugins"},{"command",Command},{"order",1}}}}}}};
    const auto active=markers+"/"+version+"-active.json",childReady=markers+"/"+version+"-child.json";
    const auto childCode="const fs=require('node:fs');process.on('SIGTERM',()=>{});fs.writeFileSync("+quote(childReady)+",JSON.stringify({pid:process.pid}));setInterval(()=>{},1000);";
    QByteArray code="const fs=require('node:fs');let panel;process.on('SIGTERM',()=>{});\n"
        "async function open(ctx){await ctx.storage.update('retained','exit fixture data');panel=await ctx.webviews.create('exit-fixture','Exit fixture');"
        "const doc=await ctx.documents.getActive();await panel.setHtml('<h1>Exit fixture</h1>',{documentId:doc.documentId,version:doc.version});"
        "fs.writeFileSync("+quote(markers+"/command-ready")+",'ready');}\n"
        "exports.activate=async ctx=>{ctx.subscriptions.push(await ctx.commands.register('tests.application-exit.open',()=>open(ctx)));"
        "ctx.subscriptions.push(await ctx.documents.watch(()=>{}));";
    if(hangActivate)code+="panel=await ctx.webviews.create('exit-candidate','Hidden candidate');const doc=await ctx.documents.getActive();"
        "await panel.setHtml('<h1>Unconfirmed candidate</h1>',{documentId:doc.documentId,version:doc.version});";
    code+="const child=require('node:child_process').spawn(process.execPath,['-e',"+quote(QString::fromUtf8(childCode))+"],{stdio:'ignore'});"
        "fs.writeFileSync("+quote(active)+",JSON.stringify({pid:process.pid,child:child.pid,execPath:process.execPath}));"
        "fs.appendFileSync("+quote(markers+"/activations.jsonl")+",JSON.stringify({version:"+quote(version)+",pid:process.pid,child:child.pid})+'\\n');";
    code+=hangActivate?"while(true){};};\n":"};\n";
    code+="exports.deactivate=async()=>{fs.writeFileSync("+quote(markers+"/"+version+"-deactivate")+",'entered');";
    code+=hangDeactivate?"while(true){};};\n":"};\n";
    write(input+"/notepadnext-plugin.json",QJsonDocument(manifest).toJson());write(input+"/LICENSE","Application exit fixture license\n");
    write(input+"/dist/index.cjs",code);
    PluginSources::SourceSnapshot source;QString error;
    require(PluginSources::PluginMetadata::parse(QJsonDocument(manifest).toJson(),Repository,{},&source,&error),error);
    source.commit=QString(40,'a');
    return source;
}
}
int runApplicationExitProbe(NotepadNextApplication &app,const QString &profile,const QString &scenario) {
    QJsonObject report{{"scenario",scenario}};QVector<ProcessIdentity> identities;
    const auto cleanup=qScopeGuard([&]{
        if(auto *engine=app.getPluginEngine();engine&&!engine->isIdle()) {
            engine->shutdown();engine->forceStop();waitFor([&]{return engine->isIdle();},6000);
        }
    });
    try {
        require(QStringList{"normal-close","unresponsive-deactivate","reload-pending","uninstall-pending","update-activating","update-preparation-pending"}.contains(scenario),"Unknown exit scenario.");
        require(app.isPrimary()&&app.init(),"Cannot initialize isolated production application.");
        QPointer<MainWindow> window;
        for(auto *widget:QApplication::topLevelWidgets())if((window=qobject_cast<MainWindow*>(widget)))break;
        require(window&&window->currentEditor(),"Missing initial editor.");
        app.getSessionManager()->setSessionFileTypes(SessionManager::None);
        window->currentEditor()->setText("production exit fixture 😀中\n");window->currentEditor()->setSavePoint();
        auto *engine=app.getPluginEngine();auto *index=app.getInstalledPluginRegistry();auto *installer=app.getPluginInstaller();
        const auto markers=profile+"/markers";require(QDir().mkpath(markers),"Cannot create markers.");
        const bool update=scenario=="update-activating";
        const auto source=package(profile+"/v1",markers,"1.0.0",scenario!="normal-close"&&!update,false);
        QString error;require(app.getRegisteredResourceRegistry()->registerSource(source,&error),error);
        require(installer->installDirectory(profile+"/v1",source.resources[0]),"Cannot start fixture installation.");
        require(waitFor([&]{return !installer->isBusy();}),"Installation did not finish.");
        require(index->plugin(Id).id()==Id,"Fixture was not installed.");
        require(engine->setEnabled(Id,true,&error),error);
        QAction *action=nullptr;for(auto *candidate:engine->findChildren<QAction*>())if(candidate->objectName()=="pluginCommand:"+QString(Command))action=candidate;
        require(action,"Missing production menu command.");action->trigger();
        require(waitFor([&]{return engine->pluginState(Id)=="active"&&QFileInfo::exists(markers+"/command-ready");}),engine->pluginLog(Id));
        require(window->findChild<QWebEngineView*>(),"Missing published preview.");
        const auto capture=[&](const QString &version) {
            require(waitFor([&]{return QFileInfo::exists(markers+"/"+version+"-child.json");}),"Descendant did not start.");
            const auto active=read(markers+"/"+version+"-active.json");
            require(active["child"].toDouble()==read(markers+"/"+version+"-child.json")["pid"].toDouble(),"Descendant PID mismatch.");
            require(QFileInfo(active["execPath"].toString()).canonicalFilePath()==QFileInfo(PluginRuntime::RuntimePaths::bundled().node).canonicalFilePath(),"Plugin used an uncontrolled runtime.");
            for(const auto &field:QStringList{"pid","child"}) {
                ProcessIdentity identity;require(ProcessIdentity::capture(qint64(active[field].toDouble()),&identity,&error),error);
                require(identity.state()==ProcessIdentity::State::Alive,"Owned Node identity is not alive at capture.");identities.append(identity);
            }
        };
        capture("1.0.0");
        QLockFile storeLock(index->packageStorePath()+"/.store.lock");storeLock.setStaleLockTime(0);
        QObject preparationContext;bool preparationFinished=false;QString preparationError;
        QObject::connect(installer,&PluginPackages::InstallationService::finished,&preparationContext,
            [&](const QString &id,const QString &failure){if(id==Id){preparationFinished=true;preparationError=failure;}});
        if(scenario=="reload-pending"||scenario=="uninstall-pending") {
            if(scenario=="reload-pending")engine->reload(Id);else engine->uninstall(Id,false);
            require(waitFor([&]{return QFileInfo::exists(markers+"/1.0.0-deactivate");},1500),"Management stop did not reach deactivate.");
            require(engine->pluginState(Id)==(scenario=="reload-pending"?"updating":"stopping"),"Expected a queued management operation.");
            require(index->plugin(Id).package.metadata["version"]=="1.0.0","Uncommitted management operation changed the index.");
        } else if(scenario=="update-preparation-pending") {
            const auto candidate=package(profile+"/v2",markers,"1.0.1",false,false);
            require(waitFor([&]{return storeLock.isLocked()||storeLock.tryLock(0);},1000),"Cannot hold the real package store lock.");
            require(installer->updateDirectory(profile+"/v2",candidate.resources[0]),"Cannot start update preparation.");
            QElapsedTimer pending;pending.start();
            require(waitFor([&]{return pending.elapsed()>=100;},500),"Preparation observation did not advance.");
            require(installer->isBusy()&&!preparationFinished,"Update preparation was not pending while the store remained locked.");
            require(engine->pluginState(Id)=="active"&&!index->plugin(Id).updatePending
                &&index->plugin(Id).package.metadata["version"]=="1.0.0","Preparation changed the active version before publication.");
        } else if(update) {
            const auto candidate=package(profile+"/v2",markers,"1.0.1",true,true);
            require(installer->updateDirectory(profile+"/v2",candidate.resources[0]),"Cannot prepare update.");
            require(waitFor([&]{return QFileInfo::exists(markers+"/1.0.1-active.json");}),engine->pluginLog(Id));
            capture("1.0.1");require(index->plugin(Id).updatePending,"Candidate was confirmed prematurely.");
            require(index->plugin(Id).package.metadata["version"]=="1.0.1","Missing candidate index.");
            require(engine->pluginState(Id)=="updating","Candidate management state is not updating.");
            bool activating=false;
            for(auto *session:engine->findChildren<PluginRuntime::PluginSession*>())if(session->state()=="activating")activating=true;
            require(activating&&engine->sessionCount()==1,"Candidate session is not in activation.");
            for(auto *view:window->findChildren<QWebEngineView*>())require(!view->isVisible(),"Unconfirmed candidate became visible.");
        }
        report["beforeExit"]=QJsonObject{{"state",engine->pluginState(Id)},{"resources",engine->resourceCount()},
            {"installedVersion",index->plugin(Id).package.metadata["version"]},{"updatePending",index->plugin(Id).updatePending},
            {"installerBusy",installer->isBusy()}};
        bool aboutToQuit=false;QObject quitContext;
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&quitContext,[&]{aboutToQuit=true;});
        app.setQuitOnLastWindowClosed(true);QTimer::singleShot(0,window,&QWidget::close);
        QElapsedTimer timer;timer.start();const auto exitCode=app.exec();
        report["quitElapsedMs"]=timer.elapsed();report["applicationExecExitCode"]=exitCode;
        require(aboutToQuit&&exitCode==0,"Last-window close did not follow the application quit path.");
        require(timer.elapsed()<8000,"Application exceeded the original exit deadline.");
        require(engine->isIdle()&&engine->sessionCount()==0&&engine->resourceCount()==0,"Owned application resources remain after exec.");
        require(!installer->isBusy(),"Installation worker remains busy after exec.");
        report["installerBusyAtExecReturn"]=installer->isBusy();
        if(scenario=="update-preparation-pending") {
            require(storeLock.isLocked(),"The store lock was released before shutdown cancellation finished.");
            require(preparationFinished&&preparationError=="Installation cancelled.","Pending preparation was not cancelled by application shutdown.");
            require(index->plugin(Id).package.metadata["version"]=="1.0.0"&&!index->plugin(Id).updatePending,"Cancelled preparation published an update.");
            report["storeLockHeldAtExecReturn"]=true;report["preparationCancelled"]=true;
        }
        if(scenario=="unresponsive-deactivate")require(QFileInfo::exists(markers+"/1.0.0-deactivate"),"Unresponsive deactivate was not entered.");
        QJsonArray processes;
        for(const auto &identity:identities) {auto value=identity.json();value["exitedAtExecReturn"]=identity.state()==ProcessIdentity::State::Exited;processes.append(value);}
        report["processes"]=processes;report["idleAtExecReturn"]=true;report["sessionsAtExecReturn"]=0;report["resourcesAtExecReturn"]=0;
        report["installedVersionAtExit"]=index->plugin(Id).package.metadata["version"];
        report["enabledAtExit"]=index->plugin(Id).enabled;report["pendingAtExit"]=index->plugin(Id).updatePending;
        report["status"]="passed";
    } catch(const std::exception &failure) {
        report["status"]="failed";report["error"]=QString::fromUtf8(failure.what());
        if(auto *engine=app.getPluginEngine())report["log"]=engine->pluginLog(Id);
    }
    QSaveFile output(qEnvironmentVariable("NNP_APPLICATION_EXIT_RESULT"));
    const auto bytes=QJsonDocument(report).toJson();
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())return 21;
    return report["status"]=="passed"?0:20;
}
