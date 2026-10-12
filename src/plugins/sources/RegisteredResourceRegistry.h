// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REGISTEREDRESOURCEREGISTRY_H
#define REGISTEREDRESOURCEREGISTRY_H

#include "PluginMetadata.h"
#include <QObject>

namespace PluginSources {

// The application owns one registry shared by every window. Installation state
// will belong to a separate PluginRegistry when the installer is introduced.
class RegisteredResourceRegistry : public QObject
{
    Q_OBJECT
public:
    explicit RegisteredResourceRegistry(const QString &filePath, QObject *parent = nullptr);
    bool load(QString *error);
    bool registerSource(const SourceSnapshot &snapshot, QString *error);
    bool removeSource(const QString &key, QString *error);

    QVector<SourceSnapshot> sources() const { return snapshots; }
    QVector<RegisteredResource> resources() const;
    QString loadError() const { return readError; }

signals:
    void changed();

private:
    bool commit(const QVector<SourceSnapshot> &candidate, QString *error);
    static bool validateSources(const QVector<SourceSnapshot> &candidate, QString *error);
    QString filePath;
    QString readError;
    QVector<SourceSnapshot> snapshots;
};

}
#endif
