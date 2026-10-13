// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINENGINE_H
#define PLUGINENGINE_H
#include "PluginController.h"
#include "InstallationService.h"
#include "PluginDiagnostics.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <functional>
class QAction;
class QMainWindow;
class QMenu;
class QToolBar;
namespace PluginDocuments {class DocumentBridge;}
namespace PluginData {class Service;}
namespace PluginPackages {class PackageCollectionService;}
namespace PluginWebviews {class WebviewService;}
namespace PluginRuntime {
class PluginSession;
struct RuntimePaths {
    QString node,hostEntry,python;
    static RuntimePaths bundled();
};
class PluginEngine:public PluginPackages::PluginController {
    Q_OBJECT
public:
    PluginEngine(PluginPackages::InstalledPluginRegistry *registry,PluginPackages::InstallationService *installer,
                 PluginDocuments::DocumentBridge *documents,RuntimePaths paths,QObject *parent=nullptr);
    void attachWindow(QMainWindow *window,QMenu *menu);
    QString pluginState(const QString &id) const override;
    int pluginPanelCount(const QString &id) const override;
    QString pluginLog(const QString &id) const override;
    QString pluginFailureCategory(const QString &id) const override;
    QString pluginFailureMessage(const QString &id) const override {return failures.value(id);}
    bool setEnabled(const QString &id,bool enabled,QString *error) override;
    void reload(const QString &id) override;
    void uninstall(const QString &id,bool keepData) override;
    void invoke(const QString &id,const QString &command) override;
    void rollback(const QString &id) override;
    void shutdown();
    void forceStop();
    bool isIdle() const;
    int sessionCount() const {return sessions.size();}
    int resourceCount() const;
    static PluginPackages::PackagePolicy packagePolicy();
signals:
    void idle();
    void error(const QString &pluginId,const QString &message);
private:
    void syncRegistry(bool activate=true);
    void request(const QString &id,const QString &command,const QString &reason);
    void startVerified(const QString &id,const QString &command,const QString &reason,const std::shared_ptr<PluginPackages::PackageLease> &lease);
    void afterStop(const QString &id,std::function<void()> action);
    void stopPreservingPanels(const QString &id,std::function<void()> action);
    void releasePreservedPanels(const QString &id);
    bool hasPreservedPanels(const QString &id) const;
    void checkIdle();
    void log(const QString &id,const QByteArray &text,PluginDiagnostics::Failure category=PluginDiagnostics::Failure::None,const QString &kind="diagnostic",const QJsonObject &details={});
    void installUpdate(const PluginPackages::PreparedPackage &package,const QStringList &sources,const QJsonObject &origin,const QString &expectedDigest);
    void failUpdate(const QString &id,const QString &message);
    void confirmUpdate(const QString &id,PluginSession *session);
    void scheduleRecovery(const QString &id,const QString &digest);
    PluginPackages::InstalledPluginRegistry *registry;
    PluginPackages::InstallationService *installer;
    PluginDocuments::DocumentBridge *documents;
    PluginData::Service *data;
    PluginPackages::PackageCollectionService *collection;
    PluginDiagnostics::Service *diagnostics;
    RuntimePaths paths;
    QPointer<QMainWindow> window;
    QPointer<QMenu> menu;
    QPointer<QToolBar> toolbar;
    QList<QPointer<QAction>> actions;
    QList<QPointer<QMenu>> pluginMenus;
    QHash<QString,PluginSession*> sessions;
    struct Verification {std::shared_ptr<std::atomic_bool> cancel;QString command,reason;};
    QHash<QString,Verification> verifying;
    QHash<QString,std::function<void()>> waiting;
    QHash<QString,QByteArray> logs;
    QHash<QString,QString> logRepositories;
    QJsonArray restoredLogs,restoredFailures;
    QString diagnosticError;
    QHash<QString,QString> failures;
    QHash<QString,PluginDiagnostics::Failure> failureCategories;
    QSet<QString> updating;
    QSet<QString> restoring;
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    QHash<QString,QPointer<PluginWebviews::WebviewService>> preservedPanels;
#endif
    QSet<QString> removing;
    QHash<QString,QVector<qint64>> crashTimes;
    QHash<QString,quint64> restartTickets;
    quint64 nextRestartTicket=0;
    bool quitting=false;
};
}
#endif
