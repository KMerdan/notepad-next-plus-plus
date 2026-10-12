// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINPACKAGELEASE_H
#define PLUGINPACKAGELEASE_H
#include <QString>
#include <memory>
#include <optional>
#include <atomic>
#include "ProcessIdentity.h"
class QLockFile;
namespace PluginPackages {
class PackageStore;
// Cooperative cross-process lifetime pin. Empty digest pins the entire store
// during downloads/extraction; a digest pins only that immutable package.
// The lock remains held until the last shared owner releases the lease.
class PackageLease {
public:
    static std::shared_ptr<PackageLease> acquire(const QString &store,const QString &digest,QString *error,int waitMs=0,const std::atomic_bool *cancel=nullptr);
    ~PackageLease();
    bool protects(const QString &store,const QString &digest) const;
    bool pinRuntimeProcess(qint64 pid,QString *error);
    PackageLease(const PackageLease &)=delete;
    PackageLease &operator=(const PackageLease &)=delete;
private:
    friend class PackageStore;
    static bool lockStore(QLockFile &lock,int waitMs,const std::atomic_bool *cancel);
    PackageLease(QString store,QString directory,QString digest,std::unique_ptr<QLockFile> lock);
    static std::shared_ptr<PackageLease> acquireLocked(const QString &canonicalStore,const QString &digest,QString *error);
    QString store,directory,digest;
    std::unique_ptr<QLockFile> lock;
    std::optional<ProcessIdentity> runtime;
};
}
#endif
