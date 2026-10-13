// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINSESSION_H
#define PLUGINSESSION_H
#include "RpcProcess.h"
#include "PluginDiagnostics.h"
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <memory>
class QMainWindow;
class QMessageBox;
namespace PluginDocuments { class DocumentBridge; }
namespace PluginWebviews { class WebviewService; }
namespace PluginData { class Service; }
namespace PluginPackages { class PackageLease; }
namespace PluginRuntime {
// A single activation generation. Installation and UI contributions outlive
// this object; every runtime registration belongs to this session only.
class PluginSession : public QObject {
    Q_OBJECT
public:
    PluginSession(const QJsonObject &manifest,const QString &packageRoot,const QString &node,const QString &hostEntry,
                  QMainWindow *window,PluginDocuments::DocumentBridge *documents,QObject *parent=nullptr,bool holdPublication=false,PluginData::Service *data=nullptr,QString dataRepository={},std::shared_ptr<PluginPackages::PackageLease> packageLease={},QString python={});
    ~PluginSession() override;
    void invoke(const QString &commandId,const QJsonArray &args={},const QJsonObject &invocation={});
    void activate(const QString &reason="startup");
    void stop(const QString &reason="disable");
    void forceStop() {process->terminate();}
    QString state() const { return currentState; }
    bool isTransportClosing() const { return process->isClosing(); }
    QString sessionId() const { return session; }
    PluginDiagnostics::Failure failureCategory() const {return failure;}
    int resourceCount() const;
    void publishResources();
    PluginWebviews::WebviewService *webviews() const { return views; }
    PluginWebviews::WebviewService *takeWebviewsForRestart(QObject *owner);
    void setRestoreSource(PluginWebviews::WebviewService *source);
    void restoreWebviews(std::function<void(const QJsonObject &)> completed);
    void commitWebviewRestoration();
signals:
    void panelsChanged();
    void stateChanged(const QString &state);
    void failed(const QString &message);
    void stopped();
    void commandFinished(const QString &commandId,const QJsonValue &result,const QJsonObject &error);
    void diagnostic(const QString &message);
    void stderrOutput(const QByteArray &bytes);
    void processExited(int exitCode,const QString &reason);
    void operationMeasured(const QString &method,qint64 durationMs,bool succeeded);
private:
    void setState(const QString &state);
    QJsonObject envelope(QJsonObject params={}) const;
    QString call(const QString &method,const QJsonObject &params,PluginRpc::RpcProcess::Callback callback,int timeoutMs=5000);
    void dispatch(const QString &method,const QJsonValue &params,const QString &token);
    void documentEvent(const QString &type,const QJsonValue &document,qint64 sequence);
    void deltaEvent(const QJsonObject &event,qint64 sequence);
    void invokePending();
    void fail(const QString &message,PluginDiagnostics::Failure category=PluginDiagnostics::Failure::ActivationFailed);
    void revoke();
    QJsonObject manifest;
    std::shared_ptr<PluginPackages::PackageLease> packageLease;
    QString root,node,hostEntry,session,currentState="idle",activationReason,queuedCommand,dataRepository,python;
    QJsonArray queuedArgs;
    QJsonObject queuedInvocation;
    QString apiVersion="0.1.0";
    QStringList grants;
    QPointer<PluginDocuments::DocumentBridge> documents;
    QPointer<QMainWindow> window;
    QPointer<PluginData::Service> data;
    PluginWebviews::WebviewService *views=nullptr;
    QPointer<PluginWebviews::WebviewService> restoreSource;
    bool panelsSuspended=false;
    PluginRpc::RpcProcess *process;
    struct Command { QString registrationId,callbackId; };
    QHash<QString,Command> commands;
    QSet<QString> watches;
    QSet<QString> deltaWatches;
    QHash<QString,QPointer<QMessageBox>> messages;
    QString lastActiveDocument;
    QString commandRequest;
    bool busy=false;
    QTimer stopDeadline;
    QTimer heartbeat;
    bool pingPending=false;
    bool publicationHeld=false;
    PluginDiagnostics::Failure failure=PluginDiagnostics::Failure::None;
};
}
#endif
