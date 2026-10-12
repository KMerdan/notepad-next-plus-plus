// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGININSTALLATIONSERVICE_H
#define PLUGININSTALLATIONSERVICE_H
#include "InstalledPluginRegistry.h"
#include "GitHubReleaseService.h"
#include <QPointer>
#include <QThread>
#include <memory>
class QTemporaryDir;
namespace PluginPackages {
class InstallationService:public QObject {
    Q_OBJECT
public:
    InstallationService(InstalledPluginRegistry *registry,PackagePolicy policy,QObject *parent=nullptr,QNetworkAccessManager *network=nullptr);
    ~InstallationService() override;
    bool installDirectory(const QString &directory,const PluginSources::RegisteredResource &resource);
    bool updateDirectory(const QString &directory,const PluginSources::RegisteredResource &resource);
    bool lookupReleases(const PluginSources::RegisteredResource &resource,bool update=false);
    bool installRelease(const ReleaseAsset &asset);
    void cancel();
    bool isBusy() const { return busy; }
signals:
    void busyChanged(bool busy);
    void progress(qint64 copiedBytes);
    void finished(const QString &pluginId,const QString &error);
    void releasesAvailable(const QString &pluginId,const QVector<PluginPackages::ReleaseAsset> &assets);
    void downloadProgress(qint64 received,qint64 total);
    void updatePrepared(const PluginPackages::PreparedPackage &package,const QStringList &sources,const QJsonObject &origin,const QString &expectedDigest);
private:
    InstalledPluginRegistry *registry;
    GitHubReleaseService *github;
    PluginSources::RegisteredResource releaseResource;
    std::shared_ptr<QTemporaryDir> downloadDirectory;
    std::shared_ptr<PackageLease> downloadLease;
    PackagePolicy policy;
    bool busy=false;
    bool releaseUpdate=false;
    QString baseDigest;
    QPointer<QThread> thread;
    std::shared_ptr<std::atomic_bool> cancellation;
    void beginPreparation(const QString &path,const PluginSources::RegisteredResource &resource,const QJsonObject &origin,bool zip,bool update=false,const QString &expectedDigest={});
    void finishNetwork(const QString &error);
};
}
#endif
