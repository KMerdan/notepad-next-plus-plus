// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceEntryResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace PluginSources {
namespace {
constexpr qsizetype MaxResponseBytes = 2 * 1024 * 1024;
}

SourceEntryResolver::SourceEntryResolver(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), network(network ? network : new QNetworkAccessManager(this))
{
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this] { fail(tr("Source lookup timed out. Try again.")); });
}

void SourceEntryResolver::resolve(const QString &input, const QString &requestedRef)
{
    if (busy) return;
    QString error;
    if (!PluginMetadata::normalizeRepository(input, &repository, &error)) { emit failed(error); return; }
    ref = requestedRef.trimmed();
    if (ref.size() > 192 || ref.contains(QChar::Null)) { emit failed(tr("Invalid Git ref.")); return; }
    busy = true;
    commit.clear();
    deadline.start(30000);
    // Resolve the default branch/ref to one immutable commit, then read only
    // its metadata. No git clone, release downloads or package hooks occur.
    QUrl url(QStringLiteral("https://api.github.com/repos/") + repository.mid(19) + QStringLiteral("/commits"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("1"));
    if (!ref.isEmpty()) query.addQueryItem(QStringLiteral("sha"), ref);
    url.setQuery(query);
    request(url, Stage::Commit);
}

void SourceEntryResolver::request(const QUrl &url, Stage requestedStage)
{
    stage = requestedStage;
    buffer.clear();
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "NotepadNext-PluginManager");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    reply = network->get(request);
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [this] {
        if (!reply) return;
        buffer += reply->read(MaxResponseBytes + 1 - buffer.size());
        if (buffer.size() > MaxResponseBytes) fail(tr("GitHub response exceeds the size limit."));
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this] {
        if (reply && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes)
            fail(tr("GitHub response exceeds the size limit."));
    });
    connect(reply, &QNetworkReply::finished, this, &SourceEntryResolver::finishReply);
}

void SourceEntryResolver::finishReply()
{
    if (!reply) return;
    auto *finished = reply.data();
    buffer += finished->read(MaxResponseBytes + 1 - buffer.size());
    const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = finished->error();
    const QString networkMessage = finished->errorString();
    reply.clear();
    finished->deleteLater();
    if (buffer.size() > MaxResponseBytes) { fail(tr("GitHub response exceeds the size limit.")); return; }
    if (status >= 300 && status < 400) { fail(tr("The repository has moved. Register its canonical GitHub URL.")); return; }
    if (status == 404) { fail(stage == Stage::Metadata ? tr("No notepadnext-plugin.json file exists at this ref.") : tr("Repository or Git ref was not found. Only public repositories are supported.")); return; }
    if (status == 403 || status == 429) { fail(tr("GitHub denied the request or its rate limit was reached. Try again later.")); return; }
    if (networkError != QNetworkReply::NoError || status != 200) { fail(tr("Could not read GitHub metadata: %1").arg(networkMessage)); return; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(buffer, &parseError);
    if (parseError.error != QJsonParseError::NoError) { fail(tr("GitHub returned an invalid JSON response.")); return; }
    if (stage == Stage::Commit) {
        if (!document.isArray() || document.array().size() != 1) { fail(tr("No commit was found at this ref.")); return; }
        commit = document.array().first().toObject().value(QStringLiteral("sha")).toString();
        if (!QRegularExpression(QStringLiteral("^[a-f0-9]{40}$")).match(commit).hasMatch()) { fail(tr("GitHub returned an invalid commit.")); return; }
        QUrl url(QStringLiteral("https://api.github.com/repos/") + repository.mid(19) + QStringLiteral("/contents/notepadnext-plugin.json"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("ref"), commit);
        url.setQuery(query);
        request(url, Stage::Metadata);
        return;
    }
    const auto object = document.object();
    const auto size = object.value(QStringLiteral("size"));
    if (!document.isObject() || object.value(QStringLiteral("type")).toString() != QStringLiteral("file")
        || object.value(QStringLiteral("encoding")).toString() != QStringLiteral("base64")
        || !object.value(QStringLiteral("content")).isString() || !size.isDouble()
        || size.toDouble() != double(size.toInteger())
        || size.toDouble() < 0 || size.toDouble() > PluginMetadata::MaxMetadataBytes) {
        fail(tr("GitHub metadata is not a supported file within the 1 MiB limit.")); return;
    }
    QByteArray encoded = object.value(QStringLiteral("content")).toString().toLatin1();
    encoded.replace("\n", "");
    encoded.replace("\r", "");
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != size.toInteger()) { fail(tr("GitHub returned invalid metadata content.")); return; }
    SourceSnapshot snapshot;
    QString error;
    if (!PluginMetadata::parse(decoded.decoded, repository, ref, &snapshot, &error)) { fail(error); return; }
    snapshot.commit = commit;
    reset();
    emit resolved(snapshot);
}

void SourceEntryResolver::reset()
{
    deadline.stop();
    busy = false;
    buffer.clear();
    if (reply) {
        auto *pending = reply.data();
        reply.clear();
        pending->disconnect(this);
        pending->abort();
        pending->deleteLater();
    }
}

void SourceEntryResolver::fail(const QString &error)
{
    reset();
    emit failed(error);
}

void SourceEntryResolver::cancel()
{
    if (!busy) return;
    reset();
    emit cancelled();
}

}
