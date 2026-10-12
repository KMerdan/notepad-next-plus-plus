// SPDX-License-Identifier: GPL-3.0-or-later
#include "GitHubReleaseService.h"
#include "ZipExtractor.h"
#include "SemVer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
namespace PluginPackages {
namespace {
constexpr qint64 MaxResponse=4*1024*1024,MaxListing=16*1024*1024;
constexpr qint64 MaxChecksum=64*1024;
const QString ChecksumName="notepadnext-plugin-checksums.json";
bool integer(const QJsonValue &value,qint64 *out) {
    const auto n=value.toDouble(-1);
    if(!value.isDouble()||!std::isfinite(n)||n<1||n>9007199254740991.0||std::floor(n)!=n)return false;
    *out=value.toInteger();return true;
}
bool allowedUrl(const QUrl &url) {
    return url.isValid()&&url.scheme()=="https"&&url.userInfo().isEmpty()&&url.port(-1)==-1&&url.fragment().isEmpty()
        &&QStringList{"api.github.com","github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"}.contains(url.host());
}
QUrl listingUrl(const QString &repository,int page) {
    QUrl url("https://api.github.com/repos/"+repository.mid(19)+"/releases");QUrlQuery query;query.addQueryItem("per_page","100");query.addQueryItem("page",QString::number(page));url.setQuery(query);return url;
}
QUrl assetUrl(const QString &repository,qint64 id) {return QUrl("https://api.github.com/repos/"+repository.mid(19)+"/releases/assets/"+QString::number(id));}
bool parseDigest(const QJsonValue &value,QString *digest) {
    digest->clear();if(value.isNull()||value.isUndefined())return true;
    static const QRegularExpression sha("\\Asha256:([a-f0-9]{64})\\z");const auto match=sha.match(value.toString());
    if(!value.isString()||!match.hasMatch())return false;*digest=match.captured(1);return true;
}
}
QJsonObject ReleaseAsset::provenance(const QString &actual) const {
    QJsonObject result{{"kind","github-release"},{"repository",repository},{"tag",tag},{"releaseId",releaseId},{"assetId",assetId},{"assetName",name},{"archiveSha256",actual},{"upstreamSha256",digest},{"upstreamDigestVerified",!digest.isEmpty()},{"integritySource",checksumAssetId?"author-checksum":"github-digest"}};
    if(checksumAssetId){result["checksumAssetId"]=checksumAssetId;result["checksumAssetDigest"]=checksumDigest;}return result;
}
QString GitHubReleaseService::platformAssetName() {
#ifdef Q_OS_MACOS
    const QString platform="macos";
#elif defined(Q_OS_WIN)
    const QString platform="windows";
#else
    const QString platform="linux";
#endif
    auto arch=QSysInfo::currentCpuArchitecture();if(arch=="x86_64")arch="x64";else if(arch=="aarch64")arch="arm64";
    return "notepadnext-plugin-"+platform+'-'+arch+".zip";
}
GitHubReleaseService::GitHubReleaseService(QObject *parent,QNetworkAccessManager *provided):QObject(parent),network(provided?provided:new QNetworkAccessManager(this)) {
    deadline.setSingleShot(true);connect(&deadline,&QTimer::timeout,this,[this]{fail(stage==Stage::Listing?tr("GitHub release lookup timed out."):tr("Plugin download timed out."));});
}
void GitHubReleaseService::discover(const PluginSources::RegisteredResource &selectedResource) {
    cancel();QString canonical,error;
    if(!PluginSources::PluginMetadata::normalizeRepository(selectedResource.repository,&canonical,&error)){emit failed(error);return;}
    PluginSources::SourceSnapshot checked;
    if(!PluginSources::PluginMetadata::parse(QJsonDocument(selectedResource.metadata).toJson(QJsonDocument::Compact),canonical,{},&checked,&error)||checked.isMarketplace()||checked.metadata["id"]!=selectedResource.id){emit failed(error.isEmpty()?tr("Invalid selected plugin resource."):error);return;}
    resource=selectedResource;resource.repository=canonical;assets.clear();listingReady=false;page=1;listBytes=0;stage=Stage::Listing;busy=true;deadline.start(60000);request(listingUrl(canonical,page));
}
void GitHubReleaseService::request(const QUrl &url) {
    if(!allowedUrl(url)){fail(tr("The release request uses an unsupported download URL."));return;}
    buffer.clear();QNetworkRequest request(url);
    request.setRawHeader("Accept",stage==Stage::Listing?"application/vnd.github+json":"application/octet-stream");
    request.setRawHeader("User-Agent","NotepadNext-PluginManager");request.setRawHeader("X-GitHub-Api-Version","2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(15000);
    reply=network->get(request);reply->setReadBufferSize(65536);
    connect(reply,&QNetworkReply::readyRead,this,[this]{readAvailable();});
    connect(reply,&QNetworkReply::metaDataChanged,this,[this] {
        if(!reply)return;const auto length=reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(status!=200)return;
        if(stage==Stage::Listing&&length>MaxResponse)fail(tr("Release metadata exceeds the 4 MiB response limit."));
        else if(stage==Stage::Checksum&&length>0&&(length!=selected.checksumSize||length>MaxChecksum))fail(tr("Release checksums differ from their reviewed metadata."));
        else if(stage==Stage::Download&&length>0&&(length!=selected.size||length>ZipExtractor::MaxArchiveBytes))fail(tr("Release asset size differs from its reviewed metadata."));
    });
    connect(reply,&QNetworkReply::finished,this,&GitHubReleaseService::finishReply);
}
bool GitHubReleaseService::readAvailable() {
    if(!reply)return false;
    if(stage==Stage::Listing) {
        const auto chunk=reply->read(MaxResponse+1-buffer.size());buffer+=chunk;listBytes+=chunk.size();
        if(buffer.size()>MaxResponse||listBytes>MaxListing){fail(tr("GitHub release metadata exceeds its size budget."));return false;}
    }else if(stage==Stage::Checksum) {
        if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){reply->read(65536);return true;}
        buffer+=reply->read(MaxChecksum+1-buffer.size());
        if(buffer.size()>selected.checksumSize||buffer.size()>MaxChecksum){fail(tr("Release checksums exceed their size budget."));return false;}
    }else {
        // Redirect/error bodies never enter the package or checksum stream.
        if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){reply->read(65536);return true;}
        while(reply&&reply->bytesAvailable()>0) {
            const auto bytes=reply->read(65536);received+=bytes.size();
            if(received>selected.size||received>ZipExtractor::MaxArchiveBytes){fail(tr("Plugin download exceeds its declared size or the 128 MiB limit."));return false;}
            if(output.write(bytes)!=bytes.size()){fail(tr("Cannot write the plugin download."));return false;}checksum.addData(bytes);
        }
        emit progress(received,selected.size);
    }
    return true;
}
void GitHubReleaseService::finishReply() {
    if(!reply||!readAvailable()||!reply)return;
    auto *finished=reply.data();const int status=finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto error=finished->error();const auto message=finished->errorString();
    const auto redirect=finished->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();const auto url=finished->url();reply=nullptr;finished->deleteLater();
    if(status>=300&&status<400) {
        if(stage==Stage::Listing||redirect.isEmpty()||++redirects>5||!allowedUrl(url.resolved(redirect))){fail(tr("The release returned an unsupported redirect. Register the canonical repository URL."));return;}
        request(url.resolved(redirect));return;
    }
    if(status==404){fail(tr("The public release or plugin asset was not found."));return;}
    if(status==403||status==429){fail(tr("GitHub denied the release request or its rate limit was reached. Try again later."));return;}
    if(error!=QNetworkReply::NoError||status!=200){fail(tr("Cannot fetch the GitHub release: %1").arg(message));return;}
    if(stage==Stage::Checksum) {if(readAuthorChecksum())startArchiveDownload();return;}
    if(stage==Stage::Download) {
        const auto actual=QString::fromLatin1(checksum.result().toHex());
        if(received!=selected.size||(!selected.digest.isEmpty()&&selected.digest!=actual)||!output.flush()){fail(tr("Plugin download size, SHA-256 or flush verification failed."));return;}
        const auto path=output.fileName();const auto asset=selected;output.close();reset(true);emit downloaded(path,asset,actual);return;
    }
    QJsonParseError parseError;const auto document=QJsonDocument::fromJson(buffer,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isArray()||document.array().size()>100){fail(tr("GitHub returned invalid release metadata."));return;}
    for(const auto &value:document.array()) {
        if(!value.isObject()){fail(tr("Invalid release record."));return;}
        const auto release=value.toObject();
        if(!release["draft"].isBool()||!release["prerelease"].isBool()||!release["tag_name"].isString()||!release["assets"].isArray()){fail(tr("Invalid release record fields."));return;}
        if(release["draft"].toBool()||release["prerelease"].toBool())continue;
        const auto tag=release["tag_name"].toString();auto version=tag;
        const auto prefix=resource.id+QStringLiteral("/v");
        if(tag.startsWith(prefix))version=tag.mid(prefix.size());
        else if(version.startsWith('v'))version.remove(0,1);
        SemVer semver;
        // Catalog permissions/version remain the review authority; installing
        // a different release requires first refreshing that resource metadata.
        if(tag.size()>258||!SemVer::parse(version,&semver)||!semver.prerelease.isEmpty()||version!=resource.metadata["version"].toString())continue;
        qint64 releaseId=0;if(!integer(release["id"],&releaseId)||release["assets"].toArray().size()>128){fail(tr("Invalid release ID or asset count."));return;}
        QJsonObject checksumAsset;bool duplicateChecksum=false;
        for(const auto &item:release["assets"].toArray())if(item.toObject()["name"]==ChecksumName){if(!checksumAsset.isEmpty())duplicateChecksum=true;checksumAsset=item.toObject();}
        QVector<ReleaseAsset> specific,generic;
        for(const auto &item:release["assets"].toArray()) {
            const auto object=item.toObject();const auto name=object["name"].toString();
            if(name!=platformAssetName()&&name!="notepadnext-plugin.zip")continue;
            ReleaseAsset asset;asset.repository=resource.repository;asset.tag=tag;asset.name=name;asset.releaseId=releaseId;asset.publishedAt=QDateTime::fromString(release["published_at"].toString(),Qt::ISODate);
            if(object["state"]!="uploaded"||!integer(object["id"],&asset.assetId)||!integer(object["size"],&asset.size)||asset.size>ZipExtractor::MaxArchiveBytes||!asset.publishedAt.isValid()){fail(tr("Invalid plugin release asset."));return;}
            if(!parseDigest(object["digest"],&asset.digest)){fail(tr("Unsupported release asset digest."));return;}
            if(asset.digest.isEmpty()) {
                // A locally calculated hash cannot verify download integrity.
                // Older releases must provide an author checksum in this same
                // reviewed release; arbitrary metadata download URLs are unused.
                if(checksumAsset.isEmpty())continue;
                if(duplicateChecksum||checksumAsset["state"]!="uploaded"||!integer(checksumAsset["id"],&asset.checksumAssetId)||asset.checksumAssetId==asset.assetId||!integer(checksumAsset["size"],&asset.checksumSize)||asset.checksumSize>MaxChecksum||!parseDigest(checksumAsset["digest"],&asset.checksumDigest)){fail(tr("Invalid release checksum asset."));return;}
            }
            if(name==platformAssetName())specific.append(asset);else generic.append(asset);
        }
        assets+=specific.isEmpty()?generic:specific;
        if(assets.size()>64){fail(tr("Too many matching plugin release assets."));return;}
    }
    if(document.array().size()==100) {
        if(page>=5){fail(tr("Release lookup reached its 500-release limit. Use a smaller release history or a catalog version in that history."));return;}
        request(listingUrl(resource.repository,++page));return;
    }
    // Multiple plugins may publish the same version in one marketplace repo.
    // Prefer this plugin's namespace; keep v<version> for existing single-plugin
    // releases. Package validation still verifies the actual ID and metadata.
    const auto namespacedTag=resource.id+QStringLiteral("/v")+resource.metadata["version"].toString();
    if(std::any_of(assets.begin(),assets.end(),[&](const ReleaseAsset &asset){return asset.tag==namespacedTag;}))
        assets.erase(std::remove_if(assets.begin(),assets.end(),[&](const ReleaseAsset &asset){return asset.tag!=namespacedTag;}),assets.end());
    std::stable_sort(assets.begin(),assets.end(),[](const ReleaseAsset &a,const ReleaseAsset &b){return a.publishedAt>b.publishedAt;});
    reset();listingReady=true;emit releasesFound(assets);
}
bool GitHubReleaseService::download(const ReleaseAsset &asset,const QString &filePath) {
    if(busy||!listingReady)return false;
    const bool offered=std::any_of(assets.begin(),assets.end(),[&](const ReleaseAsset &a){return a.repository==asset.repository&&a.tag==asset.tag&&a.name==asset.name&&a.digest==asset.digest&&a.releaseId==asset.releaseId&&a.assetId==asset.assetId&&a.size==asset.size&&a.checksumAssetId==asset.checksumAssetId&&a.checksumSize==asset.checksumSize&&a.checksumDigest==asset.checksumDigest;});
    if(!offered){emit failed(tr("Select an asset from the current release list."));return false;}
    output.setFileName(filePath);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)){emit failed(tr("Cannot create a new plugin download file."));return false;}
    selected=asset;received=0;redirects=0;checksum.reset();busy=true;deadline.start(120000);
    if(asset.digest.isEmpty()){stage=Stage::Checksum;request(assetUrl(asset.repository,asset.checksumAssetId));}else startArchiveDownload();return true;
}
bool GitHubReleaseService::readAuthorChecksum() {
    const auto actual=QString::fromLatin1(QCryptographicHash::hash(buffer,QCryptographicHash::Sha256).toHex());
    if(buffer.size()!=selected.checksumSize||(!selected.checksumDigest.isEmpty()&&selected.checksumDigest!=actual)){fail(tr("Release checksum size or SHA-256 verification failed."));return false;}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(buffer,&error);const auto object=document.object();
    if(error.error!=QJsonParseError::NoError||!document.isObject()||object.size()!=4||object["schemaVersion"]!=1||object["pluginId"]!=resource.id||object["version"]!=resource.metadata["version"]||!object["assets"].isArray()||object["assets"].toArray().isEmpty()||object["assets"].toArray().size()>16){fail(tr("Release checksums do not match this registered plugin and version."));return false;}
    static const QRegularExpression name("\\Anotepadnext-plugin(?:-(?:macos|windows|linux)-(?:x64|arm64))?\\.zip\\z"),hash("\\A[a-f0-9]{64}\\z");
    QSet<QString> names;QString expected;
    for(const auto &value:object["assets"].toArray()) {
        const auto entry=value.toObject();const auto filename=entry["name"].toString();qint64 size=0;
        if(!value.isObject()||entry.size()!=3||!entry["name"].isString()||!name.match(filename).hasMatch()||names.contains(filename)||!entry["sha256"].isString()||!hash.match(entry["sha256"].toString()).hasMatch()||!integer(entry["size"],&size)||size>ZipExtractor::MaxArchiveBytes){fail(tr("Invalid author release checksum entry."));return false;}
        names.insert(filename);if(filename==selected.name){if(size!=selected.size){fail(tr("Author release checksum size differs from the selected archive."));return false;}expected=entry["sha256"].toString();}
    }
    if(expected.isEmpty()){fail(tr("The author checksums do not include the selected archive."));return false;}
    selected.digest=expected;selected.checksumDigest=actual;return true;
}
void GitHubReleaseService::startArchiveDownload() {stage=Stage::Download;redirects=0;request(assetUrl(selected.repository,selected.assetId));}
void GitHubReleaseService::reset(bool keepFile) {
    deadline.stop();busy=false;buffer.clear();
    if(reply){auto *pending=reply.data();reply=nullptr;pending->disconnect(this);pending->abort();pending->deleteLater();}
    if(output.isOpen()){output.close();if(!keepFile)QFile::remove(output.fileName());}
}
void GitHubReleaseService::fail(const QString &error){reset();emit failed(error);}
void GitHubReleaseService::cancel(){if(!busy)return;reset();emit cancelled();}
}
