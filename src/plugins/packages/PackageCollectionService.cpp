// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageCollectionService.h"
#include "InstalledPluginRegistry.h"
#include <QThread>
namespace PluginPackages {
PackageCollectionService::PackageCollectionService(InstalledPluginRegistry *registry,QObject *parent)
    :QObject(parent),store(registry->packageStorePath()),index(registry->indexPath()) {
    timer.setSingleShot(true);connect(&timer,&QTimer::timeout,this,&PackageCollectionService::start);
    connect(registry,&InstalledPluginRegistry::changed,this,&PackageCollectionService::request);
    request();
}
PackageCollectionService::~PackageCollectionService(){shutdown();}
void PackageCollectionService::request() {
    if(stopping)return;
    if(running){again=true;return;}
    timer.start(250);
}
void PackageCollectionService::start() {
    if(stopping||running)return;
    running=true;cancel=std::make_shared<std::atomic_bool>(false);
    const auto token=cancel;const auto root=store,path=index;auto result=std::make_shared<CollectionResult>();
    auto *worker=QThread::create([root,path,token,result]{*result=PackageStore(root).collect(path,token.get());});
    connect(worker,&QThread::finished,this,[this,result]{
        running=false;cancel.reset();emit finished(*result);emit idle();
        if(stopping)return;
        if(again||result->more){again=false;request();}
        else if(result->deferred)timer.start(10000);
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
}
void PackageCollectionService::shutdown() {stopping=true;timer.stop();again=false;if(cancel)cancel->store(true);}
}
