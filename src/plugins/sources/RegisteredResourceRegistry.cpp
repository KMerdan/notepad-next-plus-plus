// SPDX-License-Identifier: GPL-3.0-or-later
#include "RegisteredResourceRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace PluginSources {
namespace {
constexpr qsizetype MaxRegistryBytes = 8 * 1024 * 1024;
constexpr qsizetype MaxSources = 64;

bool reject(QString *error, const QString &text)
{
    if (error) *error = text;
    return false;
}

bool validProvenance(const SourceSnapshot &source)
{
    static const QRegularExpression commit(QStringLiteral("^[a-f0-9]{40}$"));
    static const QRegularExpression digest(QStringLiteral("^[a-f0-9]{64}$"));
    const bool local = source.isLocal() && source.commit.isEmpty() && source.ref.isEmpty()
        && QDir::isAbsolutePath(source.localPath) && !source.localPath.contains(QChar::Null)
        && QDir::cleanPath(source.localPath) == source.localPath
        && (!source.repository.startsWith(QStringLiteral("file:"))
            || source.repository == QUrl::fromLocalFile(source.localPath).toString(QUrl::FullyEncoded));
    return (local || (!source.isLocal() && !source.repository.startsWith(QStringLiteral("file:")) && commit.match(source.commit).hasMatch())) && digest.match(source.digest).hasMatch()
        && source.checkedAt.isValid();
}
}

RegisteredResourceRegistry::RegisteredResourceRegistry(const QString &filePath, QObject *parent)
    : QObject(parent), filePath(filePath)
{
}

bool RegisteredResourceRegistry::validateSources(const QVector<SourceSnapshot> &candidate, QString *error)
{
    if (candidate.size() > MaxSources) return reject(error, tr("At most 64 sources can be registered."));
    QMap<QString, QString> repositories;
    QSet<QString> keys;
    for (const auto &source : candidate) {
        if (keys.contains(source.key())) return reject(error, tr("Duplicate registered source."));
        keys.insert(source.key());
        if (!validProvenance(source)) return reject(error, tr("Invalid source provenance."));
        for (const auto &resource : source.resources) {
            if (repositories.contains(resource.id) && !PluginMetadata::sameOrigin(repositories.value(resource.id), resource.repository))
                return reject(error, tr("Plugin ID %1 is already registered from a different release repository. Remove the conflicting source before registering this one.").arg(resource.id));
            repositories.insert(resource.id, resource.repository);
        }
    }
    return true;
}

bool RegisteredResourceRegistry::load(QString *error)
{
    QFile file(filePath);
    if (!file.exists()) { readError.clear(); snapshots.clear(); return true; }
    const auto fail = [&](const QString &text) { readError = text; return reject(error, text); };
    if (!file.open(QIODevice::ReadOnly)) return fail(tr("Cannot read registered sources: %1").arg(file.errorString()));
    if (file.size() > MaxRegistryBytes) return fail(tr("Registered source storage exceeds the 8 MiB limit."));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.read(MaxRegistryBytes + 1), &parseError);
    const auto root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || root.value(QStringLiteral("schemaVersion")) != QJsonValue(1)
        || !root.value(QStringLiteral("sources")).isArray()) return fail(tr("Registered source storage has an invalid format. The existing file has been preserved."));
    const auto records = root.value(QStringLiteral("sources")).toArray();
    if (records.size() > MaxSources) return fail(tr("Registered source storage contains too many sources."));
    QVector<SourceSnapshot> candidate;
    for (const auto &record : records) {
        const auto item = record.toObject();
        if (!record.isObject() || !item.value(QStringLiteral("ref")).isString() || !item.value(QStringLiteral("metadata")).isObject()) return fail(tr("Invalid registered source record."));
        SourceSnapshot source;
        QString validationError;
        if (!PluginMetadata::parse(QJsonDocument(item.value(QStringLiteral("metadata")).toObject()).toJson(QJsonDocument::Compact),
                                   item.value(QStringLiteral("repository")).toString(), item.value(QStringLiteral("ref")).toString(), &source, &validationError)) return fail(validationError);
        source.commit = item.value(QStringLiteral("commit")).toString();
        source.digest = item.value(QStringLiteral("digest")).toString();
        source.checkedAt = QDateTime::fromString(item.value(QStringLiteral("checkedAt")).toString(), Qt::ISODateWithMs);
        if (item.contains(QStringLiteral("localPath")) && !item.value(QStringLiteral("localPath")).isString()) return fail(tr("Invalid local source path."));
        source.localPath = item.value(QStringLiteral("localPath")).toString();
        for (auto &resource : source.resources) resource.sourceIds = {source.key()};
        candidate.append(source);
    }
    QString validationError;
    if (!validateSources(candidate, &validationError)) return fail(validationError);
    snapshots = candidate;
    readError.clear();
    emit changed();
    return true;
}

bool RegisteredResourceRegistry::commit(const QVector<SourceSnapshot> &candidate, QString *error)
{
    if (!readError.isEmpty()) return reject(error, tr("Source storage could not be loaded. Resolve the storage error before making changes: %1").arg(readError));
    if (!validateSources(candidate, error)) return false;
    QJsonArray records;
    for (const auto &source : candidate) {
        records.append(QJsonObject{{QStringLiteral("repository"), source.repository}, {QStringLiteral("ref"), source.ref},
            {QStringLiteral("commit"), source.commit}, {QStringLiteral("digest"), source.digest},
            {QStringLiteral("checkedAt"), source.checkedAt.toUTC().toString(Qt::ISODateWithMs)}, {QStringLiteral("metadata"), source.metadata},
            {QStringLiteral("localPath"), source.localPath}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("sources"), records}}).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaxRegistryBytes) return reject(error, tr("Registered source storage exceeds the 8 MiB limit."));
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) return reject(error, tr("Cannot create registered source storage."));
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return reject(error, tr("Cannot save registered sources: %1").arg(file.errorString()));
    snapshots = candidate;
    emit changed();
    return true;
}

bool RegisteredResourceRegistry::registerSource(const SourceSnapshot &snapshot, QString *error)
{
    SourceSnapshot checked;
    if (!PluginMetadata::parse(QJsonDocument(snapshot.metadata).toJson(QJsonDocument::Compact), snapshot.repository, snapshot.ref, &checked, error)) return false;
    checked.commit = snapshot.commit;
    checked.digest = snapshot.digest;
    checked.checkedAt = snapshot.checkedAt;
    checked.localPath = snapshot.localPath;
    for (auto &resource : checked.resources) resource.sourceIds = {checked.key()};
    auto candidate = snapshots;
    const auto it = std::find_if(candidate.begin(), candidate.end(), [&](const SourceSnapshot &s) { return s.key() == checked.key(); });
    if (it == candidate.end()) candidate.append(checked); else *it = checked;
    return commit(candidate, error);
}

bool RegisteredResourceRegistry::removeSource(const QString &key, QString *error)
{
    auto candidate = snapshots;
    const auto it = std::find_if(candidate.begin(), candidate.end(), [&](const SourceSnapshot &s) { return s.key() == key; });
    if (it == candidate.end()) return reject(error, tr("The source is no longer registered."));
    candidate.erase(it);
    return commit(candidate, error);
}

QVector<RegisteredResource> RegisteredResourceRegistry::resources() const
{
    QMap<QString, RegisteredResource> merged;
    auto sorted = snapshots;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.checkedAt < b.checkedAt; });
    for (const auto &source : sorted) {
        for (auto resource : source.resources) {
            const auto existing = merged.constFind(resource.id);
            if (existing != merged.cend()) resource.sourceIds = existing->sourceIds + resource.sourceIds;
            merged.insert(resource.id, resource);
        }
    }
    return merged.values().toVector();
}

}
