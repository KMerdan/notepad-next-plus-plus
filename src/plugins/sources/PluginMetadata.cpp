// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginMetadata.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <cmath>

static void initializePluginSourceResources()
{
    Q_INIT_RESOURCE(plugin_sources);
}

namespace PluginSources {
namespace {

QString message(const char *text)
{
    return QCoreApplication::translate("PluginMetadata", text);
}

bool reject(QString *error, const QString &text)
{
    if (error) *error = text;
    return false;
}

// This evaluator is deliberately limited to the keywords in the bundled
// schema. It is not exposed as a general-purpose JSON Schema implementation.
bool validate(const QJsonValue &value, const QJsonObject &schema, const QJsonObject &root,
              const QString &path, QString *error, int depth = 0)
{
    if (depth > 64) return reject(error, message("Metadata nesting is too deep."));
    const auto fail = [&](const QString &reason) { return reject(error, path + QStringLiteral(": ") + reason); };
    if (schema.contains(QStringLiteral("$ref"))) {
        const QString ref = schema.value(QStringLiteral("$ref")).toString();
        QJsonObject target;
        if (ref == QStringLiteral("#")) target = root;
        else if (ref.startsWith(QStringLiteral("#/$defs/")))
            target = root.value(QStringLiteral("$defs")).toObject().value(ref.mid(8)).toObject();
        if (target.isEmpty()) return fail(message("Unknown schema reference."));
        if (!validate(value, target, root, path, error, depth + 1)) return false;
    }
    if (schema.contains(QStringLiteral("enum")) && !schema.value(QStringLiteral("enum")).toArray().contains(value)) return fail(message("Value is not in the allowed set."));
    if (schema.contains(QStringLiteral("const")) && value != schema.value(QStringLiteral("const")))
        return fail(message("Unexpected value."));
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty()) {
        const bool matches = (type == QStringLiteral("object") && value.isObject())
            || (type == QStringLiteral("array") && value.isArray())
            || (type == QStringLiteral("string") && value.isString())
            || (type == QStringLiteral("integer") && value.isDouble() && std::floor(value.toDouble()) == value.toDouble());
        if (!matches) return fail(message("Expected %1.").arg(type));
    }
    for (const auto &item : schema.value(QStringLiteral("allOf")).toArray())
        if (!validate(value, item.toObject(), root, path, error, depth + 1)) return false;
    for (const QString &keyword : {QStringLiteral("oneOf"), QStringLiteral("anyOf")}) {
        if (!schema.contains(keyword)) continue;
        int matches = 0;
        for (const auto &item : schema.value(keyword).toArray())
            if (validate(value, item.toObject(), root, path, nullptr, depth + 1)) ++matches;
        if ((keyword == QStringLiteral("oneOf") && matches != 1) || matches == 0)
            return fail(message("Value does not match an allowed metadata format."));
    }
    if (schema.contains(QStringLiteral("not"))
        && validate(value, schema.value(QStringLiteral("not")).toObject(), root, path, nullptr, depth + 1))
        return fail(message("Value is not allowed here."));
    if (schema.contains(QStringLiteral("if"))) {
        const bool condition = validate(value, schema.value(QStringLiteral("if")).toObject(), root, path, nullptr, depth + 1);
        const QString branch = condition ? QStringLiteral("then") : QStringLiteral("else");
        if (schema.contains(branch) && !validate(value, schema.value(branch).toObject(), root, path, error, depth + 1)) return false;
    }
    if (value.isObject()) {
        const auto object = value.toObject();
        const auto properties = schema.value(QStringLiteral("properties")).toObject();
        for (const auto &field : schema.value(QStringLiteral("required")).toArray())
            if (!object.contains(field.toString())) return fail(message("Missing field %1.").arg(field.toString()));
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (properties.contains(it.key())) {
                if (!validate(it.value(), properties.value(it.key()).toObject(), root, path + QLatin1Char('.') + it.key(), error, depth + 1)) return false;
            } else if (schema.value(QStringLiteral("additionalProperties")).isBool()
                       && !schema.value(QStringLiteral("additionalProperties")).toBool())
                return fail(message("Unknown field %1.").arg(it.key()));
        }
    }
    if (value.isString()) {
        const QString text = value.toString();
        const qsizetype length = text.toUcs4().size();
        if (schema.contains(QStringLiteral("minLength")) && length < schema.value(QStringLiteral("minLength")).toInt()) return fail(message("Text is too short."));
        if (schema.contains(QStringLiteral("maxLength")) && length > schema.value(QStringLiteral("maxLength")).toInt()) return fail(message("Text is too long."));
        if (schema.contains(QStringLiteral("pattern")) && !QRegularExpression(schema.value(QStringLiteral("pattern")).toString()).match(text).hasMatch()) return fail(message("Text has an invalid format."));
    }
    if (value.isDouble()) {
        if (schema.contains(QStringLiteral("minimum")) && value.toDouble() < schema.value(QStringLiteral("minimum")).toDouble()) return fail(message("Number is too small."));
        if (schema.contains(QStringLiteral("maximum")) && value.toDouble() > schema.value(QStringLiteral("maximum")).toDouble()) return fail(message("Number is too large."));
    }
    if (value.isArray()) {
        const auto array = value.toArray();
        if (schema.contains(QStringLiteral("minItems")) && array.size() < schema.value(QStringLiteral("minItems")).toInt()) return fail(message("Too few entries."));
        if (schema.contains(QStringLiteral("maxItems")) && array.size() > schema.value(QStringLiteral("maxItems")).toInt()) return fail(message("Too many entries."));
        for (qsizetype i = 0; i < array.size(); ++i) {
            if (schema.value(QStringLiteral("uniqueItems")).toBool())
                for (qsizetype j = 0; j < i; ++j) if (array[i] == array[j]) return fail(message("Duplicate entry."));
            if (schema.contains(QStringLiteral("items")) && !validate(array[i], schema.value(QStringLiteral("items")).toObject(), root, path + QStringLiteral("[%1]").arg(i), error, depth + 1)) return false;
        }
    }
    return true;
}

bool validateSemantics(const QJsonObject &metadata, QString *error)
{
    // SemVer 2.0: no leading zeros in numeric identifiers, including prerelease.
    static const QRegularExpression version(QStringLiteral(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
    if (!version.match(metadata.value(QStringLiteral("version")).toString()).hasMatch())
        return reject(error, message("%1: version must be SemVer.").arg(metadata.value(QStringLiteral("id")).toString()));
    if (metadata.value(QStringLiteral("name")).toString().trimmed().isEmpty()) return reject(error, message("Name cannot be blank."));
    if (metadata.value(QStringLiteral("list")).isArray()) return true;
    const auto capabilities = metadata.value(QStringLiteral("capabilities")).toObject();
    for (const auto &required : capabilities.value(QStringLiteral("required")).toArray())
        if (capabilities.value(QStringLiteral("optional")).toArray().contains(required)) return reject(error, message("Required and optional capabilities must be disjoint."));
    const QString id = metadata.value(QStringLiteral("id")).toString();
    const auto contributes = metadata.value(QStringLiteral("contributes")).toObject();
    QSet<QString> commands;
    for (const auto &entry : contributes.value(QStringLiteral("commands")).toArray()) {
        const QString command = entry.toObject().value(QStringLiteral("id")).toString();
        if (!command.startsWith(id + QLatin1Char('.')) || commands.contains(command)) return reject(error, message("Command IDs must be unique and belong to their plugin."));
        commands.insert(command);
    }
    for (const QString &location : {QStringLiteral("menus"), QStringLiteral("toolbar")}) {
        QSet<QString> seen;
        for (const auto &entry : contributes.value(location).toArray()) {
            const QString command = entry.toObject().value(QStringLiteral("command")).toString();
            if (!commands.contains(command) || seen.contains(command)) return reject(error, message("Menu and toolbar entries must reference unique declared commands."));
            seen.insert(command);
        }
    }
    if(metadata.contains(QStringLiteral("compatibility"))) {
        if(metadata.value("runtime").toObject().value("kind")!="node")return reject(error,message("VS Code compatibility requires a Node package."));
        QSet<QString> aliases,targets;
        for(const auto &value:metadata.value("compatibility").toObject().value("commands").toArray()) {
            const auto alias=value.toObject().value("id").toString(),target=value.toObject().value("target").toString();
            if(aliases.contains(alias)||targets.contains(target)||!commands.contains(target))return reject(error,message("VS Code aliases must uniquely reference declared commands."));
            aliases.insert(alias);targets.insert(target);
        }
        if(targets!=commands)return reject(error,message("Every command needs a VS Code alias."));
        for(const QString &capability:{QStringLiteral("commands@1"),QStringLiteral("documents.read@1"),QStringLiteral("documents.delta@1")})
            if(!capabilities.value("required").toArray().contains(capability))return reject(error,message("VS Code compatibility requires capability: ")+capability);
    }
    for (const auto &entry : metadata.value(QStringLiteral("activationEvents")).toArray()) {
        const QString event = entry.toString();
        if (event.startsWith(QStringLiteral("onCommand:")) && !commands.contains(event.mid(10))) return reject(error, message("Activation refers to an undeclared command."));
    }
    return true;
}

}

bool PluginMetadata::normalizeRepository(const QString &input, QString *repository, QString *error)
{
    const QUrl url(input.trimmed(), QUrl::StrictMode);
    static const QRegularExpression path(QStringLiteral("^/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/?$"));
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("github.com")
        || !url.userInfo().isEmpty() || url.port() != -1 || url.hasQuery() || url.hasFragment()
        || !path.match(url.path(QUrl::FullyEncoded)).hasMatch() || input.size() > 512)
        return reject(error, message("Enter an HTTPS GitHub repository URL, such as https://github.com/owner/repository."));
    QString name = url.path();
    if (name.endsWith(QLatin1Char('/'))) name.chop(1);
    if (name.endsWith(QStringLiteral(".git"))) name.chop(4);
    const auto parts = name.mid(1).split(QLatin1Char('/'));
    for (const QString &part : parts)
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) return reject(error, message("Invalid repository name."));
    if (repository) *repository = QStringLiteral("https://github.com") + name;
    return true;
}

QString SourceSnapshot::key() const
{
    if (isLocal()) return QUrl::fromLocalFile(localPath).toString(QUrl::FullyEncoded);
    return repository.toLower() + QLatin1Char('#') + ref;
}

bool PluginMetadata::normalizeOrigin(const QString &input, QString *origin, QString *error)
{
    if (!input.startsWith(QStringLiteral("file:"))) return normalizeRepository(input, origin, error);
    const QUrl url(input, QUrl::StrictMode);
    const QString path = url.toLocalFile();
    if (!url.isValid() || !url.host().isEmpty() || !url.userInfo().isEmpty() || url.port() != -1
        || url.hasQuery() || url.hasFragment() || path.contains(QChar::Null)
        || !QDir::isAbsolutePath(path) || input.size() > 8192)
        return reject(error, message("Invalid local metadata origin."));
    if (origin) *origin = QUrl::fromLocalFile(QDir::cleanPath(path)).toString(QUrl::FullyEncoded);
    return true;
}

bool PluginMetadata::sameOrigin(const QString &left, const QString &right)
{
    return left.startsWith(QStringLiteral("file:")) || right.startsWith(QStringLiteral("file:"))
        ? left == right : left.compare(right, Qt::CaseInsensitive) == 0;
}

bool PluginMetadata::readLocalFile(const QString &path, SourceSnapshot *snapshot, QString *error)
{
    if (path.contains(QChar::Null)) return reject(error, message("Invalid local metadata path."));
    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    if (!info.isFile() || canonicalPath.isEmpty()) return reject(error, message("Select an existing plugin metadata JSON file."));
    QFile file(canonicalPath);
    if (!file.open(QIODevice::ReadOnly)) return reject(error, file.errorString());
    const QByteArray bytes = file.read(MaxMetadataBytes + 1);
    if (bytes.size() > MaxMetadataBytes) return reject(error, message("Metadata exceeds the 1 MiB limit."));
    const QString localOrigin = QUrl::fromLocalFile(canonicalPath).toString(QUrl::FullyEncoded);
    if (localOrigin.size() > 1024) return reject(error, message("Local metadata path exceeds the source association limit."));
    const auto object = QJsonDocument::fromJson(bytes).object();
    // A declared GitHub repository keeps the same plugin identity for local
    // development and later releases. Unpublished examples use their file URI.
    const QString origin = object.contains(QStringLiteral("repository"))
        ? object.value(QStringLiteral("repository")).toString() : localOrigin;
    QString normalized;
    if (!normalizeOrigin(origin, &normalized, error)) return false;
    if (normalized.startsWith(QStringLiteral("file:")) && normalized != localOrigin)
        return reject(error, message("Local metadata origin does not match the selected file."));
    SourceSnapshot result;
    if (!parse(bytes, normalized, {}, &result, error)) return false;
    result.localPath = canonicalPath;
    for (auto &resource : result.resources) resource.sourceIds = {result.key()};
    if (snapshot) *snapshot = result;
    return true;
}

bool PluginMetadata::parse(const QByteArray &bytes, const QString &repository, const QString &ref,
                           SourceSnapshot *snapshot, QString *error)
{
    if (bytes.size() > MaxMetadataBytes) return reject(error, message("Metadata exceeds the 1 MiB limit."));
    QString canonical;
    if (!normalizeOrigin(repository, &canonical, error)) return false;
    if (ref.size() > 192 || ref.contains(QChar::Null)) return reject(error, message("Invalid Git ref."));
    // Bound nesting before asking Qt to allocate a JSON tree. Braces in strings
    // and escaped quotes are excluded from the nesting count.
    int nesting = 0;
    bool quoted = false, escaped = false;
    for (char c : bytes) {
        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++nesting > 32) return reject(error, message("Metadata nesting is too deep.")); }
        else if (c == '}' || c == ']') --nesting;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return reject(error, message("Metadata must be a valid JSON object."));
    static const QJsonObject schema = [] {
        initializePluginSourceResources();
        QFile file(QStringLiteral(":/plugins/manifest.schema.json"));
        if (!file.open(QIODevice::ReadOnly)) return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    if (schema.isEmpty()) return reject(error, message("The metadata schema is unavailable."));
    QJsonObject metadata = document.object();
    if (!validate(metadata, schema, schema, QStringLiteral("metadata"), error) || !validateSemantics(metadata, error)) return false;
    QString declared;
    if (metadata.contains(QStringLiteral("repository"))) {
        if (!normalizeOrigin(metadata.value(QStringLiteral("repository")).toString(), &declared, error)) return false;
        if (!sameOrigin(declared, canonical)) return reject(error, message("Metadata repository does not match the registered repository."));
    }
    metadata.insert(QStringLiteral("repository"), canonical);
    SourceSnapshot result;
    result.repository = canonical;
    result.ref = ref;
    result.metadata = metadata;
    result.checkedAt = QDateTime::currentDateTimeUtc();
    result.digest = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QJsonArray entries = metadata.value(QStringLiteral("list")).isArray()
        ? metadata.value(QStringLiteral("list")).toArray() : QJsonArray{metadata};
    QSet<QString> ids;
    for (const auto &entry : entries) {
        const auto plugin = entry.toObject();
        if (!validateSemantics(plugin, error)) return false;
        const QString id = plugin.value(QStringLiteral("id")).toString();
        if (ids.contains(id)) return reject(error, message("Duplicate plugin ID: %1.").arg(id));
        ids.insert(id);
        QString releaseRepository;
        if (metadata.value(QStringLiteral("list")).isArray()) {
            if (!normalizeRepository(plugin.value(QStringLiteral("repository")).toString(), &releaseRepository, error)) return false;
        } else if (!normalizeOrigin(plugin.value(QStringLiteral("repository")).toString(), &releaseRepository, error)) return false;
        result.resources.append({id, releaseRepository, plugin, {result.key()}});
    }
    if (snapshot) *snapshot = result;
    return true;
}

}
