// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginEngine.h"
#include "../../PluginIcons.h"
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
#include "WebviewService.h"
#endif
#include "PluginSession.h"
#include "DocumentBridge.h"
#include "PluginDataService.h"
#include "PackageLease.h"
#include "PackageCollectionService.h"
#include <QDebug>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
using PluginDiagnostics::Failure;
namespace PluginRuntime {
RuntimePaths RuntimePaths::bundled() {
    const auto base=QCoreApplication::applicationDirPath();
    QString python;
#ifdef NOTEPADNEXT_PLUGIN_PYTHON_PATH
    python=QStringLiteral(NOTEPADNEXT_PLUGIN_PYTHON_PATH);
#endif
#ifdef Q_OS_MACOS
    return {QDir::cleanPath(base+"/../Helpers/nnp-node"),QDir::cleanPath(base+"/../Resources/plugin-host/host.cjs"),python};
#elif defined(Q_OS_WIN)
    return {base+"/plugins/runtime/node.exe",base+"/plugins/host/host.cjs",python};
#else
    return {base+"/plugins/runtime/node",base+"/plugins/host/host.cjs",python};
#endif
}
PluginPackages::PackagePolicy PluginEngine::packagePolicy() {
    PluginPackages::PackagePolicy policy;
    policy.capabilities={"commands@1","documents.read@1","documents.events@1","documents.delta@1","documents.write@1","messages@1","storage@1","configuration@1"};
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    policy.capabilities.append("webviews@1");
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    policy.capabilities.append("webviews.compat@1");
#endif
#endif
    return policy;
}
PluginEngine::PluginEngine(PluginPackages::InstalledPluginRegistry *installedRegistry,PluginPackages::InstallationService *installService,PluginDocuments::DocumentBridge *documentBridge,RuntimePaths runtimePaths,QObject *parent)
    :PluginController(parent),registry(installedRegistry),installer(installService),documents(documentBridge),paths(std::move(runtimePaths)) {
    data=new PluginData::Service(QDir(QFileInfo(registry->packageStorePath()).absolutePath()).filePath("data"),this);
    diagnostics=new PluginDiagnostics::Service(QDir(QFileInfo(registry->packageStorePath()).absolutePath()).filePath("diagnostics"),this);
    connect(diagnostics,&PluginDiagnostics::Service::idle,this,&PluginEngine::checkIdle);
    connect(diagnostics,&PluginDiagnostics::Service::unavailable,this,[this](const QString &message){diagnosticError=message;qWarning()<<"Plugin diagnostics:"<<message;for(const auto &p:registry->plugins())emit logsChanged(p.id());});
    connect(diagnostics,&PluginDiagnostics::Service::available,this,[this]{diagnosticError.clear();for(const auto &p:registry->plugins())emit logsChanged(p.id());});
    connect(diagnostics,&PluginDiagnostics::Service::restored,this,[this](const QJsonArray &events,const QJsonArray &lastFailures){restoredLogs=events;restoredFailures=lastFailures;for(const auto &p:registry->plugins())emit logsChanged(p.id());});
    collection=new PluginPackages::PackageCollectionService(registry,this);
    connect(collection,&PluginPackages::PackageCollectionService::idle,this,&PluginEngine::checkIdle);
    connect(collection,&PluginPackages::PackageCollectionService::finished,this,[this](const PluginPackages::CollectionResult &result) {
        if(!result.error.isEmpty())qWarning()<<"Plugin package collection:"<<result.error;
        for(const auto &entry:result.preserved)qWarning()<<"Plugin package preserved:"<<entry;
    });
    connect(data,&PluginData::Service::idle,this,&PluginEngine::checkIdle);
    connect(registry,&PluginPackages::InstalledPluginRegistry::changed,this,[this]{syncRegistry();});
    connect(installer,&PluginPackages::InstallationService::busyChanged,this,[this](bool busy){if(!busy)collection->request();checkIdle();});
    connect(installer,&PluginPackages::InstallationService::updatePrepared,this,&PluginEngine::installUpdate);
    connect(documents,&PluginDocuments::DocumentBridge::event,this,[this](const QString &type,const QJsonValue &document,qint64){
        if(type!="activeChanged"&&type!="metadataChanged")return;
        if(!document.isObject())return;
        const auto event="onLanguage:"+document.toObject()["languageId"].toString();
        for(const auto &p:registry->plugins())if(p.enabled&&p.package.metadata["activationEvents"].toArray().contains(event)&&!failures.contains(p.id()))request(p.id(),{},event);
    });
}
void PluginEngine::attachWindow(QMainWindow *value,QMenu *pluginsMenu) {
    Q_ASSERT(!window);window=value;menu=pluginsMenu;
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    PluginWebviews::prepareWindow(value);
#endif
    toolbar=new QToolBar(tr("Plugin Commands"),value);toolbar->setObjectName("pluginCommandsToolbar");value->addToolBar(toolbar);
    connect(value,&QObject::destroyed,this,[this]{window=nullptr;menu=nullptr;toolbar=nullptr;shutdown();});
    QTimer::singleShot(0,this,[this]{syncRegistry();});
}
void PluginEngine::syncRegistry(bool activate) {
    QSet<QString> retained;for(const auto &p:registry->plugins())retained.insert(p.id());
    for(const auto &id:logs.keys())if(!retained.contains(id)){logs.remove(id);logRepositories.remove(id);failures.remove(id);failureCategories.remove(id);}
    for(const auto &action:actions)if(action)delete action;
    actions.clear();
    for(const auto &pluginMenu:pluginMenus)if(pluginMenu)delete pluginMenu;
    pluginMenus.clear();
    if(!registry->loadError().isEmpty()) {
        for(auto *session:sessions.values())session->stop("storage-error");
        if(toolbar)toolbar->hide();return;
    }
    for(const auto &id:sessions.keys())if(!registry->plugin(id).enabled&&!updating.contains(id)&&sessions.contains(id))sessions[id]->stop("disable");
    if(!window||!menu||!toolbar)return;
    struct Contribution {int order;QString id,command,title,icon;bool tool;};QVector<Contribution> contributions;
    for(const auto &p:registry->plugins()) {
        if(!p.enabled||removing.contains(p.id()))continue;
        const auto data=p.package.metadata["contributes"].toObject();QHash<QString,QJsonObject> commands;
        for(const auto &item:data["commands"].toArray())commands.insert(item.toObject()["id"].toString(),item.toObject());
        for(const auto &key:QStringList{"menus","toolbar"})for(const auto &item:data[key].toArray()) {
            const auto c=item.toObject(),command=commands.value(c["command"].toString());
            contributions.append({c["order"].toInt(100),p.id(),c["command"].toString(),command["title"].toString(),command["icon"].toString(),key=="toolbar"});
        }
    }
    std::stable_sort(contributions.begin(),contributions.end(),[](const Contribution &a,const Contribution &b){return a.order!=b.order?a.order<b.order:(a.id+a.command)<(b.id+b.command);});
    QHash<QString,QMenu*> grouped;
    for(const auto &c:contributions) {
        // Package icons are displayed only after worker integrity validation;
        // placeholders cannot trigger synchronous decoding of untrusted files.
        auto *action=new QAction(PluginIcons::hostIcon(registry->plugin(c.id).package.metadata,window->palette()),QString(c.title).replace('&',"&&"),this);
        // Registry updates recreate actions. A retained session pins the exact
        // package that already passed worker verification, including its icons.
        const auto session=sessions.value(c.id,nullptr);
        const auto package=registry->plugin(c.id).package;
        if(session&&session->property("verifiedPackageDigest").toString()==package.digest&&!c.icon.isEmpty()&&!PluginIcons::isMarkdown(package.metadata))
            action->setIcon(QIcon(package.root+'/'+c.icon));
        if(PluginIcons::isMarkdown(package.metadata))action->setProperty("hostBrandResource",":/icons/markdown-preview.svg");
        action->setToolTip(c.title);
        action->setObjectName("pluginCommand:"+c.command);action->setProperty("pluginId",c.id);action->setProperty("commandId",c.command);action->setEnabled(!quitting&&!updating.contains(c.id));
        connect(action,&QAction::triggered,this,[this,c]{invoke(c.id,c.command);});
        if(c.tool)toolbar->addAction(action);
        else {
            if(!grouped.contains(c.id)) {
                const auto name=registry->plugin(c.id).package.metadata.value("name").toString();
                auto *pluginMenu=new QMenu(QString(name).replace('&',"&&"),menu);pluginMenu->setObjectName("pluginMenu:"+c.id);menu->addMenu(pluginMenu);grouped.insert(c.id,pluginMenu);pluginMenus.append(pluginMenu);
            }
            grouped[c.id]->addAction(action);
        }
        actions.append(action);
    }
    toolbar->setVisible(!toolbar->actions().isEmpty());
    const auto languageEvent="onLanguage:"+documents->active().toObject().value("languageId").toString();
    for(const auto &p:registry->plugins())if(activate&&p.enabled&&!quitting&&!updating.contains(p.id())&&!failures.contains(p.id())) {
        const auto events=p.package.metadata.value("activationEvents").toArray();
        if(events.contains("onStartup"))request(p.id(),{},"startup");
        else if(events.contains(languageEvent))request(p.id(),{},languageEvent);
    }
}
QString PluginEngine::pluginState(const QString &id) const {
    if(removing.contains(id))return "stopping";
    if(updating.contains(id))return "updating";
    if(restartTickets.contains(id))return "restarting";
    if(waiting.contains(id))return "stopping";
    if(verifying.contains(id))return "verifying";
    if(sessions.contains(id))return sessions.value(id)->state();
    if(failures.contains(id))return "failed";
    return registry->plugin(id).enabled?"ready":"disabled";
}
int PluginEngine::pluginPanelCount(const QString &id) const {
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    const auto session=sessions.value(id,nullptr);
    return session&&session->webviews()?session->webviews()->panelCount():0;
#else
    Q_UNUSED(id);return 0;
#endif
}
QString PluginEngine::pluginFailureCategory(const QString &id) const {return failures.contains(id)?PluginDiagnostics::label(failureCategories.value(id,Failure::None)):QString();}
QString PluginEngine::pluginLog(const QString &id) const {
    const auto repository=registry->plugin(id).package.repository;QByteArray text;
    for(const auto &value:restoredLogs){const auto event=value.toObject();if(event["pluginId"]==id&&event["repository"]==repository)text+=PluginDiagnostics::Store::format(event).toUtf8();}
    if(logRepositories.value(id)==repository)text+=logs.value(id);
    if(text.size()>32*1024)text.remove(0,text.size()-32*1024);
    QString previous;
    for(const auto &value:restoredFailures){const auto event=value.toObject();if(event["pluginId"]==id&&event["repository"]==repository)previous=tr("Previous recorded failure:")+"\n"+PluginDiagnostics::Store::format(event)+"\n";}
    const auto unavailable=diagnosticError.isEmpty()?QString():tr("Saved diagnostics are unavailable: %1").arg(diagnosticError)+"\n\n";
    return unavailable+previous+QString::fromUtf8(text);
}
void PluginEngine::log(const QString &id,const QByteArray &text,Failure category,const QString &kind,const QJsonObject &details) {
    const auto repository=registry->plugin(id).package.repository;if(repository.isEmpty())return;
    if(logRepositories.value(id)!=repository){logs.remove(id);logRepositories[id]=repository;}
    const auto session=sessions.value(id,nullptr);
    const auto event=PluginDiagnostics::Store::event(id,repository,session?session->sessionId():QString(),kind,text,category,details);
    diagnostics->append(event);
    auto &buffer=logs[id];buffer.append(PluginDiagnostics::Store::format(event).toUtf8());
    if(buffer.size()>32*1024)buffer.remove(0,buffer.size()-32*1024);
    emit logsChanged(id);
}
bool PluginEngine::setEnabled(const QString &id,bool enabled,QString *message) {
    if(quitting||updating.contains(id)||removing.contains(id)){if(message)*message=tr("Wait for shutdown or the plugin operation to finish.");return false;}
    const bool ok=registry->setEnabled(id,enabled,message);
    if(ok){failures.remove(id);restartTickets.remove(id);crashTimes.remove(id);if(enabled)syncRegistry();}
    if(ok&&!enabled){if(verifying.contains(id))verifying[id].cancel->store(true);if(sessions.contains(id))sessions[id]->stop("disable");}
    if(ok)emit statusChanged(id);return ok;
}
void PluginEngine::invoke(const QString &id,const QString &command) {
    const auto p=registry->plugin(id);bool declared=false;
    for(const auto &item:p.package.metadata["contributes"].toObject()["commands"].toArray())if(item.toObject()["id"]==command)declared=true;
    if(!declared||!p.enabled||quitting||waiting.contains(id)||updating.contains(id)||removing.contains(id))return;
    failures.remove(id);restartTickets.remove(id);crashTimes.remove(id);
    if(auto *session=sessions.value(id,nullptr);session&&(session->state()=="failed"||session->state()=="stopped")) {
        afterStop(id,[this,id,command]{request(id,command,"command");});return;
    }
    request(id,command,"command");
}
void PluginEngine::request(const QString &id,const QString &command,const QString &reason) {
    bool validating=updating.contains(id)&&reason=="update-validation";
    if(quitting||!registry->loadError().isEmpty()||waiting.contains(id)||removing.contains(id)||!window||(!registry->plugin(id).enabled&&!validating)||(updating.contains(id)&&!validating))return;
    if(registry->plugin(id).validationDeferred) {
        updating.insert(id);QString message;
        if(!registry->startValidation(id,&message)){updating.remove(id);failures[id]=message;failureCategories[id]=Failure::UpdateFailed;log(id,message.toUtf8(),Failure::UpdateFailed,"failure");emit error(id,message);emit statusChanged(id);return;}
        validating=true;
    }
    if(auto *session=sessions.value(id,nullptr)) {if(!command.isEmpty()&&session->state()!="stopping")session->invoke(command);return;}
    if(verifying.contains(id)){if(!command.isEmpty())verifying[id].command=command;return;}
    if(!QFileInfo(paths.node).isExecutable()||!QFileInfo(paths.hostEntry).isFile()||!QDir::isAbsolutePath(paths.node)||!QDir::isAbsolutePath(paths.hostEntry)) {
        const auto message=tr("The bundled Node 24 runtime or plugin host is missing. Repair the application installation.");failures[id]=message;failureCategories[id]=Failure::RuntimeUnavailable;log(id,message.toUtf8(),Failure::RuntimeUnavailable,"failure");emit statusChanged(id);emit error(id,message);if(validating)failUpdate(id,message);return;
    }
    if(registry->plugin(id).package.metadata["runtime"].toObject()["kind"]=="python"&&(!QDir::isAbsolutePath(paths.python)||!QFileInfo(paths.python).isExecutable()||!QFileInfo(QFileInfo(paths.hostEntry).dir().filePath("python-host.cjs")).isFile())) {
        const auto message=tr("This build has no configured Python plugin runtime. Configure an absolute Python 3.12 interpreter path.");failures[id]=message;failureCategories[id]=Failure::RuntimeUnavailable;log(id,message.toUtf8(),Failure::RuntimeUnavailable,"failure");emit statusChanged(id);emit error(id,message);if(validating)failUpdate(id,message);return;
    }
    const auto p=registry->plugin(id);const auto store=registry->packageStorePath();
    auto cancel=std::make_shared<std::atomic_bool>(false);verifying.insert(id,{cancel,command,reason});emit statusChanged(id);
    struct Outcome {bool ok=false;QString error;std::shared_ptr<PluginPackages::PackageLease> lease;};auto outcome=std::make_shared<Outcome>();
    auto *thread=QThread::create([p,store,cancel,outcome]{
        outcome->lease=PluginPackages::PackageLease::acquire(store,p.package.digest,&outcome->error,1000);
        outcome->ok=outcome->lease&&PluginPackages::PackageStore(store).verify(p.package,&outcome->error,cancel.get());
    });
    connect(thread,&QThread::finished,this,[this,id,digest=p.package.digest,cancel,outcome] {
        const auto v=verifying.take(id);
        if(!cancel->load()&&outcome->ok&&registry->plugin(id).package.digest==digest&&(registry->plugin(id).enabled||updating.contains(id))&&!quitting)startVerified(id,v.command,v.reason,outcome->lease);
        else if(!cancel->load()&&!outcome->ok){failures[id]=outcome->error;failureCategories[id]=Failure::PackageInvalid;log(id,outcome->error.toUtf8(),Failure::PackageInvalid,"failure");emit error(id,outcome->error);if(updating.contains(id))failUpdate(id,outcome->error);}
        emit statusChanged(id);
        if(waiting.contains(id)){auto action=waiting.take(id);action();}checkIdle();
    });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);thread->start();
}
void PluginEngine::startVerified(const QString &id,const QString &command,const QString &reason,const std::shared_ptr<PluginPackages::PackageLease> &lease) {
    if(!window||quitting)return;
    const auto p=registry->plugin(id);
    auto *session=new PluginSession(p.package.metadata,p.package.root,paths.node,paths.hostEntry,window,documents,this,updating.contains(id),data,p.package.repository,lease,paths.python);sessions.insert(id,session);
    session->setProperty("verifiedPackageDigest",p.package.digest);
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    connect(session,&PluginSession::panelsChanged,this,[this,id]{emit statusChanged(id);});
#endif
    for(const auto &action:actions)if(action&&action->property("pluginId")==id) {
        for(const auto &item:p.package.metadata["contributes"].toObject()["commands"].toArray())if(item.toObject()["id"]==action->property("commandId").toString()) {
            const auto icon=item.toObject()["icon"].toString();if(!icon.isEmpty()&&!PluginIcons::isMarkdown(p.package.metadata))action->setIcon(QIcon(p.package.root+'/'+icon));
        }
    }
    auto becameActive=std::make_shared<bool>(false);
    connect(session,&PluginSession::stateChanged,this,[this,id,session,becameActive](const QString &state){
        if(state=="active")*becameActive=true;
        log(id,("state: "+state).toUtf8(),Failure::None,"state");emit statusChanged(id);
        if(state=="active"&&updating.contains(id))QTimer::singleShot(0,this,[this,id,session]{confirmUpdate(id,session);});
    });
    connect(session,&PluginSession::stderrOutput,this,[this,id](const QByteArray &bytes){log(id,bytes,Failure::None,"stderr");});
    connect(session,&PluginSession::diagnostic,this,[this,id](const QString &message){log(id,message.toUtf8());});
    connect(session,&PluginSession::failed,this,[this,id,session,digest=p.package.digest,becameActive](const QString &message){
        failures[id]=message;failureCategories[id]=session->failureCategory();log(id,message.toUtf8(),session->failureCategory(),"failure");emit error(id,message);emit statusChanged(id);
        const auto current=registry->plugin(id);
        if(updating.contains(id)&&current.updatePending&&current.package.digest==digest)failUpdate(id,message);
        else if(*becameActive&&!updating.contains(id))scheduleRecovery(id,digest);
    });
    connect(session,&PluginSession::commandFinished,this,[this,id,session](const QString &,const QJsonValue &,const QJsonObject &failure){
        if(failure.isEmpty())return;
        // Closed queued calls are consequences of the already reported
        // activation/process failure, or an intentional management stop.
        // Keep that primary classification and do not report a second fault.
        if(session->state()=="stopping"||session->state()=="stopped"||session->state()=="failed"||quitting)return;
        const auto message=PluginDiagnostics::displayMessage(failure["message"].toString());log(id,message.toUtf8(),Failure::CommandFailed,"failure");emit error(id,message);
    });
    connect(session,&PluginSession::processExited,this,[this,id](int code,const QString &reason){log(id,reason.toUtf8(),Failure::None,"exit",{{"exitCode",code}});});
    connect(session,&PluginSession::operationMeasured,this,[this,id](const QString &method,qint64 duration,bool ok){log(id,ok?QByteArray("Command completed."):QByteArray("Command did not complete successfully."),Failure::None,"command",{{"method",method},{"durationMs",duration}});});
    connect(session,&PluginSession::stopped,this,[this,id,session] {
        if(sessions.value(id)!=session)return;
        sessions.remove(id);session->deleteLater();emit statusChanged(id);
        collection->request();
        if(waiting.contains(id)){auto action=waiting.take(id);action();}checkIdle();
    });
    if(command.isEmpty())session->activate(reason);else session->invoke(command);
}
void PluginEngine::afterStop(const QString &id,std::function<void()> action) {
    if(waiting.contains(id)){emit operationFinished(id,tr("Another lifecycle operation is in progress."));return;}
    failures.remove(id);
    if(sessions.contains(id)||verifying.contains(id)) {
        waiting.insert(id,std::move(action));
        if(verifying.contains(id))verifying[id].cancel->store(true);
        if(sessions.contains(id))sessions[id]->stop("management");
        emit statusChanged(id);
    }else action();
}
void PluginEngine::reload(const QString &id) {
    if(quitting||updating.contains(id)||removing.contains(id)||!registry->plugin(id).enabled){emit operationFinished(id,tr("Enable the plugin and wait for its operation before reloading it."));return;}
    restartTickets.remove(id);crashTimes.remove(id);
    afterStop(id,[this,id]{request(id,{},"reload");emit operationFinished(id,{});});
}
void PluginEngine::uninstall(const QString &id,bool keepData) {
    if(quitting||updating.contains(id)||removing.contains(id)||waiting.contains(id)||registry->plugin(id).id()!=id)return;
    const auto repository=registry->plugin(id).package.repository;
    removing.insert(id);syncRegistry(false);emit statusChanged(id);
    restartTickets.remove(id);crashTimes.remove(id);
    afterStop(id,[this,id,keepData,repository] {
        QString message;
        if(!registry->remove(id,&message)) {
            removing.remove(id);syncRegistry(false);emit statusChanged(id);emit operationFinished(id,message);checkIdle();return;
        }
        // Queue deletion behind writes already accepted by this generation.
        // Registration metadata and package revisions have separate owners.
        if(keepData){removing.remove(id);emit statusChanged(id);emit operationFinished(id,{});checkIdle();return;}
        data->erase(id,[this,id](const PluginData::Result &result) {
            removing.remove(id);emit statusChanged(id);
            emit operationFinished(id,result.error.isEmpty()?QString():tr("Plugin uninstalled, but its data could not be deleted: %1").arg(result.error["message"].toString()));checkIdle();
        },repository);
    });
}
void PluginEngine::installUpdate(const PluginPackages::PreparedPackage &package,const QStringList &sources,const QJsonObject &origin,const QString &expectedDigest) {
    const auto id=package.metadata.value("id").toString();
    if(quitting||updating.contains(id)||removing.contains(id)||waiting.contains(id)||registry->plugin(id).package.digest!=expectedDigest) {
        emit operationFinished(id,tr("The plugin changed while its update was being prepared. Try again."));return;
    }
    const bool wasActive=sessions.contains(id)&&sessions.value(id)->state()=="active";
    restartTickets.remove(id);crashTimes.remove(id);updating.insert(id);syncRegistry();emit statusChanged(id);
    afterStop(id,[this,id,package,sources,origin,expectedDigest,wasActive] {
        QString message;
        if(!registry->beginUpdate(package,sources,origin,expectedDigest,&message,!wasActive)) {
            updating.remove(id);syncRegistry();if(registry->plugin(id).enabled)request(id,{},"update-rejected");emit statusChanged(id);emit operationFinished(id,message);return;
        }
        if(!wasActive){updating.remove(id);syncRegistry(false);log(id,"Package updated without execution; activation validation deferred until first use.");emit statusChanged(id);emit operationFinished(id,{});return;}
        request(id,{},"update-validation");
    });
}
void PluginEngine::rollback(const QString &id) {
    const auto current=registry->plugin(id);
    if(quitting||updating.contains(id)||removing.contains(id)||waiting.contains(id)||!current.previous){emit operationFinished(id,tr("No previous version is available now."));return;}
    const bool wasActive=sessions.contains(id)&&sessions.value(id)->state()=="active";
    restartTickets.remove(id);crashTimes.remove(id);updating.insert(id);syncRegistry();emit statusChanged(id);
    afterStop(id,[this,id,digest=current.package.digest,deferred=current.validationDeferred,wasActive] {
        QString message;
        const bool ok=deferred?registry->abortUpdate(id,&message):registry->beginRollback(id,digest,&message,!wasActive);
        if(!ok) {
            updating.remove(id);syncRegistry();if(registry->plugin(id).enabled)request(id,{},"rollback-rejected");emit statusChanged(id);emit operationFinished(id,message);return;
        }
        if(!wasActive){updating.remove(id);syncRegistry(false);emit statusChanged(id);emit operationFinished(id,{});return;}
        request(id,{},"update-validation");
    });
}
void PluginEngine::confirmUpdate(const QString &id,PluginSession *session) {
    if(quitting||!updating.contains(id)||sessions.value(id)!=session||session->state()!="active")return;
    QString message;
    if(!registry->confirmUpdate(id,&message)){failUpdate(id,message);return;}
    session->publishResources();
    updating.remove(id);failures.remove(id);syncRegistry();
    if(!registry->plugin(id).enabled&&sessions.value(id)==session)session->stop("update-validation-complete");
    log(id,"Update activation validated; previous revision retained.");emit statusChanged(id);emit operationFinished(id,{});
}
void PluginEngine::failUpdate(const QString &id,const QString &message) {
    // Defer out of a session failure signal: process exit also emits stopped,
    // and rollback must never attach a replacement inside that same emission.
    QTimer::singleShot(0,this,[this,id,message] {
        if(quitting||!updating.contains(id)||waiting.contains(id))return;
        afterStop(id,[this,id,message] {
            QString recoveryError;
            const bool restored=registry->abortUpdate(id,&recoveryError);
            updating.remove(id);failures.remove(id);
            if(!restored){failures[id]=recoveryError;failureCategories[id]=Failure::UpdateFailed;}
            log(id,(restored?tr("Update failed; previous version restored: ")+message:recoveryError).toUtf8(),Failure::UpdateFailed,"failure");
            syncRegistry();
            if(restored&&registry->plugin(id).enabled)request(id,{},"rollback-recovery");
            emit statusChanged(id);emit operationFinished(id,restored?tr("Update failed. The previous version was restored. %1").arg(message):recoveryError);
        });
    });
}
void PluginEngine::scheduleRecovery(const QString &id,const QString &digest) {
    if(quitting||updating.contains(id)||removing.contains(id)||waiting.contains(id)||!registry->plugin(id).enabled||registry->plugin(id).package.digest!=digest)return;
    const auto now=QDateTime::currentMSecsSinceEpoch();auto &times=crashTimes[id];
    while(!times.isEmpty()&&now-times.first()>60000)times.removeFirst();
    if(times.size()>=3){log(id,"Automatic recovery stopped after three restarts within 60 seconds. Reload or enable the plugin to retry.");return;}
    times.append(now);const auto ticket=++nextRestartTicket;restartTickets[id]=ticket;
    const int delay=1000*(1<<(times.size()-1));log(id,QString("Automatic recovery in %1 ms.").arg(delay).toUtf8());emit statusChanged(id);
    QTimer::singleShot(delay,this,[this,id,digest,ticket] {
        if(restartTickets.value(id)!=ticket)return;restartTickets.remove(id);
        if(!quitting&&!waiting.contains(id)&&!updating.contains(id)&&registry->plugin(id).enabled&&registry->plugin(id).package.digest==digest) {
            afterStop(id,[this,id,digest] {
                if(registry->plugin(id).package.digest==digest)request(id,{},"crash-recovery");
            });
        }
        emit statusChanged(id);checkIdle();
    });
}
int PluginEngine::resourceCount() const {int total=0;for(auto *session:sessions)total+=session->resourceCount();return total;}
bool PluginEngine::isIdle() const {return sessions.isEmpty()&&verifying.isEmpty()&&restartTickets.isEmpty()&&removing.isEmpty()&&data->isIdle()&&collection->isIdle()&&diagnostics->isIdle()&&!installer->isBusy();}
void PluginEngine::checkIdle() {if(isIdle())emit idle();}
void PluginEngine::shutdown() {
    quitting=true;collection->shutdown();installer->cancel();diagnostics->flush();
    for(const auto &id:waiting.keys())removing.remove(id); // Uncommitted management operations are cancelled.
    waiting.clear();restartTickets.clear();
    for(auto &v:verifying)v.cancel->store(true);
    for(const auto &action:actions)if(action)action->setEnabled(false);
    for(auto *session:sessions.values())session->stop("shutdown");checkIdle();
}
void PluginEngine::forceStop() {for(auto *session:sessions.values())session->forceStop();installer->cancel();}
}
