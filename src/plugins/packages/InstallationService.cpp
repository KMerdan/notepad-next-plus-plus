// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstallationService.h"
#include "PackageLease.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QDir>
namespace PluginPackages {
InstallationService::InstallationService(InstalledPluginRegistry *installedRegistry,PackagePolicy selectedPolicy,QObject *parent,QNetworkAccessManager *network)
    :QObject(parent),registry(installedRegistry),github(new GitHubReleaseService(this,network)),policy(std::move(selectedPolicy)) {
    connect(github,&GitHubReleaseService::releasesFound,this,[this](const QVector<ReleaseAsset> &assets){busy=false;emit busyChanged(false);emit releasesAvailable(releaseResource.id,assets);});
    connect(github,&GitHubReleaseService::failed,this,&InstallationService::finishNetwork);
    connect(github,&GitHubReleaseService::cancelled,this,[this]{finishNetwork(tr("Installation cancelled."));});
    connect(github,&GitHubReleaseService::progress,this,&InstallationService::downloadProgress);
    connect(github,&GitHubReleaseService::downloaded,this,[this](const QString &path,const ReleaseAsset &asset,const QString &digest){beginPreparation(path,releaseResource,asset.provenance(digest),true,releaseUpdate,baseDigest);});
}
InstallationService::~InstallationService() {
    cancel();
    // Production exit must keep this service alive until busyChanged(false).
    // No thread closure captures this object; abnormal owner destruction drops
    // the result and leaves only a safe unreferenced immutable package.
}
bool InstallationService::installDirectory(const QString &directory,const PluginSources::RegisteredResource &resource) {
    if(busy||!registry->loadError().isEmpty()||!registry->plugin(resource.id).id().isEmpty()) return false;
    busy=true;emit busyChanged(true);beginPreparation(directory,resource,{{"kind","local"}},false);return true;
}
bool InstallationService::updateDirectory(const QString &directory,const PluginSources::RegisteredResource &resource) {
    const auto current=registry->plugin(resource.id);
    if(busy||!registry->loadError().isEmpty()||current.id().isEmpty()||current.updatePending||current.package.repository!=resource.repository)return false;
    busy=true;emit busyChanged(true);beginPreparation(directory,resource,{{"kind","local"}},false,true,current.package.digest);return true;
}
bool InstallationService::lookupReleases(const PluginSources::RegisteredResource &resource,bool update) {
    const auto current=registry->plugin(resource.id);
    if(busy||!registry->loadError().isEmpty()||(!current.id().isEmpty())!=update||current.updatePending
        ||(update&&current.package.repository!=resource.repository))return false;
    busy=true;releaseResource=resource;releaseUpdate=update;baseDigest=current.package.digest;emit busyChanged(true);github->discover(resource);return true;
}
bool InstallationService::installRelease(const ReleaseAsset &asset) {
    const auto current=registry->plugin(releaseResource.id);
    if(busy||!registry->loadError().isEmpty()||releaseResource.id.isEmpty()||asset.repository!=releaseResource.repository
        ||(!current.id().isEmpty())!=releaseUpdate||current.updatePending||current.package.digest!=baseDigest)return false;
    QString error;downloadLease=PackageLease::acquire(registry->packageStorePath(),{},&error);
    if(!downloadLease){emit finished(releaseResource.id,error);return false;}
    downloadDirectory=std::make_shared<QTemporaryDir>(registry->packageStorePath()+"/.download-XXXXXX");
    if(!downloadDirectory->isValid()){downloadDirectory.reset();downloadLease.reset();return false;}
    busy=true;emit busyChanged(true);
    if(!github->download(asset,downloadDirectory->path()+"/plugin.zip")){if(busy)finishNetwork(tr("The release selection is no longer valid."));return false;}
    return true;
}
void InstallationService::beginPreparation(const QString &directory,const PluginSources::RegisteredResource &resource,const QJsonObject &origin,bool zip,bool update,const QString &expectedDigest) {
    cancellation=std::make_shared<std::atomic_bool>(false);
    const auto cancelToken=cancellation;const auto store=registry->packageStorePath();const auto selectedPolicy=policy;
    const auto downloadOwner=downloadDirectory;
    const auto lifetime=downloadLease;
    const QPointer<InstallationService> target=this;
    struct Outcome { PreparedPackage package;QString error;bool ok=false; };
    auto outcome=std::make_shared<Outcome>();
    thread=QThread::create([directory,resource,store,selectedPolicy,cancelToken,target,outcome,downloadOwner,lifetime,zip] {
        QElapsedTimer timer;timer.start();
        const auto progress=[&](qint64 bytes){
            if(timer.elapsed()<100)return;timer.restart();
            if(target)QMetaObject::invokeMethod(target,[target,bytes]{if(target)emit target->progress(bytes);},Qt::QueuedConnection);
        };
        PackageStore packages(store);
        // Maintenance may briefly own the publication lock after uninstall.
        // Only this worker waits; GUI callers retain nonblocking defaults.
        outcome->ok=zip?packages.prepareZip(directory,resource,selectedPolicy,&outcome->package,&outcome->error,cancelToken.get(),progress,1000)
            :packages.prepareDirectory(directory,resource,selectedPolicy,&outcome->package,&outcome->error,cancelToken.get(),progress,1000);
    });
    connect(thread,&QThread::finished,this,[this,resource,outcome,cancelToken,origin,update,expectedDigest] {
        thread=nullptr;
        if(cancelToken->load()) {outcome->ok=false;outcome->error=tr("Installation cancelled.");}
        if(outcome->ok&&!update)outcome->ok=registry->add(outcome->package,resource.sourceIds,origin,&outcome->error);
        busy=false;cancellation.reset();downloadDirectory.reset();downloadLease.reset();emit busyChanged(false);
        if(outcome->ok&&update)emit updatePrepared(outcome->package,resource.sourceIds,origin,expectedDigest);
        else emit finished(resource.id,outcome->ok?QString():outcome->error);
    });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    thread->start();
}
void InstallationService::finishNetwork(const QString &error) {busy=false;downloadDirectory.reset();downloadLease.reset();emit busyChanged(false);emit finished(releaseResource.id,error);}
void InstallationService::cancel() {if(cancellation)cancellation->store(true);github->cancel();}
}
