// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINMETADATA_H
#define PLUGINMETADATA_H

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QStringList>
#include <QVector>

namespace PluginSources {

struct RegisteredResource {
    QString id;
    QString repository;
    QJsonObject metadata;
    QStringList sourceIds;
};

struct SourceSnapshot {
    QString repository;
    QString ref;
    QString commit;
    QString digest;
    QString localPath;
    QDateTime checkedAt;
    QJsonObject metadata;
    QVector<RegisteredResource> resources;

    QString key() const;
    bool isLocal() const { return !localPath.isEmpty(); }
    bool isMarketplace() const { return metadata.value(QStringLiteral("list")).isArray(); }
};

// Validates the shipped metadata schema plus identity/ownership constraints.
// Parsing discovers resources only; it cannot install packages or execute code.
class PluginMetadata
{
public:
    static constexpr qsizetype MaxMetadataBytes = 1024 * 1024;
    static bool normalizeRepository(const QString &input, QString *repository, QString *error);
    static bool normalizeOrigin(const QString &input, QString *origin, QString *error);
    static bool sameOrigin(const QString &left, const QString &right);
    static bool readLocalFile(const QString &path, SourceSnapshot *snapshot, QString *error);
    static bool parse(const QByteArray &bytes, const QString &repository, const QString &ref,
                      SourceSnapshot *snapshot, QString *error);
};

}

Q_DECLARE_METATYPE(PluginSources::SourceSnapshot)

#endif
