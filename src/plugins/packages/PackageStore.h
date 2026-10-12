// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINPACKAGESTORE_H
#define PLUGINPACKAGESTORE_H
#include "PluginMetadata.h"
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>
namespace PluginPackages {
class PackageLease;
struct PackagePolicy {
    QString applicationVersion="0.15.0",apiVersion="0.1.0";
    QStringList capabilities;
    // Older profiles remain installable; each session negotiates one version.
    QStringList compatibleApiVersions;
};
struct PreparedPackage {
    QString root,digest,repository;
    QJsonObject metadata;
    qint64 bytes=0;
    int files=0;
    // Transient ownership, never serialized in the Installed index.
    std::shared_ptr<PackageLease> lease;
};
struct CollectionResult {
    int packages=0,temporaryDirectories=0,staleLeases=0;
    bool deferred=false,cancelled=false,more=false;
    QString error;
    QStringList preserved;
};
// Blocking file operations: run on an installer worker, never the GUI thread.
// No scripts, dependency resolution, npm invocation or plugin execution.
class PackageStore {
public:
    static constexpr qint64 MaxPackageBytes=256*1024*1024;
    static constexpr qint64 MaxFileBytes=32*1024*1024;
    static constexpr int MaxFiles=8192;
    static bool isSafeRelativePath(const QString &path);
    explicit PackageStore(QString path):path(std::move(path)) {}
    bool prepareDirectory(const QString &directory,const PluginSources::RegisteredResource &expected,
                          const PackagePolicy &policy,PreparedPackage *result,QString *error,
                          const std::atomic_bool *cancel=nullptr,const std::function<void(qint64)> &progress={},int lockWaitMs=0) const;
    bool verify(const PreparedPackage &package,QString *error,const std::atomic_bool *cancel=nullptr) const;
    bool prepareZip(const QString &archive,const PluginSources::RegisteredResource &expected,
                    const PackagePolicy &policy,PreparedPackage *result,QString *error,
                    const std::atomic_bool *cancel=nullptr,const std::function<void(qint64)> &progress={},int lockWaitMs=0) const;
    QString rootPath() const { return path; }
    CollectionResult collect(const QString &installedIndex,const std::atomic_bool *cancel=nullptr) const;
private:
    QString path;
};
}
Q_DECLARE_METATYPE(PluginPackages::PreparedPackage)
#endif
