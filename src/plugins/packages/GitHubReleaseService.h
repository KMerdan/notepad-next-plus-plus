// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GITHUBRELEASESERVICE_H
#define GITHUBRELEASESERVICE_H
#include "PluginMetadata.h"
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <QUrl>
class QNetworkAccessManager;
class QNetworkReply;
namespace PluginPackages {
struct ReleaseAsset {
    QString repository,tag,name,digest;
    qint64 releaseId=0,assetId=0,size=0;
    qint64 checksumAssetId=0,checksumSize=0;
    QString checksumDigest;
    QDateTime publishedAt;
    QJsonObject provenance(const QString &actualDigest) const;
};
class GitHubReleaseService:public QObject {
    Q_OBJECT
public:
    explicit GitHubReleaseService(QObject *parent=nullptr,QNetworkAccessManager *network=nullptr);
    ~GitHubReleaseService() override {reset();}
    void discover(const PluginSources::RegisteredResource &resource);
    // Asset must come from the current successful discovery. A fresh API
    // request pins the returned asset ID, not an arbitrary metadata URL.
    bool download(const ReleaseAsset &asset,const QString &newFilePath);
    void cancel();
    bool isBusy() const {return busy;}
    static QString platformAssetName();
signals:
    void releasesFound(const QVector<PluginPackages::ReleaseAsset> &assets);
    void downloaded(const QString &filePath,const PluginPackages::ReleaseAsset &asset,const QString &sha256);
    void progress(qint64 received,qint64 total);
    void failed(const QString &error);
    void cancelled();
private:
    enum class Stage {Listing,Checksum,Download};
    void request(const QUrl &url);
    bool readAvailable();
    void finishReply();
    bool readAuthorChecksum();
    void startArchiveDownload();
    void reset(bool keepFile=false);
    void fail(const QString &message);
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QTimer deadline;
    bool busy=false;
    bool listingReady=false;
    Stage stage=Stage::Listing;
    PluginSources::RegisteredResource resource;
    QVector<ReleaseAsset> assets;
    QByteArray buffer;
    qint64 listBytes=0,received=0;
    int page=1,redirects=0;
    ReleaseAsset selected;
    QFile output;
    QCryptographicHash checksum{QCryptographicHash::Sha256};
};
}
Q_DECLARE_METATYPE(PluginPackages::ReleaseAsset)
Q_DECLARE_METATYPE(QVector<PluginPackages::ReleaseAsset>)
#endif
