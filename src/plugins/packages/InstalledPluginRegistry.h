// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INSTALLEDPLUGINREGISTRY_H
#define INSTALLEDPLUGINREGISTRY_H
#include "PackageStore.h"
#include <QObject>
#include <optional>
#include <QSet>
namespace PluginPackages {
struct InstalledRevision {
    PreparedPackage package;
    QStringList sourceIds;
    QJsonObject origin;
    QDateTime installedAt;
};
struct InstalledPlugin {
    PreparedPackage package;
    QStringList sourceIds;
    QJsonObject origin;
    QDateTime installedAt;
    bool enabled=false;
    std::optional<InstalledRevision> previous;
    std::optional<InstalledRevision> pendingPrevious;
    bool updatePending=false;
    bool validationDeferred=false;
    QString id() const { return package.metadata["id"].toString(); }
};
// GUI-owned small persistent index. Package I/O and integrity hashing happen
// on a worker before committing a PreparedPackage here. Uninstall removes the
// index reference; lifetime leases protect sessions and prepared packages from
// cross-process collection until their last owner has stopped using them.
class InstalledPluginRegistry : public QObject {
    Q_OBJECT
public:
    InstalledPluginRegistry(QString filePath,QString storePath,QObject *parent=nullptr);
    bool load(QString *error);
    bool add(const PreparedPackage &prepared,const QStringList &sources,const QJsonObject &origin,QString *error);
    bool setEnabled(const QString &id,bool enabled,QString *error);
    bool remove(const QString &id,QString *error);
    bool beginUpdate(const PreparedPackage &prepared,const QStringList &sources,const QJsonObject &origin,const QString &expectedDigest,QString *error,bool deferValidation=false);
    bool beginRollback(const QString &id,const QString &expectedDigest,QString *error,bool deferValidation=false);
    bool startValidation(const QString &id,QString *error);
    bool confirmUpdate(const QString &id,QString *error);
    bool abortUpdate(const QString &id,QString *error);
    QVector<InstalledPlugin> plugins() const { return records; }
    InstalledPlugin plugin(const QString &id) const;
    QString loadError() const { return readError; }
    QString packageStorePath() const { return storePath; }
    QString indexPath() const { return filePath; }
    // Reads one atomic durable snapshot without recovery/mutation. Collection
    // reads this after inspecting lifetime leases under the store lock.
    static bool readPackageReferences(const QString &indexPath,const QString &store,QSet<QString> *digests,QString *error);
signals:
    void changed();
private:
    bool commit(const QVector<InstalledPlugin> &candidate,QString *error);
    bool preparedInStore(const PreparedPackage &prepared,QString *error) const;
    QString filePath,storePath,readError;
    QByteArray diskDigest;
    QVector<InstalledPlugin> records;
};
}
#endif
