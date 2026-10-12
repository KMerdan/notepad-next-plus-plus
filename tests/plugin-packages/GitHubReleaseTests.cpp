// SPDX-License-Identifier: GPL-3.0-or-later
#include "GitHubReleaseService.h"
#include "InstallationService.h"
#include "ZipExtractor.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QLockFile>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QTest>
#include <QtEndian>
#include <zlib.h>
#include <cstring>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#endif
using namespace PluginPackages;
namespace {
struct Response {QByteArray bytes;int status=200;QUrl redirect;int delay=0;qint64 declaredLength=-1;};
class FakeReply:public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request,const Response &response,QObject *parent):QNetworkReply(parent),content(response.bytes) {
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,response.status);
        if(!response.redirect.isEmpty())setAttribute(QNetworkRequest::RedirectionTargetAttribute,response.redirect);
        setHeader(QNetworkRequest::ContentLengthHeader,response.declaredLength<0?content.size():response.declaredLength);
        open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        if(response.status>=400)setError(ContentNotFoundError,"HTTP failure");
        QTimer::singleShot(response.delay,this,[this]{if(isFinished())return;emit metaDataChanged();if(isFinished())return;emit readyRead();if(isFinished())return;setFinished(true);emit finished();});
    }
    void abort()override{if(isFinished())return;setFinished(true);setError(OperationCanceledError,"cancelled");emit finished();}
    qint64 bytesAvailable()const override{return content.size()-offset+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 size)override{const auto count=qMin(size,qint64(content.size()-offset));if(count<=0)return -1;std::memcpy(data,content.constData()+offset,size_t(count));offset+=count;return count;}
private:QByteArray content;qint64 offset=0;
};
class FakeNetwork:public QNetworkAccessManager {
public:QList<Response> responses;QList<QNetworkRequest> requests;
protected:
    QNetworkReply *createRequest(Operation operation,const QNetworkRequest &request,QIODevice *body)override {
        if(operation!=GetOperation||body||responses.isEmpty())qFatal("Unexpected release request");requests.append(request);return new FakeReply(request,responses.takeFirst(),this);
    }
};
PluginSources::RegisteredResource resource(const QString &version="1.2.3") {
    QJsonObject m{{"manifestVersion",1},{"id","tests.release"},{"name","Release Sample"},{"version",version},{"list",false},
        {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.1.0"}}},{"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.js"}}},
        {"capabilities",QJsonObject{{"required",QJsonArray{}},{"optional",QJsonArray{}}}},{"activationEvents",QJsonArray{"onStartup"}}};
    PluginSources::SourceSnapshot s;QString error;if(!PluginSources::PluginMetadata::parse(QJsonDocument(m).toJson(),"https://github.com/tests/release",{},&s,&error))qFatal("%s",qPrintable(error));return s.resources[0];
}
QJsonObject asset(const QByteArray &bytes=QByteArray("zip content"),const QString &name="notepadnext-plugin.zip",qint64 id=20) {
    return {{"id",id},{"name",name},{"size",bytes.size()},{"state","uploaded"},{"digest","sha256:"+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
}
QJsonObject release(const QJsonArray &assets=QJsonArray{asset()},const QString &tag="v1.2.3",qint64 id=10) {
    return {{"id",id},{"draft",false},{"prerelease",false},{"tag_name",tag},{"published_at","2026-10-10T00:00:00Z"},{"assets",assets}};
}
QByteArray listing(const QJsonArray &records=QJsonArray{release()}){return QJsonDocument(records).toJson(QJsonDocument::Compact);}
QJsonObject authorChecksums(const QByteArray &bytes=QByteArray("zip content")) {
    return {{"schemaVersion",1},{"pluginId","tests.release"},{"version","1.2.3"},{"assets",QJsonArray{QJsonObject{{"name","notepadnext-plugin.zip"},{"size",bytes.size()},{"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}}}}};
}
QVector<ReleaseAsset> offered(QSignalSpy &spy){return qvariant_cast<QVector<ReleaseAsset>>(spy[0][0]);}
void le16(QByteArray &b,quint16 n){char bytes[2];qToLittleEndian(n,bytes);b.append(bytes,2);}
void le32(QByteArray &b,quint32 n){char bytes[4];qToLittleEndian(n,bytes);b.append(bytes,4);}
QByteArray packageZip(const PluginSources::RegisteredResource &selected=resource()) {
    QList<QPair<QByteArray,QByteArray>> files={{"notepadnext-plugin.json",QJsonDocument(selected.metadata).toJson()},{"dist/index.js","throw new Error('install must not run plugin code')"},{"LICENSE","test license"}};
    QByteArray local,central;
    for(const auto &f:files) {
        const auto offset=local.size();const auto crc=quint32(crc32(0,reinterpret_cast<const Bytef*>(f.second.constData()),uInt(f.second.size())));
        le32(local,0x04034b50);le16(local,20);le16(local,0);le16(local,0);le32(local,0);le32(local,crc);le32(local,f.second.size());le32(local,f.second.size());le16(local,f.first.size());le16(local,0);local+=f.first;local+=f.second;
        le32(central,0x02014b50);le16(central,20);le16(central,20);le16(central,0);le16(central,0);le32(central,0);le32(central,crc);le32(central,f.second.size());le32(central,f.second.size());le16(central,f.first.size());le16(central,0);le16(central,0);le16(central,0);le16(central,0);le32(central,0);le32(central,offset);central+=f.first;
    }
    QByteArray end;le32(end,0x06054b50);le16(end,0);le16(end,0);le16(end,files.size());le16(end,files.size());le32(end,central.size());le32(end,local.size());le16(end,0);return local+central+end;
}
}
class GitHubReleaseTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){qRegisterMetaType<QVector<ReleaseAsset>>();qRegisterMetaType<PreparedPackage>();}
    void matchingAssetsAndNoInstallation();
    void paginationAndBudget();
    void downloadRedirectAndProvenance();
    void invalidDownload_data();
    void invalidDownload();
    void lookupFailureAndCancel();
    void installTransaction();
    void releaseUpdatePreparation();
    void authorChecksumFallback();
    void invalidAuthorChecksum_data();
    void invalidAuthorChecksum();
    void checksumDiscoveryAndSelection();
    void installWithAuthorChecksum();
    void localInstallationWaitsForMaintenance_data();
    void localInstallationWaitsForMaintenance();
};
void GitHubReleaseTests::localInstallationWaitsForMaintenance_data() {
    QTest::addColumn<int>("operation");QTest::newRow("release-lock-and-install")<<0;
    QTest::newRow("cancel-while-lock-held")<<1;QTest::newRow("bounded-lock-timeout")<<2;
#ifdef Q_OS_WIN
    QTest::newRow("recover-temporary-permission-error")<<3;
    QTest::newRow("cancel-permission-retry")<<4;
    QTest::newRow("bounded-permission-timeout")<<5;
#endif
}
void GitHubReleaseTests::localInstallationWaitsForMaintenance() {
    QFETCH(int,operation);QTemporaryDir temp;const auto selected=resource();const auto input=temp.path()+"/input",store=temp.path()+"/store";
    QVERIFY(QDir().mkpath(input+"/dist"));QVERIFY(QDir().mkpath(store));
    const auto write=[&](const QString &name,const QByteArray &data){QFile file(input+'/'+name);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();};
    QVERIFY(write("notepadnext-plugin.json",QJsonDocument(selected.metadata).toJson()));QVERIFY(write("dist/index.js","throw new Error('installation must not execute code');"));QVERIFY(write("LICENSE","test license"));
    QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",store);QVERIFY(index.load(&error));InstallationService installer(&index,PackagePolicy{});
    QSignalSpy finished(&installer,&InstallationService::finished);QLockFile maintenance(store+"/.store.lock");maintenance.setStaleLockTime(0);QVERIFY(maintenance.tryLock(0));
    // Timer callbacks must not outlive this row when an assertion fails.
    QTimer release;release.setSingleShot(true);
#ifdef Q_OS_WIN
    auto name=QDir::toNativeSeparators(store);
    PSECURITY_DESCRIPTOR originalDescriptor=nullptr;PACL originalAcl=nullptr;
    const auto freeDescriptor=qScopeGuard([&]{if(originalDescriptor)LocalFree(originalDescriptor);});
    bool permissionsChanged=false;
    const auto restorePermissions=[&]{
        if(!permissionsChanged)return true;
        const auto status=SetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(name.data()),SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,nullptr,nullptr,originalAcl,nullptr);
        if(status==ERROR_SUCCESS)permissionsChanged=false;
        return status==ERROR_SUCCESS;
    };
    const auto restoreOnExit=qScopeGuard([&]{restorePermissions();});
    if(operation>=3) {
        maintenance.unlock();
        QCOMPARE(GetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(name.data()),SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,nullptr,nullptr,&originalAcl,nullptr,&originalDescriptor),DWORD(ERROR_SUCCESS));
        HANDLE token=nullptr;QVERIFY(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));
        const auto closeToken=qScopeGuard([&]{CloseHandle(token);});
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);QVERIFY(size>0&&size<65536);
        QByteArray principal(int(size),'\0');
        QVERIFY(GetTokenInformation(token,TokenUser,principal.data(),size,&size));
        EXPLICIT_ACCESSW denied{};denied.grfAccessPermissions=FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY;
        denied.grfAccessMode=DENY_ACCESS;denied.grfInheritance=NO_INHERITANCE;
        denied.Trustee.TrusteeForm=TRUSTEE_IS_SID;denied.Trustee.TrusteeType=TRUSTEE_IS_USER;
        denied.Trustee.ptstrName=static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(principal.data())->User.Sid);
        PACL deniedAcl=nullptr;const auto freeAcl=qScopeGuard([&]{if(deniedAcl)LocalFree(deniedAcl);});
        QCOMPARE(SetEntriesInAclW(1,&denied,originalAcl,&deniedAcl),DWORD(ERROR_SUCCESS));
        QCOMPARE(SetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(name.data()),SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,nullptr,nullptr,deniedAcl,nullptr),DWORD(ERROR_SUCCESS));
        permissionsChanged=true;
        // The namespace is free, but the real directory ACL denies creation.
        // Prove the exact native Qt error before exercising the worker retry.
        QLockFile probe(store+"/.store.lock");probe.setStaleLockTime(0);
        QVERIFY(!probe.tryLock(0));QCOMPARE(probe.error(),QLockFile::PermissionError);
        connect(&release,&QTimer::timeout,this,[&]{QVERIFY(restorePermissions());});
    } else
#endif
        connect(&release,&QTimer::timeout,this,[&]{maintenance.unlock();});
    int ticks=0;QTimer responsive;connect(&responsive,&QTimer::timeout,this,[&]{++ticks;});responsive.start(20);QElapsedTimer elapsed;elapsed.start();
    QVERIFY(installer.installDirectory(input,selected));QVERIFY(installer.isBusy());QCOMPARE(index.plugins().size(),0);
    if(operation==0||operation==3)release.start(180);
    if(operation==1||operation==4)QTimer::singleShot(40,&installer,&InstallationService::cancel);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,2500);QVERIFY(!installer.isBusy());
    const auto message=finished[0][1].toString();
    if(operation==0||operation==3){QVERIFY2(message.isEmpty(),qPrintable(message));QCOMPARE(index.plugins().size(),1);QVERIFY(ticks>=3);}
    else {QCOMPARE(index.plugins().size(),0);QCOMPARE(maintenance.isLocked(),operation<3);QVERIFY(!message.isEmpty());
        if(operation==1||operation==4){QVERIFY(message.contains("cancelled",Qt::CaseInsensitive));QVERIFY(elapsed.elapsed()<500);}
        else {QVERIFY(message.contains("transaction"));QVERIFY(elapsed.elapsed()>=900);QVERIFY(elapsed.elapsed()<2500);QVERIFY(ticks>=3);}
    }
    QCOMPARE(QDir(store).entryList({".stage-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
}
void GitHubReleaseTests::matchingAssetsAndNoInstallation() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),failed(&service,&GitHubReleaseService::failed);
    auto draft=release();draft["draft"]=true;auto prerelease=release();prerelease["prerelease"]=true;
    network.responses={{listing({draft,prerelease,release({asset()},"v2.0.0"),release({asset("zip content",GitHubReleaseService::platformAssetName(),21),asset()})})}};
    service.discover(resource());QTRY_COMPARE(found.size(),1);QVERIFY(failed.isEmpty());const auto assets=offered(found);QCOMPARE(assets.size(),1);QCOMPARE(assets[0].assetId,21);QCOMPARE(assets[0].name,GitHubReleaseService::platformAssetName());QVERIFY(!service.isBusy());QCOMPARE(network.requests.size(),1);QCOMPARE(network.requests[0].url().path(),QString("/repos/tests/release/releases"));
    QTemporaryDir t;auto forged=assets[0];forged.digest=QString(64,'0');QVERIFY(!service.download(forged,t.path()+"/forged"));QVERIFY(!QFileInfo::exists(t.path()+"/forged"));
}
void GitHubReleaseTests::paginationAndBudget() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),failed(&service,&GitHubReleaseService::failed);QJsonArray page;
    for(int i=0;i<100;++i)page.append(release({},"v0.0.1",i+1));network.responses={{listing(page)},{listing()}};
    service.discover(resource());QTRY_COMPARE(found.size(),1);QCOMPARE(network.requests.size(),2);QVERIFY(network.requests[1].url().query().contains("page=2"));QCOMPARE(offered(found).size(),1);
    network.responses={{QByteArray(4*1024*1024+1,'x')}};service.discover(resource());QTRY_COMPARE(failed.size(),1);QVERIFY(failed.last()[0].toString().contains("budget")||failed.last()[0].toString().contains("limit"));
    network.responses.clear();for(int i=0;i<5;++i)network.responses.append({listing(page)});service.discover(resource());QTRY_COMPARE(failed.size(),2);QVERIFY(failed.last()[0].toString().contains("500-release"));
}
void GitHubReleaseTests::downloadRedirectAndProvenance() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),done(&service,&GitHubReleaseService::downloaded),failed(&service,&GitHubReleaseService::failed);
    const QByteArray payload="zip content";network.responses={{listing()},{"redirect body is not package bytes",302,QUrl("https://release-assets.githubusercontent.com/github-production-release-asset/test?token=temporary")},{payload}};
    service.discover(resource());QTRY_COMPARE(found.size(),1);QTemporaryDir t;const auto selected=offered(found)[0];QVERIFY(service.download(selected,t.path()+"/plugin.zip"));QTRY_COMPARE(done.size(),1);QVERIFY(failed.isEmpty());
    QFile file(t.path()+"/plugin.zip");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),payload);QCOMPARE(network.requests.size(),3);QCOMPARE(network.requests[1].url().path(),QString("/repos/tests/release/releases/assets/20"));QCOMPARE(network.requests[1].rawHeader("Accept"),QByteArray("application/octet-stream"));QVERIFY(!network.requests[2].hasRawHeader("Authorization"));
    const auto provenance=selected.provenance(done[0][2].toString());QCOMPARE(provenance["archiveSha256"].toString(),selected.digest);QVERIFY(provenance["upstreamDigestVerified"].toBool());QCOMPARE(provenance["releaseId"].toInteger(),10);
}
void GitHubReleaseTests::invalidDownload_data() {
    QTest::addColumn<int>("kind");QTest::newRow("sha mismatch")<<0;QTest::newRow("oversize")<<1;QTest::newRow("truncated")<<2;QTest::newRow("redirect host")<<3;QTest::newRow("redirect http")<<4;QTest::newRow("too many redirects")<<5;QTest::newRow("rate limit")<<6;QTest::newRow("not found")<<7;QTest::newRow("cancel")<<8;QTest::newRow("declared size")<<9;
}
void GitHubReleaseTests::invalidDownload() {
    QFETCH(int,kind);FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),failed(&service,&GitHubReleaseService::failed),cancelled(&service,&GitHubReleaseService::cancelled),done(&service,&GitHubReleaseService::downloaded);
    network.responses={{listing()}};service.discover(resource());QTRY_COMPARE(found.size(),1);const auto selected=offered(found)[0];
    if(kind==0)network.responses={{"ZIP CONTENT"}};
    if(kind==1)network.responses={{"zip content too large",200,{},0,0}};
    if(kind==2)network.responses={{"zip",200,{},0,0}};
    if(kind==3)network.responses={{"",302,QUrl("https://evil.example/package.zip")}};
    if(kind==4)network.responses={{"",302,QUrl("http://github.com/package.zip")}};
    if(kind==5)for(int i=0;i<6;++i)network.responses.append({"",302,QUrl("https://objects.githubusercontent.com/loop")});
    if(kind==6)network.responses={{"error",429}};
    if(kind==7)network.responses={{"error",404}};
    if(kind==8)network.responses={{"zip content",200,{},100}};
    if(kind==9)network.responses={{"zip content",200,{},0,12}};
    QTemporaryDir t;const auto path=t.path()+"/plugin.zip";QVERIFY(service.download(selected,path));if(kind==8)service.cancel();
    QTRY_VERIFY(failed.size()+cancelled.size()==1);QVERIFY(done.isEmpty());QVERIFY(!QFileInfo::exists(path));QVERIFY(!service.isBusy());
}
void GitHubReleaseTests::lookupFailureAndCancel() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),failed(&service,&GitHubReleaseService::failed),cancelled(&service,&GitHubReleaseService::cancelled);
    network.responses={{"{}"},{"error",404},{"error",403},{"",302,QUrl("https://github.com/new/repo")},{listing(),200,{},100}};
    for(int i=0;i<4;++i){service.discover(resource());QTRY_COMPARE(failed.size(),i+1);}
    service.discover(resource());service.cancel();QCOMPARE(cancelled.size(),1);QVERIFY(found.isEmpty());QTemporaryDir t;ReleaseAsset forged;QVERIFY(!service.download(forged,t.path()+"/file"));
    auto bad=asset();bad["digest"]="sha256:invalid";network.responses={{listing({release({bad})})}};service.discover(resource());QTRY_COMPARE(failed.size(),5);
}
void GitHubReleaseTests::installTransaction() {
    QTemporaryDir t;FakeNetwork network;InstalledPluginRegistry registry(t.path()+"/installed.json",t.path()+"/store");QString error;QVERIFY(registry.load(&error));InstallationService installer(&registry,PackagePolicy(),nullptr,&network);
    QSignalSpy available(&installer,&InstallationService::releasesAvailable),finished(&installer,&InstallationService::finished);const auto archive=packageZip();network.responses={{listing({release({asset(archive)})})},{archive}};
    QVERIFY(installer.lookupReleases(resource()));QTRY_COMPARE(available.size(),1);QVERIFY(registry.plugins().isEmpty());QVERIFY(!QFileInfo::exists(t.path()+"/store"));
    const auto assets=qvariant_cast<QVector<ReleaseAsset>>(available[0][1]);QVERIFY(installer.installRelease(assets[0]));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QVERIFY2(finished[0][1].toString().isEmpty(),qPrintable(finished[0][1].toString()));
    QCOMPARE(registry.plugins().size(),1);QVERIFY(!registry.plugins()[0].enabled);QCOMPARE(registry.plugins()[0].package.metadata,resource().metadata);QVERIFY(registry.plugins()[0].origin["upstreamDigestVerified"].toBool());QVERIFY(PackageStore(registry.packageStorePath()).verify(registry.plugins()[0].package,&error));
    QVERIFY(QDir(registry.packageStorePath()).entryList({".download-*",".unpack-*",".stage-*"},QDir::Dirs|QDir::Hidden).isEmpty());
    QVERIFY(!installer.lookupReleases(resource()));
}
void GitHubReleaseTests::releaseUpdatePreparation() {
    QTemporaryDir t;FakeNetwork network;InstalledPluginRegistry registry(t.path()+"/installed.json",t.path()+"/store");QString error;QVERIFY(registry.load(&error));InstallationService installer(&registry,PackagePolicy(),nullptr,&network);
    QSignalSpy available(&installer,&InstallationService::releasesAvailable),finished(&installer,&InstallationService::finished),prepared(&installer,&InstallationService::updatePrepared);
    const auto initial=packageZip();network.responses={{listing({release({asset(initial)})})},{initial}};
    QVERIFY(installer.lookupReleases(resource()));QTRY_COMPARE(available.size(),1);QVERIFY(installer.installRelease(qvariant_cast<QVector<ReleaseAsset>>(available[0][1])[0]));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QVERIFY(finished[0][1].toString().isEmpty());
    const auto original=registry.plugins()[0];const auto next=resource("1.2.4");const auto archive=packageZip(next);network.responses={{listing({release({asset(archive)},"v1.2.4")})},{archive}};
    QVERIFY(installer.lookupReleases(next,true));QTRY_COMPARE(available.size(),2);QCOMPARE(registry.plugins()[0].package.digest,original.package.digest);
    QVERIFY(installer.installRelease(qvariant_cast<QVector<ReleaseAsset>>(available[1][1])[0]));QTRY_COMPARE_WITH_TIMEOUT(prepared.size(),1,5000);
    const auto candidate=qvariant_cast<PreparedPackage>(prepared[0][0]);QCOMPARE(candidate.metadata,next.metadata);QCOMPARE(prepared[0][3].toString(),original.package.digest);
    QVERIFY(PackageStore(registry.packageStorePath()).verify(candidate,&error));QCOMPARE(registry.plugins()[0].package.digest,original.package.digest);QVERIFY(!registry.plugins()[0].enabled);QCOMPARE(finished.size(),1);
    QVERIFY(QDir(registry.packageStorePath()).entryList({".download-*",".unpack-*",".stage-*"},QDir::Dirs|QDir::Hidden).isEmpty());
    // The runtime, not download completion, controls the version transaction.
    QVERIFY(registry.beginUpdate(candidate,next.sourceIds,prepared[0][2].toJsonObject(),original.package.digest,&error));QVERIFY(registry.confirmUpdate(next.id,&error));
    const auto newer=resource("1.2.5");network.responses={{listing({release({asset()},"v1.2.5")})}};
    QVERIFY(installer.lookupReleases(newer,true));QTRY_COMPARE(available.size(),3);QVERIFY(registry.remove(next.id,&error));
    QVERIFY(!installer.installRelease(qvariant_cast<QVector<ReleaseAsset>>(available[2][1])[0]));QVERIFY(registry.plugins().isEmpty());
}
void GitHubReleaseTests::authorChecksumFallback() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),done(&service,&GitHubReleaseService::downloaded),failed(&service,&GitHubReleaseService::failed);
    const QByteArray payload="zip content",checksums=QJsonDocument(authorChecksums(payload)).toJson(QJsonDocument::Compact);auto package=asset(payload);package["digest"]=QJsonValue::Null;
    const auto checksumAsset=asset(checksums,"notepadnext-plugin-checksums.json",21);
    network.responses={{listing({release({package,checksumAsset})})},{"redirect checksum body",302,QUrl("https://objects.githubusercontent.com/author-checksums")},{checksums},{"redirect package body",302,QUrl("https://release-assets.githubusercontent.com/plugin")},{payload}};
    service.discover(resource());QTRY_COMPARE(found.size(),1);const auto selected=offered(found)[0];QVERIFY(selected.digest.isEmpty());QCOMPARE(selected.checksumAssetId,21);
    QTemporaryDir t;QVERIFY(service.download(selected,t.path()+"/plugin.zip"));QTRY_COMPARE(done.size(),1);QVERIFY(failed.isEmpty());
    const auto verified=qvariant_cast<ReleaseAsset>(done[0][1]);const auto provenance=verified.provenance(done[0][2].toString());QVERIFY(provenance["upstreamDigestVerified"].toBool());QCOMPARE(provenance["integritySource"].toString(),QString("author-checksum"));QCOMPARE(provenance["checksumAssetId"].toInteger(),21);QCOMPARE(provenance["archiveSha256"],provenance["upstreamSha256"]);
    QCOMPARE(network.requests.size(),5);QCOMPARE(network.requests[1].url().path(),QString("/repos/tests/release/releases/assets/21"));QCOMPARE(network.requests[3].url().path(),QString("/repos/tests/release/releases/assets/20"));
    QFile file(t.path()+"/plugin.zip");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),payload);
}
void GitHubReleaseTests::invalidAuthorChecksum_data() {
    QTest::addColumn<int>("kind");const QStringList names={"wrong plugin","wrong version","missing archive","duplicate name","unknown field","invalid sha","path entry","archive size","truncated checksum","checksum digest","oversized checksum","redirect host","redirect http","rate limit","missing checksum","cancel checksum","archive hash mismatch","extra root field"};
    for(int i=0;i<names.size();++i)QTest::newRow(qPrintable(names[i]))<<i;
}
void GitHubReleaseTests::invalidAuthorChecksum() {
    QFETCH(int,kind);FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),done(&service,&GitHubReleaseService::downloaded),failed(&service,&GitHubReleaseService::failed),cancelled(&service,&GitHubReleaseService::cancelled);
    auto checksums=authorChecksums();auto entries=checksums["assets"].toArray();auto entry=entries[0].toObject();
    if(kind==0)checksums["pluginId"]="tests.foreign";if(kind==1)checksums["version"]="1.2.4";if(kind==2)entry["name"]="notepadnext-plugin-windows-arm64.zip";
    if(kind==4)entry["url"]="https://evil.example/plugin";if(kind==5)entry["sha256"]="invalid";if(kind==6)entry["name"]="../notepadnext-plugin.zip";if(kind==7)entry["size"]=12;
    entries[0]=entry;if(kind==3)entries.append(entry);checksums["assets"]=entries;if(kind==17)checksums["url"]="https://evil.example/checksums";
    auto bytes=QJsonDocument(checksums).toJson(QJsonDocument::Compact);auto checksumAsset=asset(bytes,"notepadnext-plugin-checksums.json",21);auto package=asset();package.remove("digest");
    if(kind==9)checksumAsset["digest"]="sha256:"+QString(64,'0');if(kind==10){bytes=QByteArray(65537,'x');checksumAsset=asset(QByteArray("{}"),"notepadnext-plugin-checksums.json",21);}
    network.responses={{listing({release({package,checksumAsset})})}};service.discover(resource());QTRY_COMPARE(found.size(),1);QCOMPARE(offered(found).size(),1);
    Response response{bytes};if(kind==8){response.bytes.chop(1);response.declaredLength=0;}if(kind==10)response.declaredLength=0;
    if(kind==11)response={"",302,QUrl("https://evil.example/checksums")};if(kind==12)response={"",302,QUrl("http://github.com/checksums")};if(kind==13)response={"error",429};if(kind==14)response={"error",404};if(kind==15)response.delay=100;
    network.responses={response};if(kind==16)network.responses.append({"ZIP CONTENT"});
    QTemporaryDir t;const auto path=t.path()+"/plugin.zip";QVERIFY(service.download(offered(found)[0],path));if(kind==15)service.cancel();
    QTRY_COMPARE(failed.size()+cancelled.size(),1);QVERIFY(done.isEmpty());QVERIFY(!QFileInfo::exists(path));QVERIFY(!service.isBusy());QVERIFY(network.responses.isEmpty());
}
void GitHubReleaseTests::checksumDiscoveryAndSelection() {
    FakeNetwork network;GitHubReleaseService service(nullptr,&network);QSignalSpy found(&service,&GitHubReleaseService::releasesFound),failed(&service,&GitHubReleaseService::failed);
    auto package=asset();package.remove("digest");network.responses={{listing({release({package})})}};service.discover(resource());QTRY_COMPARE(found.size(),1);QVERIFY(offered(found).isEmpty());
    const auto bytes=QJsonDocument(authorChecksums()).toJson(QJsonDocument::Compact);const auto checksumAsset=asset(bytes,"notepadnext-plugin-checksums.json",21);
    network.responses={{listing({release({package,checksumAsset})})}};service.discover(resource());QTRY_COMPARE(found.size(),2);auto forged=qvariant_cast<QVector<ReleaseAsset>>(found[1][0])[0];forged.checksumAssetId=999;QTemporaryDir t;QVERIFY(!service.download(forged,t.path()+"/forged"));QCOMPARE(failed.size(),1);QVERIFY(!QFileInfo::exists(t.path()+"/forged"));
    network.responses={{listing({release({package,checksumAsset,checksumAsset})})}};service.discover(resource());QTRY_COMPARE(failed.size(),2);
    auto huge=checksumAsset;huge["size"]=65537;network.responses={{listing({release({package,huge})})}};service.discover(resource());QTRY_COMPARE(failed.size(),3);
    // GitHub's digest is preferred and needs no checksum request.
    network.responses={{listing({release({asset(),checksumAsset})})},{"zip content"}};service.discover(resource());QTRY_COMPARE(found.size(),3);const auto native=qvariant_cast<QVector<ReleaseAsset>>(found[2][0])[0];QCOMPARE(native.checksumAssetId,0);QSignalSpy done(&service,&GitHubReleaseService::downloaded);QVERIFY(service.download(native,t.path()+"/native"));QTRY_COMPARE(done.size(),1);QCOMPARE(network.requests.last().url().path(),QString("/repos/tests/release/releases/assets/20"));
}
void GitHubReleaseTests::installWithAuthorChecksum() {
    QTemporaryDir t;FakeNetwork network;InstalledPluginRegistry registry(t.path()+"/installed.json",t.path()+"/store");QString error;QVERIFY(registry.load(&error));InstallationService installer(&registry,PackagePolicy(),nullptr,&network);
    const auto archive=packageZip(),checksums=QJsonDocument(authorChecksums(archive)).toJson(QJsonDocument::Compact);auto package=asset(archive);package.remove("digest");auto sidecar=asset(checksums,"notepadnext-plugin-checksums.json",21);sidecar.remove("digest");
    network.responses={{listing({release({package,sidecar})})},{checksums},{archive}};QSignalSpy available(&installer,&InstallationService::releasesAvailable),finished(&installer,&InstallationService::finished);
    QVERIFY(installer.lookupReleases(resource()));QTRY_COMPARE(available.size(),1);QVERIFY(registry.plugins().isEmpty());QVERIFY(installer.installRelease(qvariant_cast<QVector<ReleaseAsset>>(available[0][1])[0]));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QVERIFY2(finished[0][1].toString().isEmpty(),qPrintable(finished[0][1].toString()));
    const auto installed=registry.plugins()[0];QVERIFY(!installed.enabled);QCOMPARE(installed.origin["integritySource"].toString(),QString("author-checksum"));QVERIFY(installed.origin["upstreamDigestVerified"].toBool());QVERIFY(PackageStore(registry.packageStorePath()).verify(installed.package,&error));
}
QTEST_GUILESS_MAIN(GitHubReleaseTests)
#include "GitHubReleaseTests.moc"
