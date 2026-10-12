// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINPACKAGECOLLECTIONSERVICE_H
#define PLUGINPACKAGECOLLECTIONSERVICE_H
#include "PackageStore.h"
#include <QObject>
#include <QTimer>
namespace PluginPackages {
class InstalledPluginRegistry;
// Debounced, bounded worker collection. Does not touch the GUI registry from
// a worker; the current durable index is authoritative under filesystem locks.
class PackageCollectionService:public QObject {
    Q_OBJECT
public:
    explicit PackageCollectionService(InstalledPluginRegistry *registry,QObject *parent=nullptr);
    ~PackageCollectionService() override;
    void request();
    void shutdown();
    bool isIdle() const {return !running;}
signals:
    void finished(const PluginPackages::CollectionResult &result);
    void idle();
private:
    void start();
    QString store,index;
    QTimer timer;
    std::shared_ptr<std::atomic_bool> cancel;
    bool running=false,again=false,stopping=false;
};
}
Q_DECLARE_METATYPE(PluginPackages::CollectionResult)
#endif
