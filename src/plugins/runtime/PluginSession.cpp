// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginSession.h"
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include "ProtocolProfile.h"
#include "WireContract.h"
#include "PluginDataService.h"
#include "PackageLease.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QRegularExpression>
#include <QUuid>
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
#include "WebviewService.h"
#endif
using PluginRpc::rpcError;
using PluginDiagnostics::Failure;
namespace PluginRuntime {
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
const QHash<QString,QString> capabilities={
    {"nnp.documents.getActive","documents.read@1"},{"nnp.documents.getSnapshot","documents.read@1"},
    {"nnp.documents.watch","documents.events@1"},{"nnp.documents.applyEdits","documents.write@1"},
    {"nnp.documents.watchAll","documents.delta@1"},{"nnp.documents.getMirrorSnapshot","documents.delta@1"},
    {"nnp.window.showMessage","messages@1"},
    {"nnp.commands.register","commands@1"},{"nnp.webviews.create","webviews@1"},{"nnp.webviews.reveal","webviews@1"},
    {"nnp.webviews.setHtml","webviews@1"},{"nnp.webviews.mapResource","webviews@1"},{"nnp.webviews.postMessage","webviews@1"},
    {"nnp.webviews.createCompatible","webviews.compat@1"},{"nnp.webviews.setDocument","webviews.compat@1"},{"nnp.webviews.mapAsset","webviews.compat@1"},{"nnp.webviews.setTitle","webviews.compat@1"},
    {"nnp.storage.get","storage@1"},{"nnp.storage.update","storage@1"},
    {"nnp.configuration.get","configuration@1"},{"nnp.configuration.update","configuration@1"},{"nnp.resources.dispose",{}}
};
bool string(const QJsonObject &params,const QString &key,int limit=128) { return params[key].isString() && !params[key].toString().isEmpty() && params[key].toString().toUcs4().size()<=limit; }
bool fields(const QJsonObject &params,const QStringList &required,const QStringList &optional={}) {
    for(const auto &key:required) if(!params.contains(key)) return false;
    for(auto it=params.constBegin();it!=params.constEnd();++it) if(it.key()!="sessionId" && it.key()!="generation" && !required.contains(it.key()) && !optional.contains(it.key())) return false;
    return true;
}
}
PluginSession::PluginSession(const QJsonObject &metadata,const QString &package,const QString &runtime,const QString &host,QMainWindow *window,PluginDocuments::DocumentBridge *bridge,QObject *parent,bool holdPublication,PluginData::Service *dataService,QString repository,std::shared_ptr<PluginPackages::PackageLease> lease,QString interpreter)
    : QObject(parent),manifest(metadata),packageLease(std::move(lease)),root(package),node(runtime),hostEntry(host),session(uuid()),dataRepository(std::move(repository)),python(std::move(interpreter)),documents(bridge),window(window),data(dataService),process(new PluginRpc::RpcProcess(capabilities.keys(),this)),publicationHeld(holdPublication) {
    process->holdLifetime(packageLease);
    stopDeadline.setSingleShot(true); stopDeadline.setInterval(3500);
    heartbeat.setInterval(10000);
    connect(&heartbeat,&QTimer::timeout,this,[this] {
        if(currentState!="active"||pingPending)return;
        pingPending=true;
        call("nnp.health.ping",envelope(),[this](const QJsonValue &result,const QJsonObject &error) {
            pingPending=false;if(currentState!="active")return;
            if(!error.isEmpty()||!result.isNull())fail("Plugin health check failed or timed out.",Failure::HealthTimeout);
        },2000);
    });
    connect(&stopDeadline,&QTimer::timeout,this,[this] { process->terminate(); });
    connect(process,&PluginRpc::RpcProcess::requestReceived,this,&PluginSession::dispatch);
    connect(process,&PluginRpc::RpcProcess::diagnostic,this,&PluginSession::diagnostic);
    connect(process,&PluginRpc::RpcProcess::stderrOutput,this,&PluginSession::stderrOutput);
    connect(process,&PluginRpc::RpcProcess::finished,this,[this](int code,const QString &reason) {
        stopDeadline.stop(); revoke(); busy=false;
        if(currentState=="stopping") setState("stopped");
        else if(currentState!="failed") {
            using Kind=PluginRpc::RpcProcess::FailureKind;
            switch(process->failureKind()) {
            case Kind::RuntimeUnavailable:failure=Failure::RuntimeUnavailable;break;
            case Kind::SupervisionFailure:failure=Failure::OwnershipFailure;break;
            case Kind::ProtocolFault:failure=Failure::ProtocolFault;break;
            default:failure=Failure::ProcessCrash;break;
            }
            setState("failed");emit failed(reason.isEmpty() ? QStringLiteral("Plugin process exited (%1).").arg(code) : reason);
        }
        emit processExited(code,reason);
        if(!queuedCommand.isEmpty()) { const auto id=queuedCommand; queuedCommand.clear(); emit commandFinished(id,{},rpcError(1004,"Plugin connection closed")); }
        emit stopped();
    });
    connect(process,&PluginRpc::RpcProcess::started,this,[this] {
        if(currentState!="starting") return;
        QString leaseError;
        if(packageLease&&!packageLease->pinRuntimeProcess(process->processId(),&leaseError)){fail(leaseError,Failure::OwnershipFailure);return;}
        setState("initializing"); QJsonArray capabilities; for(const auto &capability:grants) capabilities.append(capability);
        const QJsonObject params{{"pluginId",manifest["id"]},{"sessionId",session},{"generation",1},{"protocolVersions",QJsonArray{"0.1"}},
            {"hostVersion","0.15.0"},{"apiVersion","0.1.0"},{"grantedCapabilities",capabilities},
            {"limits",PluginRpc::profileLimits()}};
        call("nnp.initialize",params,[this,capabilities](const QJsonValue &value,const QJsonObject &error) {
            if(currentState!="initializing") return;
            const auto result=value.toObject();
            if(!error.isEmpty() || result["protocolVersion"]!="0.1" || result["apiVersion"]!="0.1.0" || result["capabilities"]!=capabilities || result["limits"]!=PluginRpc::profileLimits()) { fail("Plugin initialization or capability negotiation failed.",Failure::InitializationFailed); return; }
            setState("activating");
            call("nnp.plugin.activate",envelope({{"reason",activationReason}}),[this](const QJsonValue &,const QJsonObject &error) {
                if(currentState!="activating") return;
                if(!error.isEmpty()) { fail(error["message"].toString()); return; }
                for(const auto &command:manifest.value("contributes").toObject().value("commands").toArray())
                    if(!commands.contains(command.toObject().value("id").toString())){fail("Plugin activation did not register every declared command.");return;}
                setState("active");if(!publicationHeld)publishResources();invokePending();
            });
        });
    });
    if(documents) connect(documents,&PluginDocuments::DocumentBridge::event,this,&PluginSession::documentEvent);
    if(documents) connect(documents,&PluginDocuments::DocumentBridge::deltaEvent,this,&PluginSession::deltaEvent);
}
PluginSession::~PluginSession() { revoke(); process->terminate(); }
QJsonObject PluginSession::envelope(QJsonObject params) const { params["sessionId"]=session; params["generation"]=1; return params; }
QString PluginSession::call(const QString &method,const QJsonObject &params,PluginRpc::RpcProcess::Callback callback,int timeoutMs) {
    QString error;int code;
    if(!PluginRpc::WireContract::validate(method,"params",params,&error,&code)){callback({},rpcError(code,error));return {};}
    return process->call(method,params,[method,callback=std::move(callback)](const QJsonValue &result,const QJsonObject &failure) {
        QString message;int code;
        if(failure.isEmpty()&&!PluginRpc::WireContract::validate(method,"result",result,&message,&code))callback({},rpcError(code,message));
        else callback(result,failure);
    },timeoutMs);
}
void PluginSession::setState(const QString &state) {
    currentState=state;
    if(state=="active")heartbeat.start();else heartbeat.stop();
    emit stateChanged(state);
}
void PluginSession::activate(const QString &reason) {
    if(currentState!="idle") return;
    if(!documents || !window || !QFileInfo(node).isAbsolute() || !QFileInfo(hostEntry).isAbsolute() || !QFileInfo(hostEntry).isFile()) { fail("The controlled plugin runtime is unavailable.",Failure::RuntimeUnavailable); return; }
    QStringList supported={"documents.read@1","documents.events@1","documents.delta@1","documents.write@1","commands@1","messages@1"
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        ,"webviews@1"
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        ,"webviews.compat@1"
#endif
#endif
    };
    if(data)supported.append({"storage@1","configuration@1"});
    for(const auto &required:manifest["capabilities"].toObject()["required"].toArray()) {
        if(!supported.contains(required.toString())) { fail("Required capability is unavailable: "+required.toString(),Failure::CapabilityMissing); return; }
        grants.append(required.toString());
    }
    // Optional capabilities are not silently granted. An installation policy
    // can add explicit grants when its permission contract is implemented.
    QStringList arguments{hostEntry,root};
    if(manifest["runtime"].toObject()["kind"]=="python") {
        const auto relay=QFileInfo(hostEntry).dir().filePath("python-host.cjs");
        if(!QFileInfo(python).isAbsolute()||!QFileInfo(python).isExecutable()||!QFileInfo(relay).isFile()){fail("The configured Python runtime is unavailable.",Failure::RuntimeUnavailable);return;}
        arguments={relay,root,python};
    }
    activationReason=reason; setState("starting"); process->start(node,arguments,root);
}
void PluginSession::invoke(const QString &commandId,const QJsonArray &args) {
    bool declared=false; for(const auto &command:manifest["contributes"].toObject()["commands"].toArray()) if(command.toObject()["id"]==commandId) declared=true;
    if(!declared) { emit commandFinished(commandId,{},rpcError(-32602,"Command is not declared")); return; }
    if(currentState=="stopping" || currentState=="stopped" || currentState=="failed") { emit commandFinished(commandId,{},rpcError(1004,"Plugin is unavailable")); return; }
    if(busy || !queuedCommand.isEmpty()) return; // coalesce clicks during activation or an open command
    queuedCommand=commandId; queuedArgs=args;
    if(currentState=="idle") activate("command"); else if(currentState=="active") invokePending();
}
void PluginSession::invokePending() {
    if(currentState!="active" || publicationHeld || queuedCommand.isEmpty()) return;
    const auto id=queuedCommand; const auto args=queuedArgs; queuedCommand.clear(); queuedArgs={};
    if(!commands.contains(id)) { emit commandFinished(id,{},rpcError(-32602,"Plugin did not register the declared command")); return; }
    busy=true;
    QElapsedTimer elapsed;elapsed.start();
    commandRequest=call("nnp.commands.invoke",envelope({{"callbackId",commands[id].callbackId},{"args",args}}),[this,id,elapsed](const QJsonValue &result,const QJsonObject &error) { commandRequest.clear();busy=false;emit operationMeasured("nnp.commands.invoke",elapsed.elapsed(),error.isEmpty());emit commandFinished(id,result,error); });
    if(!messages.isEmpty())process->setDeadlinePaused(commandRequest,true);
}
void PluginSession::fail(const QString &sourceMessage,Failure category) {
    const auto message=PluginDiagnostics::displayMessage(sourceMessage);
    using Kind=PluginRpc::RpcProcess::FailureKind;
    switch(process->failureKind()) {
    case Kind::RuntimeUnavailable:category=Failure::RuntimeUnavailable;break;
    case Kind::SupervisionFailure:category=Failure::OwnershipFailure;break;
    case Kind::ProtocolFault:category=Failure::ProtocolFault;break;
    case Kind::ProcessCrash:case Kind::TransportFailure:category=Failure::ProcessCrash;break;
    default:break;
    }
    failure=category;
    setState("failed"); revoke(); emit failed(message);
    if(process->isRunning()) process->terminate();
    else {
        if(!queuedCommand.isEmpty()) { const auto id=queuedCommand; queuedCommand.clear(); emit commandFinished(id,{},rpcError(1005,message)); }
        emit stopped();
    }
}
void PluginSession::stop(const QString &reason) {
    if(currentState=="stopping" || currentState=="stopped") return;
    const bool active=currentState=="active" || currentState=="activating";
    setState("stopping"); revoke();
    if(!process->isRunning()) { setState("stopped"); emit stopped(); return; }
    stopDeadline.start();
    if(!active) { process->terminate(); return; }
    call("nnp.plugin.deactivate",envelope({{"reason",reason}}),[this,reason](const QJsonValue &,const QJsonObject &error) {
        if(currentState!="stopping") return;
        if(!error.isEmpty()) { process->terminate(); return; }
        call("nnp.shutdown",envelope({{"reason",reason}}),[this](const QJsonValue &,const QJsonObject &error) {
            if(currentState!="stopping") return;
            if(!error.isEmpty()) process->terminate();
            else QTimer::singleShot(1000,this,[this] { if(currentState=="stopping" && process->isRunning()) process->terminate(); });
        },1000);
    },2000);
}
void PluginSession::revoke() {
    watches.clear(); commands.clear();
    const auto oldMessages=messages;messages.clear();
    process->setDeadlinePaused(commandRequest,false);
    for(auto message:oldMessages)if(message){message->disconnect(this);message->close();message->deleteLater();}
    if(documents)for(int i=0;i<deltaWatches.size();++i)documents->releaseDeltaStream();deltaWatches.clear();
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    if(views) { views->clear(); delete views; views=nullptr; }
#endif
}
void PluginSession::publishResources() {
    if(currentState!="active")return;
    publicationHeld=false;
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    if(views)views->setPublished(true);
#endif
    invokePending();
}
int PluginSession::resourceCount() const {
    return watches.size()+deltaWatches.size()+commands.size()+messages.size()
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        +(views ? views->panelCount()+views->resourceCount() : 0)
#endif
    ;
}
void PluginSession::documentEvent(const QString &type,const QJsonValue &document,qint64 sequence) {
    if(type=="activeChanged") lastActiveDocument=document.toObject()["documentId"].toString();
    if((currentState!="active" && currentState!="activating") || watches.isEmpty()) return;
    if(type!="activeChanged" && document.toObject()["documentId"].toString()!=lastActiveDocument) return;
    for(const auto &subscription:watches) process->notify("nnp.documents.event",envelope({{"subscriptionId",subscription},{"eventSeq",sequence},{"type",type},{"document",document}}));
}
void PluginSession::deltaEvent(const QJsonObject &event,qint64 sequence) {
    if((currentState!="active"&&currentState!="activating")||deltaWatches.isEmpty())return;
    for(const auto &subscription:deltaWatches) {
        auto payload=event;payload["subscriptionId"]=subscription;payload["eventSeq"]=sequence;payload=envelope(payload);
        QString error;int code;if(!PluginRpc::WireContract::validate("nnp.documents.delta","params",payload,&error,&code)){fail(error,Failure::ProtocolFault);return;}
        process->notify("nnp.documents.delta",payload);
    }
}
void PluginSession::dispatch(const QString &method,const QJsonValue &value,const QString &token) {
    // Requests only. A notification cannot mutate host resources through a
    // method whose contract requires an acknowledged result.
    if(token.isEmpty()) { emit diagnostic("Ignored notification for a request-only host method."); return; }
    const auto params=value.toObject(); QJsonObject error; QJsonValue result(QJsonValue::Null);
    const auto reject=[this,&token](int code,const QString &message) { process->respond(token,{},rpcError(code,message)); };
    if(!value.isObject() || params["sessionId"]!=session || params["generation"]!=1) { reject(1004,"Stale plugin session"); return; }
    QString contractError;int contractCode;
    if(!PluginRpc::WireContract::validate(method,"params",value,&contractError,&contractCode)){reject(contractCode,contractError);return;}
    if(method=="nnp.resources.dispose") {
        if(currentState!="activating"&&currentState!="active"&&currentState!="stopping"&&currentState!="failed") {reject(1004,"Resources require an initialized plugin");return;}
        if(!fields(params,{"resourceId"}) || !string(params,"resourceId")) { reject(-32602,"Invalid resource handle"); return; }
        const auto id=params["resourceId"].toString(); watches.remove(id);if(deltaWatches.remove(id)&&documents)documents->releaseDeltaStream();
        if(auto message=messages.take(id)){message->disconnect(this);message->close();message->deleteLater();}
        if(messages.isEmpty())process->setDeadlinePaused(commandRequest,false);
        for(auto it=commands.begin();it!=commands.end();) { if(it->registrationId==id) it=commands.erase(it); else ++it; }
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        if(views) views->dispose(id);
#endif
        process->respond(token,result); return;
    }
    if(currentState!="active" && currentState!="activating") { reject(1004,"PluginStopping"); return; }
    const bool dataMethod=method.startsWith("nnp.storage.")||method.startsWith("nnp.configuration.");
    if(dataMethod&&!data) { reject(1005,"UnsupportedCapability"); return; }
    if(!grants.contains(capabilities.value(method))) { reject(1003,"CapabilityDenied"); return; }
    if((method=="nnp.documents.watchAll"||method=="nnp.documents.getMirrorSnapshot")&&!grants.contains("documents.read@1")){reject(1003,"Document delta streams require documents.read@1.");return;}
    if(method=="nnp.window.showMessage") {
        if(currentState!="active"||publicationHeld||!window){reject(1004,"Messages require a validated active plugin.");return;}
        if(messages.size()>=8){reject(1007,"Message limit reached");return;}
        const auto id=uuid();const auto severity=params["severity"].toString();
        const auto icon=severity=="info"?QMessageBox::Information:severity=="warning"?QMessageBox::Warning:QMessageBox::Critical;
        auto *message=new QMessageBox(icon,manifest["name"].toString(),params["message"].toString(),QMessageBox::NoButton,window);
        message->setObjectName("pluginMessage:"+manifest["id"].toString()+":"+id);message->setTextFormat(Qt::PlainText);message->setWindowModality(Qt::NonModal);
        QHash<QAbstractButton*,QString> actions;for(const auto &item:params["items"].toArray())actions.insert(message->addButton(item.toString(),QMessageBox::ActionRole),item.toString());
        if(actions.isEmpty())message->addButton(QMessageBox::Ok);
        messages.insert(id,message);
        process->setDeadlinePaused(commandRequest,true);
        connect(message,&QMessageBox::finished,this,[this,id,message,actions](int){
            if(!messages.remove(id))return;
            if(messages.isEmpty())process->setDeadlinePaused(commandRequest,false);
            const auto selected=actions.contains(message->clickedButton())?QJsonValue(actions.value(message->clickedButton())):QJsonValue(QJsonValue::Null);
            if(currentState=="active")process->notify("nnp.window.messageEvent",envelope({{"messageId",id},{"selected",selected}}));
            message->deleteLater();
        });
        message->show();process->respond(token,QJsonObject{{"messageId",id}});return;
    }
    if(dataMethod) {
        const bool update=method.endsWith(".update");
        if(!fields(params,{"key"},update?QStringList{"value","expectedValue"}:QStringList{})||!string(params,"key")) {reject(-32602,"Invalid plugin data params");return;}
        if(update&&(currentState!="active"||publicationHeld)) {reject(1004,"Data writes require a validated active plugin.");return;}
        const QPointer<PluginSession> owner(this);
        data->access(manifest["id"].toString(),method.startsWith("nnp.storage.")?"storage":"configuration",params["key"].toString(),update,params.value("value"),[owner,token](const PluginData::Result &result) {
            if(!owner)return;
            if(owner->currentState!="active"&&owner->currentState!="activating")owner->process->respond(token,{},rpcError(1004,"PluginStopping"));
            else owner->process->respond(token,result.value,result.error);
        },dataRepository,params.value("expectedValue"));
        return;
    }
    if((currentState!="active"||publicationHeld)&&method=="nnp.documents.applyEdits"){reject(1004,"Document writes require a validated active plugin.");return;}
    if(!documents) { reject(1002,"Document service is unavailable"); return; }
    if(method=="nnp.documents.getActive") {
        if(!fields(params,{})) { reject(-32602,"Invalid params"); return; } result=documents->active();
    } else if(method=="nnp.documents.getSnapshot") {
        if(!fields(params,{"documentId"}) || !string(params,"documentId")) { reject(-32602,"Invalid document handle"); return; } result=documents->snapshot(params["documentId"].toString(),&error);
    } else if(method=="nnp.documents.watch") {
        if(!fields(params,{"target","mode"}) || params["target"]!="active" || params["mode"]!="invalidate") { reject(-32602,"Invalid document subscription"); return; }
        if(watches.size()+deltaWatches.size()>=64) { reject(1007,"Subscription limit reached"); return; }
        const auto id=uuid(); watches.insert(id); const auto active=documents->active(); lastActiveDocument=active.toObject()["documentId"].toString();
        result=QJsonObject{{"subscriptionId",id},{"document",active},{"eventSeq",documents->eventSequence()}};
    } else if(method=="nnp.documents.watchAll") {
        const auto list=documents->list();if(list.size()>64||watches.size()+deltaWatches.size()>=64){reject(1007,"Document mirror or subscription limit reached");return;}
        const auto id=uuid();deltaWatches.insert(id);documents->retainDeltaStream();
        const auto active=documents->active();result=QJsonObject{{"subscriptionId",id},{"documents",list},{"activeDocumentId",active.isNull()?QJsonValue(QJsonValue::Null):active.toObject()["documentId"]},{"eventSeq",documents->deltaSequence()},{"maxDocuments",64},{"maxTotalUtf8Bytes",4194304}};
    } else if(method=="nnp.documents.getMirrorSnapshot") {
        if(!deltaWatches.contains(params["subscriptionId"].toString())){reject(1004,"Document mirror subscription is not owned by this session");return;}
        const auto snapshot=documents->snapshot(params["documentId"].toString(),&error);
        if(error.isEmpty())result=QJsonObject{{"snapshot",snapshot},{"eventSeq",documents->deltaSequence()}};
    } else if(method=="nnp.documents.applyEdits") {
        if(currentState!="active") { reject(1004,"Document writes require an active plugin"); return; }
        if(!fields(params,{"documentId","expectedVersion","edits"}) || !string(params,"documentId") || !params["expectedVersion"].isDouble() || !params["edits"].isArray()) { reject(-32602,"Invalid document edits"); return; }
        result=documents->applyEdits(params["documentId"].toString(),params["expectedVersion"].toInteger(-1),params["edits"].toArray(),&error);
    } else if(method=="nnp.commands.register") {
        if(!fields(params,{"commandId","callbackId"}) || !string(params,"commandId",192) || !string(params,"callbackId")) { reject(-32602,"Invalid command registration"); return; }
        const auto id=params["commandId"].toString(); bool declared=false;
        for(const auto &command:manifest["contributes"].toObject()["commands"].toArray()) if(command.toObject()["id"]==id) declared=true;
        if(!declared || commands.contains(id)) { reject(-32602,"Command is undeclared or already registered"); return; }
        const auto registration=uuid(); commands.insert(id,{registration,params["callbackId"].toString()}); result=QJsonObject{{"registrationId",registration}};
    }
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    else if(method.startsWith("nnp.webviews.")) {
        if(capabilities.value(method)=="webviews.compat@1"&&!grants.contains("webviews@1")){reject(1003,"Compatible Webviews require webviews@1 as well");return;}
        if(!views && (method=="nnp.webviews.create"||method=="nnp.webviews.createCompatible")) {
            const auto owner=QString::fromLatin1(QCryptographicHash::hash(session.toUtf8(),QCryptographicHash::Sha256).toHex().left(32));
            views=new PluginWebviews::WebviewService(manifest["id"].toString(),root,window,documents,this,owner);
            connect(views,&PluginWebviews::WebviewService::panelsChanged,this,&PluginSession::panelsChanged);
            views->setPublished(currentState=="active"&&!publicationHeld);
            connect(views,&PluginWebviews::WebviewService::event,this,[this](const QString &panelId,const QString &type,const QJsonValue &payload) {
                if(currentState=="active" || currentState=="activating") process->notify("nnp.webviews.event",envelope({{"panelId",panelId},{"type",type},{"payload",payload}}));
            });
        }
        if(!views) { reject(1004,"Panel closed"); return; }
        if(method=="nnp.webviews.createCompatible") {
            QStringList roots;for(const auto &entry:params["localResourceRoots"].toArray())roots.append(entry.toString());
            result=views->createCompatible(params["panelId"].toString(),params["viewType"].toString(),params["title"].toString(),params["enableScripts"].toBool(),roots,&error);
        } else if(method=="nnp.webviews.create") {
            if(!fields(params,{"viewType","title","position"}) || !string(params,"viewType") || !string(params,"title") || !QRegularExpression("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$").match(params["viewType"].toString()).hasMatch()) { reject(-32602,"Invalid panel declaration"); return; }
            result=views->create(params["viewType"].toString(),params["title"].toString(),params["position"].toString(),&error);
        } else {
            if(!string(params,"panelId")) { reject(-32602,"Invalid panel handle"); return; }
            const auto id=params["panelId"].toString();
            if(method=="nnp.webviews.setHtml") {
                if(!fields(params,{"panelId","html","renderContext"}) || !params["html"].isString() || !params["renderContext"].isObject()) { reject(-32602,"Invalid HTML publication"); return; }
                const QPointer<PluginRpc::RpcProcess> recipient=process;
                views->setHtmlAsync(id,params["html"].toString(),params["renderContext"].toObject(),[recipient,token](const QJsonObject &error) {
                    if(recipient)recipient->respond(token,QJsonValue(QJsonValue::Null),error);
                });
                return;
            } else if(method=="nnp.webviews.reveal") {
                if(!fields(params,{"panelId"},{"preserveFocus"}) || (params.contains("preserveFocus") && !params["preserveFocus"].isBool())) { reject(-32602,"Invalid panel reveal"); return; }
                views->reveal(id,params["preserveFocus"].toBool(),&error);
            } else if(method=="nnp.webviews.postMessage") {
                if(!fields(params,{"panelId","message"})) { reject(-32602,"Invalid panel message"); return; }
                result=views->postMessage(id,params["message"],&error);
            } else if(method=="nnp.webviews.mapResource") {
                if(!fields(params,{"panelId","resource"}) || !params["resource"].isObject()) { reject(-32602,"Invalid image resource"); return; }
                result=views->mapResource(id,params["resource"].toObject(),&error);
            } else if(method=="nnp.webviews.setDocument") {
                views->setDocument(id,params["html"].toString(),&error);
            } else if(method=="nnp.webviews.mapAsset") {
                result=views->mapAsset(id,params["path"].toString(),&error);
            } else if(method=="nnp.webviews.setTitle") {
                views->setTitle(id,params["title"].toString(),&error);
            }
        }
    }
#else
    else if(method.startsWith("nnp.webviews.")) { error=rpcError(1005,"WebEngine is not available in this build"); }
#endif
    else error=rpcError(1005,"UnsupportedCapability");
    if(error.isEmpty()&&!PluginRpc::WireContract::validate(method,"result",result,&contractError,&contractCode))error=rpcError(contractCode,contractError);
    process->respond(token,result,error);
}
}
