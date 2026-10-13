// SPDX-License-Identifier: GPL-3.0-or-later
#include "WebviewService.h"
#include "../runtime/PreviewPolicy.h"
#include "HtmlPolicy.h"
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QMutex>
#include <QMutexLocker>
#include <QCloseEvent>
#include <QApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QPromise>
#include <QPalette>
#include <QFontDatabase>
#include <QTimer>
#include <QThreadPool>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QQuickWidget>
#include <QUuid>
#include <QWebChannel>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <functional>
#include <algorithm>
#include <limits>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QWebEnginePermission>
#endif
using PluginRpc::rpcError;
static void initCompatBridge() { Q_INIT_RESOURCE(compat_bridge); }
namespace PluginWebviews {
namespace {
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-'); }
struct PreparedHtml { QByteArray document; QString revision; QJsonObject error; };
QString cssFamily(const QString &family) {
    QString result="\"";
    // Font preferences are data, including when embedded in a raw-text style
    // element. Hex escapes cannot close a CSS string or an HTML style tag.
    for(const auto character:family.left(512)) {
        const auto value=character.unicode();
        if(value<32||value==127||character=='"'||character=='\\'||character=='<'||character=='>'||character=='&')
            result+='\\'+QString::number(value?value:0xfffd,16)+' ';
        else result+=character;
    }
    return result+'"';
}
QString cssFontSize(const QFont &font,int dpi) {
    const auto pixels=font.pixelSize()>0?double(font.pixelSize()):font.pointSizeF()*dpi/72.0;
    return QString::number(qBound(1.0,pixels>0?pixels:14.0,256.0),'f',2)+"px";
}
QThreadPool *htmlWorkers() {
    static const auto pool=[] {auto result=std::make_unique<QThreadPool>();result->setMaxThreadCount(4);return result;}();
    return pool.get();
}
PreparedHtml prepareHtml(const QString &html,const QHash<QString,QUrl> &images,const QString &appearance) {
    PreparedHtml result;QString safe,reason;
    if(!HtmlPolicy::sanitize(html,images,&safe,&reason)){result.error=rpcError(-32602,reason);return result;}
    QFile library(":/qtwebchannel/qwebchannel.js");
    if(!library.open(QIODevice::ReadOnly)){result.error=rpcError(-32603,"WebChannel bootstrap unavailable");return result;}
    result.revision=token();
    const auto csp="default-src 'none'; img-src 'self'; style-src 'nonce-"+result.revision+"'; script-src 'nonce-"+result.revision+"'; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none';";
    const auto bootstrap=QString::fromUtf8(library.readAll())+QStringLiteral("\nnew QWebChannel(qt.webChannelTransport, channel=>{window.notepadnext=Object.freeze({postMessage: message=>channel.objects.panelBridge.postMessage('%1',message)});});").arg(result.revision);
    result.document=QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"/><meta http-equiv=\"Content-Security-Policy\" content=\"%1\"/><style nonce=\"%2\">body{line-height:1.65;margin:24px;overflow-wrap:anywhere}body>p,body>ul,body>ol,body>pre,body>blockquote,body>table{content-visibility:auto;contain-intrinsic-size:auto 80px}img{max-width:100%;height:auto}pre{padding:14px;overflow:auto}blockquote{border-left:3px solid;padding-left:16px}table{border-collapse:collapse}td,th{border:1px solid;padding:6px}</style><style id=\"nnp-host-appearance\" nonce=\"%2\">%5</style></head><body>%3<script nonce=\"%2\">%4</script></body></html>")
        .arg(QString(csp).toHtmlEscaped(),result.revision,safe,bootstrap,appearance).toUtf8();
    if(result.document.size()>8*1024*1024){result.document={};result.error=rpcError(1008,"ContentTooLarge");}
    return result;
}
class BridgeObject : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    std::function<void(const QString &,const QJsonValue &)> receive;
    std::function<void(const QString &,bool,const QJsonValue &)> state;
public slots:
    void postMessage(const QString &revision,const QVariant &message) { if(receive) receive(revision,QJsonValue::fromVariant(message)); }
    void commitState(const QString &revision,bool hasState,const QVariant &value) { if(state) state(revision,hasState,QJsonValue::fromVariant(value)); }
};
class Dock : public QDockWidget {
public:
    using QDockWidget::QDockWidget;
    std::function<void()> closing;
protected:
    void closeEvent(QCloseEvent *event) override {
        QDockWidget::closeEvent(event);
        // Disposal clears the member callback. Keep its captures alive until
        // the callback (including the panel ID used for its event) returns.
        const auto callback=closing;
        if(event->isAccepted() && callback) callback();
    }
};
class Page : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
    QUrl allowed;
    QString servicePrefix;
    bool owns(const QUrl &url) const {
        auto target=url;target.setFragment({});
        return servicePrefix.isEmpty()?target==allowed:target.toString(QUrl::FullyEncoded).startsWith(servicePrefix);
    }
    std::function<void(const QUrl &)> external;
protected:
    bool acceptNavigationRequest(const QUrl &url,NavigationType type,bool main) override {
        if(main && owns(url)) return true;
        if(main && type==NavigationTypeLinkClicked && (url.scheme()=="https" || url.scheme()=="http") && external) external(url);
        return false;
    }
    QWebEnginePage *createWindow(WebWindowType) override { return nullptr; }
    QStringList chooseFiles(FileSelectionMode,const QStringList &,const QStringList &) override { return {}; }
};
struct Resource { QByteArray bytes,mime; QString panelId; QByteArray csp; };
class Handler : public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    QString owner;
    QHash<QString,Resource> resources;
    std::function<void(const QString &)> resolve;
    void requestStarted(QWebEngineUrlRequestJob *request) override {
        const auto url=request->requestUrl();
        if(request->requestMethod()!="GET" || url.scheme()!="nnp-plugin" || url.host()!=owner || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty() || url.port()!=-1) { request->fail(QWebEngineUrlRequestJob::RequestDenied); return; }
        if(!resources.contains(url.path())&&resolve)resolve(url.path());
        if(!resources.contains(url.path())) {request->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
        const auto &resource=resources[url.path()];
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        if(!resource.csp.isEmpty())request->setAdditionalResponseHeaders({{"Content-Security-Policy",resource.csp},{"X-Content-Type-Options","nosniff"}});
#endif
        auto *buffer=new QBuffer(request); buffer->setData(resource.bytes); buffer->open(QIODevice::ReadOnly); request->reply(resource.mime,buffer);
    }
};
class Interceptor : public QWebEngineUrlRequestInterceptor {
public:
    Interceptor(const QString &owner,QObject *parent) : QWebEngineUrlRequestInterceptor(parent),owner(owner) {}
    QMutex mutex;
    QHash<QString,int> services;
    bool suspended=false;
    void interceptRequest(QWebEngineUrlRequestInfo &request) override {
        const QMutexLocker lock(&mutex);
        if(suspended){request.block(true);return;}
        const auto url=request.requestUrl(),first=request.firstPartyUrl();
        if(url.scheme()=="http") {
            const auto target=url.toString(QUrl::FullyEncoded),source=first.toString(QUrl::FullyEncoded);
            bool permitted=false;
            for(auto it=services.cbegin();it!=services.cend();++it)if(target.startsWith(it.key())&&(first.isEmpty()||source.startsWith(it.key())))permitted=true;
            request.block(!permitted);return;
        }
        if(first.scheme()=="http"){request.block(true);return;}
        const bool foreignPanel=first.scheme()=="nnp-plugin"&&first.host()==owner&&first.path().section('/',1,1)!=url.path().section('/',1,1);
        request.block(url.scheme()!="nnp-plugin" || url.host()!=owner || url.port()!=-1 || !url.userInfo().isEmpty() || foreignPanel);
    }
private:
    const QString owner;
};
}
struct WebviewService::Private {
    struct Panel {
        quint64 order=0;
        QString restoreId,title;
        QJsonObject browserState;
        QString sourceDocument;
        QHash<QString,QJsonObject> documentStates;
        quint64 sourceSequence=0;
        bool sourceNeedsReload=false,sourceAnnounced=false;
        QPointer<QWidget> container;
        bool claimed=false,uiRestored=false,preservedPlacement=false;
        QPointer<QWidget> contextBar;
        QPointer<QLabel> documentLabel,contextStatus,footer;
        QJsonObject renderContext;
        QPointer<QFutureWatcher<PreparedHtml>> pendingHtml;
        quint64 htmlSequence=0;
        QPointer<Dock> dock; QPointer<Page> page; QMetaObject::Connection focusConnection; QString viewType,revision; QHash<QString,QUrl> images; QUrl url; bool loaded=false; QHash<QByteArray,QString> imageCache; bool requestedVisible=false,preserveFocus=true; bool compatible=false,enableScripts=false,service=false; QStringList roots; QSet<QString> assets; QJsonValue state=QJsonValue(QJsonValue::Undefined); bool visible=false,active=false; QList<QJsonValue> outgoing; qsizetype outgoingBytes=0; };
    QString pluginId,root,owner;
    QPointer<QMainWindow> window;
    QPointer<PluginDocuments::DocumentBridge> documents;
    QWebEngineProfile *profile;
    Handler *handler;
    Interceptor *interceptor;
    QHash<QString,Panel> panels;
    QJsonObject previewPolicy;
    qint64 imageBytes=0;
    bool published=true;
    bool appearancePending=false;
    bool suspended=false;
    quint64 nextPanel=0;
    QPointer<WebviewService> restoreSource;
};
void registerScheme() {
    QWebEngineUrlScheme scheme("nnp-plugin"); scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme); QWebEngineUrlScheme::registerScheme(scheme);
}
void prepareWindow(QMainWindow *window) {
    if(!window||window->findChild<QQuickWidget*>("pluginCompositionAnchor",Qt::FindDirectChildrenOnly))return;
    // QWebEngineView embeds a QQuickWidget. Adding the first texture widget
    // after show() recreates the native top-level surface on Qt 6 (visible on
    // macOS as the editor disappearing). Keep a host-owned empty anchor for
    // the window lifetime, independent of plugin activation and disposal.
    auto *anchor=new QQuickWidget(window);
    anchor->setObjectName("pluginCompositionAnchor");
    anchor->setFixedSize(0,0);anchor->setFocusPolicy(Qt::NoFocus);
    anchor->setAttribute(Qt::WA_TransparentForMouseEvents);anchor->hide();
}
WebviewService::WebviewService(const QString &id,const QString &root,QMainWindow *window,PluginDocuments::DocumentBridge *documents,QObject *parent,const QString &owner)
    : QObject(parent),d(std::make_unique<Private>()) {
    initCompatBridge();d->pluginId=id; d->root=root; d->window=window; d->documents=documents; d->owner=owner.isEmpty()?token():owner;
    d->profile=new QWebEngineProfile(this); d->profile->setHttpCacheType(QWebEngineProfile::NoCache); d->profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    d->handler=new Handler(d->profile); d->handler->owner=d->owner;
    d->handler->resolve=[this](const QString &path){const auto id=path.section('/',1,1),prefix="/"+id+"/asset/";if(!path.startsWith(prefix))return;QJsonObject error;mapAsset(id,path.mid(prefix.size()),&error);};
    d->profile->installUrlSchemeHandler("nnp-plugin",d->handler); d->interceptor=new Interceptor(d->owner,d->profile);d->profile->setUrlRequestInterceptor(d->interceptor);
    connect(d->profile,&QWebEngineProfile::downloadRequested,this,[](auto *request) { request->cancel(); });
    if(window)window->installEventFilter(this);
    if(documents) {
        connect(documents,&PluginDocuments::DocumentBridge::appearanceChanged,this,&WebviewService::refreshAppearance);
        connect(documents,&PluginDocuments::DocumentBridge::event,this,[this](const QString &type){
            if(type=="activeChanged")refreshAppearance();
            if(!d->previewPolicy.isEmpty())QTimer::singleShot(0,this,&WebviewService::refreshPreviewSource);
            for(auto it=d->panels.cbegin();it!=d->panels.cend();++it)refreshPanelContext(it.key());
        });
    }
}
WebviewService::~WebviewService() { clear(); delete d->profile; }
void WebviewService::configurePreview(const QJsonObject &policy) {d->previewPolicy=policy;}
void WebviewService::reloadSource(const QString &id,const QJsonObject &source,std::function<void(const QJsonObject &)> completed) {
    if(!d->panels.contains(id)||!d->panels[id].service||d->previewPolicy.isEmpty()||d->suspended){completed(rpcError(1004,"Source preview is closed or suspended"));return;}
    const auto valid=[this,source]{
        const auto current=PluginRuntime::previewSource(d->previewPolicy,d->documents);
        if(current.isNull())return source["documentId"].isNull()&&source["version"].isNull();
        return source["documentId"]==current.toObject()["documentId"]&&source["version"]==current.toObject()["version"];
    };
    if(!valid()){completed(rpcError(1001,"Source tab or document version changed"));return;}
    const QPointer<WebviewService> safe=this;const auto sequence=d->panels[id].sourceSequence;
    const auto finish=[safe,id,valid,sequence,completed](const QVariant &value){
        if(!safe||!safe->d->panels.contains(id)||safe->d->suspended){completed(rpcError(1004,"Source preview closed during reload"));return;}
        auto &panel=safe->d->panels[id];if(panel.sourceSequence!=sequence||!valid()){completed(rpcError(1001,"Source changed during reload"));return;}
        if(!panel.sourceNeedsReload&&value.isValid()) {
            const auto bytes=value.toString().toUtf8();panel.browserState=bytes.size()<=128*1024?QJsonDocument::fromJson(bytes).object():QJsonObject{};
            const auto state=panel.browserState["snapshot"].toObject();panel.state=state["hasState"].toBool()?state["state"]:QJsonValue(QJsonValue::Undefined);
        }
        if(panel.sourceNeedsReload){const auto state=panel.browserState["snapshot"].toObject();panel.state=state["hasState"].toBool()?state["state"]:QJsonValue(QJsonValue::Undefined);}
        panel.revision=token();
        QJsonObject error;if(!safe->installCompatibleBridge(id,&error)){completed(error);return;}
        panel.sourceNeedsReload=false;panel.loaded=false;panel.page->load(panel.url);completed({});
    };
    auto &panel=d->panels[id];
    if(panel.sourceNeedsReload||!panel.loaded){finish(QVariant());return;}
    panel.page->runJavaScript("JSON.stringify({scrollX,scrollY,snapshot:window[Symbol.for('NotepadNext.compatWebviewBootstrap')]?.snapshot?.()})",finish);
}
void WebviewService::refreshPreviewSource() {
    if(d->previewPolicy.isEmpty())return;
    // The source outlives neither its tab nor its current file type, even when
    // this service is preserving docks for an in-flight update.
    if(!PluginRuntime::hasPreviewDocuments(d->previewPolicy,d->documents)) {clear();return;}
    const auto source=PluginRuntime::previewSource(d->previewPolicy,d->documents);
    const auto selected=source.toObject()["documentId"].toString();
    const auto ids=d->panels.keys();
    for(const auto &id:ids) {
        if(!d->panels.contains(id))continue;
        auto &panel=d->panels[id];
        for(const auto &document:panel.documentStates.keys())
            if(!PluginRuntime::previewMatches(d->previewPolicy,d->documents->info(document)))panel.documentStates.remove(document);
        if(panel.dock) {
            if(selected.isEmpty())panel.dock->hide();
            else if(d->published&&panel.requestedVisible&&!d->suspended)panel.dock->show();
        }
        if(d->suspended)continue;
        if(panel.sourceDocument==selected) {++panel.sourceSequence;emit event(id,"sourceChanged",QJsonObject{{"document",source},{"hasState",!panel.state.isUndefined()},{"state",panel.state.isUndefined()?QJsonValue(QJsonValue::Null):panel.state}});continue;}
        const auto old=panel.sourceDocument;const auto sequence=++panel.sourceSequence;
        const QPointer<WebviewService> safe=this;
        const auto finish=[safe,id,selected,old,source,sequence](const QVariant &value) {
            if(!safe||!safe->d->panels.contains(id))return;
            auto &panel=safe->d->panels[id];if(panel.sourceSequence!=sequence||safe->d->suspended||PluginRuntime::previewSource(safe->d->previewPolicy,safe->d->documents).toObject()["documentId"].toString()!=selected)return;
            const auto bytes=value.toString().toUtf8();const auto snapshot=bytes.size()<=128*1024?QJsonDocument::fromJson(bytes).object():QJsonObject{};
            if(!old.isEmpty()&&PluginRuntime::previewMatches(safe->d->previewPolicy,safe->d->documents->info(old))) {
                // Bounded ephemeral state. It never becomes persistent plugin data.
                if(panel.documentStates.size()>=32&&!panel.documentStates.contains(old))panel.documentStates.erase(panel.documentStates.begin());
                panel.documentStates.insert(old,snapshot);
            }
            panel.sourceNeedsReload=true;panel.sourceDocument=selected;panel.browserState=panel.documentStates.value(selected);panel.uiRestored=false;
            const auto state=panel.browserState["snapshot"].toObject();panel.state=state["hasState"].toBool()?state["state"]:QJsonValue(QJsonValue::Undefined);
            // Service pages stay loaded. Their renderer receives the tab's state
            // explicitly; fragment/HTML pages receive it on their next load.
            emit safe->event(id,"sourceChanged",QJsonObject{{"document",source},{"hasState",!panel.state.isUndefined()},{"state",panel.state.isUndefined()?QJsonValue(QJsonValue::Null):panel.state}});
        };
        if(!panel.page||!panel.loaded){finish(QString());continue;}
        panel.page->runJavaScript(QStringLiteral(R"JS((()=>{const snapshot=window[Symbol.for('NotepadNext.compatWebviewBootstrap')]?.snapshot?.();const r={scrollX,scrollY,snapshot,fields:[]};for(const [i,n] of Array.from(document.querySelectorAll('input:not([type=password]):not([type=file]),textarea,select')).slice(0,128).entries()){if(n.value.length>4096)continue;r.fields.push({i,id:n.id,tag:n.tagName,type:n.type,value:n.value,checked:n.checked,selected:n.selectedIndex});if(new TextEncoder().encode(JSON.stringify(r)).length>120*1024){r.fields.pop();break;}}return JSON.stringify(r);})())JS"),finish);
    }
}
QString WebviewService::appearanceCss() const {
    const auto palette=d->window?d->window->palette():qApp->palette();
    const auto bodyFont=d->window?d->window->font():qApp->font();
    const auto codeFont=d->documents?d->documents->activeEditorFont():QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto dpi=d->window?d->window->logicalDpiY():96;
    return QStringLiteral("body{font-family:%1,sans-serif;font-size:%2;color:%3;background:%4;color-scheme:%5}pre{background:%6}code{font-family:%7,monospace;font-size:%8}a{color:%9}blockquote,td,th{border-color:%10}")
        .arg(cssFamily(bodyFont.family()),cssFontSize(bodyFont,dpi),palette.color(QPalette::Text).name(QColor::HexRgb),
             palette.color(QPalette::Base).name(QColor::HexRgb),palette.color(QPalette::Base).lightness()<128?"dark":"light",
             palette.color(QPalette::AlternateBase).name(QColor::HexRgb),cssFamily(codeFont.family()),cssFontSize(codeFont,dpi),
             palette.color(QPalette::Link).name(QColor::HexRgb),palette.color(QPalette::Mid).name(QColor::HexRgb));
}
bool WebviewService::eventFilter(QObject *watched,QEvent *event) {
    if(watched==d->window&&(event->type()==QEvent::PaletteChange||event->type()==QEvent::FontChange||
            event->type()==QEvent::ApplicationPaletteChange||event->type()==QEvent::ApplicationFontChange))refreshAppearance();
    return QObject::eventFilter(watched,event);
}
void WebviewService::refreshAppearance() {
    if(d->suspended||d->appearancePending)return;
    d->appearancePending=true;
    QTimer::singleShot(0,this,[this]{
        d->appearancePending=false;
        for(auto it=d->panels.cbegin();it!=d->panels.cend();++it){applyAppearance(it.key());refreshPanelContext(it.key());}
    });
}
void WebviewService::refreshPanelContext(const QString &id) {
    if(d->suspended||!d->panels.contains(id))return;auto &panel=d->panels[id];
    if(!panel.contextBar)return;
    const auto documentId=panel.renderContext["documentId"].toString();
    const auto document=d->documents?d->documents->info(documentId):QJsonObject{};
    const bool bound=!document.isEmpty();panel.contextBar->setVisible(bound);panel.footer->setVisible(bound);
    if(!bound)return;
    const QUrl uri(document["uri"].toString());
    const auto name=uri.isLocalFile()?QFileInfo(uri.toLocalFile()).fileName():tr("Untitled");
    panel.documentLabel->setText(name);panel.documentLabel->setToolTip(uri.toDisplayString());
    const auto active=d->documents->active().toObject();
    const bool current=panel.loaded&&active["documentId"]==panel.renderContext["documentId"]&&document["version"]==panel.renderContext["version"];
    panel.contextStatus->setText(current?tr("● Live"):tr("Updating…"));
    panel.footer->setText(tr("Following the active document · unsaved changes included"));
    const auto palette=d->window?d->window->palette():qApp->palette();const bool dark=palette.color(QPalette::Base).lightness()<128;
    const auto secondary=dark?"#b6b6bd":"#626267",border=dark?"#4b4b50":"#cbcbcb";
    panel.contextBar->setStyleSheet(QString("QWidget#pluginDocumentContext{background:%1;border-bottom:1px solid %2}QLabel{border:0;color:%3;font-size:13px}")
        .arg(palette.color(QPalette::Base).name(),border,palette.color(QPalette::Text).name()));
    panel.contextStatus->setStyleSheet(QString("border:0;color:%1;font-size:11px").arg(current?(dark?"#85d0a1":"#22643a"):secondary));
    panel.footer->setStyleSheet(QString("background:%1;color:%2;font-size:11px;padding:0 16px;border-top:1px solid %3").arg(dark?"#26262a":"#f7f7f7",secondary,border));
}
void WebviewService::applyAppearance(const QString &id) {
    const auto it=d->panels.constFind(id);
    if(it==d->panels.cend()||it->compatible||!it->loaded||!it->page||it->page->url()!=it->url)return;
    const auto literal=[](const QString &value){const auto json=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return QString::fromUtf8(json.mid(1,json.size()-2));};
    const auto code=QStringLiteral("(()=>{const s=document.getElementById('nnp-host-appearance'),css=%1;if(s&&s.nonce===%2&&s.textContent!==css)s.textContent=css;})()")
        .arg(literal(appearanceCss()),literal(it->revision));
    it->page->runJavaScript(code,QWebEngineScript::ApplicationWorld);
}
QJsonObject WebviewService::createCompatible(const QString &id,const QString &viewType,const QString &title,bool enableScripts,const QStringList &roots,QJsonObject *error,const QString &restorationKey) {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    *error=rpcError(1005,"Compatible Webviews require Qt 6.6 response-header support");return {};
#else
    if(!QRegularExpression("^[a-f0-9]{32}$").match(id).hasMatch()||roots.size()>16){*error=rpcError(-32602,"Invalid compatible panel declaration");return {};}
    QStringList canonical;const auto base=QFileInfo(d->root).canonicalFilePath();
    for(const auto &relative:roots){
        if(relative.isEmpty()||QDir::isAbsolutePath(relative)||relative.contains('\\')||relative.contains(':')){*error=rpcError(-32602,"Resource roots must be package-relative directories");return {};}
        for(const auto &part:relative.split('/'))if(part.isEmpty()||part==".."||part=="."){*error=rpcError(-32602,"Resource root traversal");return {};}
        const auto directory=QFileInfo(QDir(d->root).filePath(relative)).canonicalFilePath();
        if(directory.isEmpty()||!directory.startsWith(base+'/')||!QFileInfo(directory).isDir()){*error=rpcError(1003,"Resource root escapes package or is not a directory");return {};}
        if(!canonical.contains(directory))canonical.append(directory);
    }
    auto result=createPanel(id,viewType,title,"right",true,error,restorationKey);if(!error->isEmpty())return {};
    d->panels[id].enableScripts=enableScripts;d->panels[id].roots=canonical;return result;
#endif
}
QString WebviewService::mapAsset(const QString &id,const QString &relative,QJsonObject *error) {
    *error={};if(!d->panels.contains(id)||!d->panels[id].compatible){*error=rpcError(1004,"Compatible panel closed");return {};}
    auto &panel=d->panels[id];
    if(relative.isEmpty()||relative.size()>512||QDir::isAbsolutePath(relative)||relative.contains('\\')||relative.contains(':')||relative.contains(QChar::Null)){*error=rpcError(-32602,"Expected a package-relative asset path");return {};}
    for(const auto &part:relative.split('/'))if(part.isEmpty()||part=="."||part==".."){*error=rpcError(-32602,"Asset path traversal");return {};}
    QUrl url;url.setScheme("nnp-plugin");url.setHost(d->owner);url.setPath("/"+id+"/asset/"+relative);
    if(panel.assets.contains(relative))return url.toString(QUrl::FullyEncoded);
    const auto file=QFileInfo(QDir(d->root).filePath(relative)).canonicalFilePath();bool allowed=false;for(const auto &root:panel.roots)if(file.startsWith(root+'/'))allowed=true;
    if(!allowed||!QFileInfo(file).isFile()){*error=rpcError(1003,"Asset is missing or outside localResourceRoots");return {};}
    QByteArray bytes,mime;const auto suffix=QFileInfo(file).suffix().toLower();
    if(suffix=="js"||suffix=="css"){
        QFile input(file);if(!input.open(QIODevice::ReadOnly)||input.size()>2*1024*1024){*error=rpcError(1008,"Text asset exceeds 2 MiB or cannot be read");return {};}
        bytes=input.read(2*1024*1024+1);QStringDecoder utf8(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);const QString decoded=utf8.decode(bytes);Q_UNUSED(decoded);
        if(bytes.size()>2*1024*1024||utf8.hasError()){*error=rpcError(1009,"Text assets require bounded UTF-8");return {};}
        mime=suffix=="js"?"text/javascript":"text/css";
    }else{QString reason;if(!HtmlPolicy::readImage(d->root,relative,&bytes,&mime,&reason)){*error=rpcError(-32602,reason);return {};}}
    if(panel.assets.size()>=64){*error=rpcError(1007,"Compatible asset limit reached");return {};}
    if(d->imageBytes+bytes.size()>32*1024*1024){*error=rpcError(1008,"Session resource budget exceeded");return {};}
    panel.assets.insert(relative);d->imageBytes+=bytes.size();d->handler->resources.insert(url.path(),{bytes,mime,id});return url.toString(QUrl::FullyEncoded);
}
bool WebviewService::setTitle(const QString &id,const QString &title,QJsonObject *error) {
    *error={};if(!d->panels.contains(id)||!d->panels[id].compatible||!d->panels[id].dock){*error=rpcError(1004,"Compatible panel closed");return false;}
    if(title.isEmpty()||title.toUcs4().size()>128){*error=rpcError(-32602,"Invalid panel title");return false;}auto &panel=d->panels[id];panel.title=title;if(panel.restoreId.isEmpty())panel.dock->setWindowTitle(title);return true;
}
bool WebviewService::setDocument(const QString &id,const QString &html,QJsonObject *error) {
    *error={};if(!d->panels.contains(id)||!d->panels[id].compatible||!d->panels[id].page){*error=rpcError(1004,"Compatible panel closed");return false;}
    if(d->suspended||d->panels[id].service){*error=rpcError(1004,"Service pages cannot be replaced with setDocument");return false;}
    const auto bytes=html.toUtf8();if(bytes.size()>HtmlPolicy::MaxHtmlBytes){*error=rpcError(1008,"HTML exceeds 6 MiB");return false;}
    auto &panel=d->panels[id];QStringList nonces;auto matches=QRegularExpression("\\bnonce\\s*=\\s*['\"]([A-Za-z0-9+/_=-]{1,128})['\"]",QRegularExpression::CaseInsensitiveOption).globalMatch(html);
    while(matches.hasNext()){const auto nonce=matches.next().captured(1);if(!nonces.contains(nonce))nonces.append(nonce);if(nonces.size()>32){*error=rpcError(1008,"Too many CSP nonces");return false;}}
    QString script=panel.enableScripts?QStringLiteral("'self'"):QStringLiteral("'none'"),style=QStringLiteral("'self'");
    for(const auto &nonce:nonces){style+=" 'nonce-"+nonce+"'";if(panel.enableScripts)script+=" 'nonce-"+nonce+"'";}
    const auto csp="default-src 'none'; img-src 'self'; style-src "+style+"; script-src "+script+"; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none'; object-src 'none';";
    if(!panel.url.isEmpty())d->handler->resources.remove(panel.url.path());panel.outgoing.clear();panel.outgoingBytes=0;panel.revision=token();panel.loaded=false;panel.url=QUrl("nnp-plugin://"+d->owner+"/"+id+"/page-"+panel.revision+".html");panel.page->allowed=panel.url;
    if(!installCompatibleBridge(id,error))return false;
    d->handler->resources.insert(panel.url.path(),{bytes,"text/html",id,csp.toUtf8()});if(d->published||!panel.restoreId.isEmpty())panel.page->load(panel.url);return true;
}
bool WebviewService::installCompatibleBridge(const QString &id,QJsonObject *error) {
    auto &panel=d->panels[id];
    QFile library(":/qtwebchannel/qwebchannel.js"),bootstrap(":/plugin-webviews/compat-bootstrap.js");
    if(!library.open(QIODevice::ReadOnly)||!bootstrap.open(QIODevice::ReadOnly)){*error=rpcError(-32603,"Compatible bridge unavailable");return false;}
    const auto literal=[](const QJsonValue &value){const auto json=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return QString::fromUtf8(json.mid(1,json.size()-2));};
    auto code=QString::fromUtf8(bootstrap.readAll());code.replace("__NNP_REVISION__",literal(panel.revision));
    code.replace("__NNP_PREFIX__",literal(panel.service?panel.page->servicePrefix:panel.url.toString(QUrl::FullyEncoded)));
    code.replace("__NNP_STATE__",panel.state.isUndefined()?"undefined":QString("JSON.parse(")+literal(QString::fromUtf8(QJsonDocument(QJsonArray{panel.state}).toJson(QJsonDocument::Compact)))+")[0]");
    QWebEngineScript injected;injected.setName("nnp-compatible-bridge");injected.setInjectionPoint(QWebEngineScript::DocumentCreation);injected.setWorldId(QWebEngineScript::MainWorld);injected.setRunsOnSubFrames(false);injected.setSourceCode(QString::fromUtf8(library.readAll())+'\n'+code);
    panel.page->scripts().clear();panel.page->scripts().insert(injected);return true;
}
void WebviewService::createService(const QString &viewType,const QString &title,const QUrl &endpoint,const QString &nonce,const QString &sessionId,const QString &restorationKey,std::function<void(const QJsonObject &,const QJsonObject &)> completed) {
    const auto address=endpoint.toString(QUrl::FullyEncoded);
    if(!QRegularExpression("^http://127\\.0\\.0\\.1:[0-9]{4,5}/nnp/[a-f0-9]{32}/index\\.html$").match(address).hasMatch()||endpoint.port()<1024||endpoint.port()>65535||!QRegularExpression("^[a-f0-9]{32}$").match(nonce).hasMatch()) {
        completed({},rpcError(-32602,"Service URL must be an SDK-owned IPv4 loopback endpoint"));return;
    }
    const auto prefix=address.left(address.size()-10);
    auto *network=new QNetworkAccessManager(this);network->setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(QUrl(prefix+"__nnp_probe"));request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(2000);
    auto *reply=network->get(request);reply->setReadBufferSize(4097);auto *timeout=new QTimer(reply);timeout->setSingleShot(true);timeout->start(2000);
    connect(timeout,&QTimer::timeout,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::readyRead,reply,[reply]{if(reply->bytesAvailable()>4096)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,network,viewType,title,endpoint,nonce,sessionId,restorationKey,prefix,completed]{
        const auto bytes=reply->readAll();const auto proof=QJsonDocument::fromJson(bytes).object();
        const bool valid=reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&bytes.size()<=4096&&proof["sessionId"]==sessionId&&proof["nonce"]==nonce;
        network->deleteLater();
        if(d->suspended||!valid){completed({},rpcError(1003,"Local service ownership verification failed"));return;}
        QJsonObject error;const auto result=createCompatible(token(),viewType,title,true,{},&error,restorationKey);
        if(!error.isEmpty()){completed({},error);return;}
        const auto id=result["panelId"].toString();auto &panel=d->panels[id];panel.service=true;panel.sourceNeedsReload=!d->previewPolicy.isEmpty();panel.revision=token();panel.url=endpoint;panel.page->allowed=endpoint;panel.page->servicePrefix=prefix;
        {const QMutexLocker lock(&d->interceptor->mutex);++d->interceptor->services[prefix];}
        if(!installCompatibleBridge(id,&error)){dispose(id);completed({},error);return;}
        if(d->published||!panel.restoreId.isEmpty())panel.page->load(endpoint);completed(result,{});
    });
}
QJsonObject WebviewService::create(const QString &viewType,const QString &title,const QString &position,QJsonObject *error,const QString &restorationKey) {
    for(auto it=d->panels.cbegin();it!=d->panels.cend();++it) if(!it->compatible&&it->viewType==viewType) { *error={};return {{"panelId",it.key()}}; }
    return createPanel(token(),viewType,title,position,false,error,restorationKey);
}
QJsonObject WebviewService::createPanel(const QString &id,const QString &viewType,const QString &title,const QString &position,bool compatible,QJsonObject *error,const QString &restorationKey) {
    *error={};
    if(!d->previewPolicy.isEmpty()) {
        if(viewType!=d->previewPolicy["viewType"].toString()){*error=rpcError(1003,"Panel view type does not match the preview declaration");return {};}
        if(!PluginRuntime::hasPreviewDocuments(d->previewPolicy,d->documents)){*error=rpcError(1002,"No supported source tab remains");return {};}
        if(!d->panels.isEmpty()){*error=rpcError(1007,"Editor previews own one shared dock per window");return {};}
    }
    if(!d->window || viewType.isEmpty() || viewType.size()>128 || title.isEmpty() || title.toUcs4().size()>128 || (position!="right" && position!="bottom")) { *error=rpcError(-32602,"Invalid panel declaration"); return {}; }
    if(d->panels.contains(id)) { *error=rpcError(-32602,"Duplicate panel ID");return {}; }
    if(d->panels.size()>=8) { *error=rpcError(1007,"Panel limit reached"); return {}; }
    QString restoreId;
    if(!restorationKey.isEmpty()){
        if(!d->restoreSource||!d->restoreSource->d->panels.contains(restorationKey)){*error=rpcError(1004,"Unknown restoration key");return {};}
        const auto &old=d->restoreSource->d->panels[restorationKey];
        if(old.claimed||!old.dock||old.viewType!=viewType||old.compatible!=compatible){*error=rpcError(1004,"Restoration key was closed or belongs to another view type");return {};}
        restoreId=restorationKey;
    }
    if(restoreId.isEmpty()&&d->restoreSource){
        quint64 first=std::numeric_limits<quint64>::max();
        for(auto it=d->restoreSource->d->panels.cbegin();it!=d->restoreSource->d->panels.cend();++it)
            if(!it->claimed&&it->dock&&it->viewType==viewType&&it->compatible==compatible&&it->order<first){restoreId=it.key();first=it->order;}
    }
    auto *dock=restoreId.isEmpty()?new Dock(title,d->window):d->restoreSource->d->panels[restoreId].dock.data();
    if(restoreId.isEmpty())dock->setObjectName("plugin:"+d->pluginId+":"+viewType+ (compatible?":"+id:QString()));
    auto *container=new QWidget(dock);auto *layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto *contextBar=new QWidget(container);contextBar->setObjectName("pluginDocumentContext");contextBar->setFixedHeight(40);
    auto *contextLayout=new QHBoxLayout(contextBar);contextLayout->setContentsMargins(24,0,24,0);
    auto *documentLabel=new QLabel(contextBar),*contextStatus=new QLabel(contextBar);documentLabel->setTextFormat(Qt::PlainText);contextStatus->setTextFormat(Qt::PlainText);
    contextLayout->addWidget(documentLabel,1);contextLayout->addWidget(contextStatus);layout->addWidget(contextBar);contextBar->hide();
    auto *view=new QWebEngineView(container);auto *page=new Page(d->profile,view);view->setPage(page);layout->addWidget(view,1);
    auto *footer=new QLabel(container);footer->setTextFormat(Qt::PlainText);footer->setFixedHeight(28);footer->hide();layout->addWidget(footer);
    container->setFocusProxy(view);container->setFocusPolicy(Qt::StrongFocus);
    if(restoreId.isEmpty()){dock->setWidget(container);dock->hide();}else {container->resize(dock->widget()->size());layout->activate();container->hide();}
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,false); page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,false); page->settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard,false);
    page->settings()->setAttribute(QWebEngineSettings::JavascriptCanPaste,false); page->settings()->setAttribute(QWebEngineSettings::DnsPrefetchEnabled,false);
    page->settings()->setAttribute(QWebEngineSettings::PluginsEnabled,false); page->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,false);
    page->external=[this](const QUrl &url) { emit externalLinkRequested(url); };
    auto *channel=new QWebChannel(page); auto *bridge=new BridgeObject(channel); channel->registerObject("panelBridge",bridge); page->setWebChannel(channel);
    bridge->receive=[this,id](const QString &revision,const QJsonValue &message) {
        if(d->suspended)return;
        if(!d->panels.contains(id)) return;
        const auto &panel=d->panels[id];
        const auto bytes=QJsonDocument(QJsonArray{message}).toJson(QJsonDocument::Compact);
        if((panel.loaded||panel.compatible) && panel.page && panel.page->owns(panel.page->url()) && revision==panel.revision && bytes.size()<=64*1024) emit event(id,"message",message);
    };
    bridge->state=[this,id](const QString &revision,bool hasState,const QJsonValue &state){if(d->suspended||!d->panels.contains(id))return;auto &panel=d->panels[id];const auto bytes=QJsonDocument(QJsonArray{state}).toJson(QJsonDocument::Compact);if(panel.compatible&&panel.page&&panel.page->owns(panel.page->url())&&revision==panel.revision&&bytes.size()<=64*1024)panel.state=hasState?state:QJsonValue(QJsonValue::Undefined);};
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    connect(page,&QWebEnginePage::permissionRequested,page,[](QWebEnginePermission permission) { permission.deny(); });
#else
    connect(page,&QWebEnginePage::featurePermissionRequested,page,[page](const QUrl &origin,QWebEnginePage::Feature feature) { page->setFeaturePermission(origin,feature,QWebEnginePage::PermissionDeniedByUser); });
#endif
    connect(page,&QWebEnginePage::loadFinished,this,[this,id](bool ok) {
        if(!d->panels.contains(id)) return;
        auto &panel=d->panels[id]; panel.loaded=ok && panel.page && panel.page->owns(panel.page->url());
        if(panel.loaded&&!panel.compatible)applyAppearance(id);
        if(panel.loaded&&panel.compatible){const auto outgoing=panel.outgoing;panel.outgoing.clear();panel.outgoingBytes=0;for(const auto &message:outgoing){QJsonObject error;postMessage(id,message,&error);}}
        if(panel.loaded&&(!panel.restoreId.isEmpty()||!d->previewPolicy.isEmpty()))restoreBrowserState(id);
        if(!ok) emit event(id,"loadFailed",QStringLiteral("Panel page could not load."));
        refreshPanelContext(id);
    });
    connect(page,&QWebEnginePage::renderProcessTerminated,this,[this,id] { if(d->panels.contains(id)) { d->panels[id].loaded=false; emit event(id,"loadFailed",QStringLiteral("Panel renderer stopped.")); } });
    dock->closing=[this,id] {
        if(d->panels.contains(id)&&d->restoreSource&&!d->panels[id].restoreId.isEmpty())d->restoreSource->dispose(d->panels[id].restoreId);
        dispose(id);
    };
    Private::Panel panel;panel.dock=dock;panel.page=page;panel.container=container;panel.viewType=viewType;panel.compatible=compatible;panel.order=++d->nextPanel;panel.title=title;
    panel.restoreId=restoreId;
    panel.preservedPlacement=!restoreId.isEmpty();
    if(!restoreId.isEmpty()){
        auto &old=d->restoreSource->d->panels[restoreId];old.claimed=true;
        panel.sourceDocument=old.sourceDocument;panel.documentStates=old.documentStates;
        panel.state=old.state;panel.browserState=old.browserState;panel.requestedVisible=old.requestedVisible;panel.preserveFocus=true;
    }
    if(!d->previewPolicy.isEmpty()) {
        const auto source=PluginRuntime::previewSource(d->previewPolicy,d->documents).toObject();const auto selected=source["documentId"].toString();
        if(!panel.sourceDocument.isEmpty()&&panel.sourceDocument!=selected)panel.documentStates.insert(panel.sourceDocument,panel.browserState);
        if(panel.sourceDocument!=selected){panel.browserState=panel.documentStates.value(selected);const auto snapshot=panel.browserState["snapshot"].toObject();panel.state=snapshot["hasState"].toBool()?snapshot["state"]:QJsonValue(QJsonValue::Undefined);}
        panel.sourceDocument=selected;
    }
    panel.contextBar=contextBar;panel.documentLabel=documentLabel;panel.contextStatus=contextStatus;panel.footer=footer;
    d->panels.insert(id,panel);
    connect(dock,&QDockWidget::visibilityChanged,this,[this,id]{viewState(id);});
    // The dock survives dispose until its close event/deferred deletion ends.
    // Bind the capture to the service lifetime, and remove it on panel close.
    d->panels[id].focusConnection=connect(qApp,&QApplication::focusChanged,this,[this,id]{viewState(id);});
    // A dock created after restoreState can restore its saved placement.
    if(restoreId.isEmpty()){
        if(!d->window->restoreDockWidget(dock)) d->window->addDockWidget(position=="bottom" ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea,dock);
        dock->hide();
    }
    emit panelsChanged();return {{"panelId",id}};
}
void WebviewService::viewState(const QString &id) {
    if(!d->panels.contains(id))return;auto &panel=d->panels[id];if(!panel.compatible||!panel.dock)return;
    const bool visible=d->published&&panel.dock->isVisible();const auto *focus=qApp->focusWidget();const bool active=visible&&focus&&(focus==panel.dock||panel.dock->isAncestorOf(focus));
    if(visible==panel.visible&&active==panel.active)return;panel.visible=visible;panel.active=active;emit event(id,"viewState",QJsonObject{{"visible",visible},{"active",active},{"viewColumn",1}});
}
bool WebviewService::reveal(const QString &id,bool preserveFocus,QJsonObject *error) {
    *error={}; if(!d->panels.contains(id) || !d->panels[id].dock) { *error=rpcError(1004,"Panel closed"); return false; }
    d->panels[id].requestedVisible=true;d->panels[id].preserveFocus=preserveFocus;
    if(!d->previewPolicy.isEmpty()&&!d->panels[id].sourceAnnounced){d->panels[id].sourceAnnounced=true;QTimer::singleShot(0,this,[this]{refreshPreviewSource();});}
    if(!d->published||(!d->panels[id].restoreId.isEmpty()&&d->restoreSource))return true;
    if(!d->previewPolicy.isEmpty()&&PluginRuntime::previewSource(d->previewPolicy,d->documents).isNull())return true;
    auto *dock=d->panels[id].dock.data(); dock->show(); dock->raise(); if(!preserveFocus) dock->widget()->setFocus(); return true;
}
void WebviewService::setPublished(bool published) {
    d->published=published;
    for(auto it=d->panels.begin();it!=d->panels.end();++it)if(it->dock) {
        auto &panel=it.value();
        if(published&&!d->previewPolicy.isEmpty()&&PluginRuntime::previewSource(d->previewPolicy,d->documents).isNull()){panel.dock->hide();continue;}
        if(published&&panel.preservedPlacement){viewState(it.key());continue;}
        if(published&&panel.compatible&&panel.page&&!panel.url.isEmpty()&&!panel.page->owns(panel.page->url()))panel.page->load(panel.url);
        if(!published)panel.dock->hide();
        else if(panel.requestedVisible){panel.dock->show();panel.dock->raise();if(!panel.preserveFocus)panel.dock->widget()->setFocus();}
    }
}
QJsonObject WebviewService::renderContextError(const QString &id,const QJsonObject &context) const {
    if(d->suspended)return rpcError(1004,"Plugin panels are suspended for hot update");
    if(!d->panels.contains(id)||!d->panels[id].page||!d->documents)return rpcError(1004,"Panel closed");
    if(d->panels[id].compatible)return rpcError(1005,"Compatible panels require setDocument");
    if(context.size()!=2||!context.contains("documentId")||!context.contains("version"))return rpcError(-32602,"Invalid render context");
    const auto active=d->previewPolicy.isEmpty()?d->documents->active():PluginRuntime::previewSource(d->previewPolicy,d->documents);
    if(context["documentId"].isNull()) {
        if(!context["version"].isNull()||!active.isNull())return rpcError(1001,"VersionConflict");
    } else {
        const auto doc=d->documents->info(context["documentId"].toString());
        if(doc.isEmpty())return rpcError(1002,"DocumentClosed");
        if(active.toObject()["documentId"]!=context["documentId"]||doc["version"]!=context["version"])return rpcError(1001,"VersionConflict");
    }
    return {};
}
void WebviewService::publishHtml(const QString &id,const QByteArray &document,const QString &revision) {
    auto &panel=d->panels[id];
    if(!panel.url.isEmpty())d->handler->resources.remove(panel.url.path());
    panel.revision=revision;panel.loaded=false;panel.url=QUrl("nnp-plugin://"+d->owner+"/"+id+"/page-"+revision+".html");panel.page->allowed=panel.url;
    d->handler->resources.insert(panel.url.path(),{document,"text/html",id});panel.page->load(panel.url);
}
bool WebviewService::setHtml(const QString &id,const QString &html,const QJsonObject &context,QJsonObject *error) {
    *error=renderContextError(id,context);if(!error->isEmpty())return false;
    auto &panel=d->panels[id];++panel.htmlSequence;
    if(panel.pendingHtml)panel.pendingHtml->cancel();
    const auto prepared=prepareHtml(html,panel.images,appearanceCss());*error=prepared.error;
    if(!error->isEmpty())return false;
    panel.renderContext=context;publishHtml(id,prepared.document,prepared.revision);refreshPanelContext(id);return true;
}
void WebviewService::setHtmlAsync(const QString &id,const QString &html,const QJsonObject &context,std::function<void(const QJsonObject &)> completed) {
    const auto error=renderContextError(id,context);
    if(!error.isEmpty()){completed(error);return;}
    auto &panel=d->panels[id];
    if(panel.pendingHtml){completed(rpcError(1007,"Panel HTML preparation is busy"));return;}
    if(html.size()>HtmlPolicy::MaxHtmlBytes){completed(rpcError(-32602,"Panel content exceeds 6 MiB."));return;}
    const auto sequence=++panel.htmlSequence;
    auto *watcher=new QFutureWatcher<PreparedHtml>(this);panel.pendingHtml=watcher;
    connect(watcher,&QFutureWatcher<PreparedHtml>::finished,this,[this,id,context,sequence,watcher,completed=std::move(completed)] {
        watcher->deleteLater();
        QJsonObject error=renderContextError(id,context);
        if(d->panels.contains(id)) {
            auto &panel=d->panels[id];
            if(panel.pendingHtml==watcher)panel.pendingHtml=nullptr;
            if(error.isEmpty()&&(panel.htmlSequence!=sequence||watcher->isCanceled()))error=rpcError(1001,"VersionConflict");
        }
        if(error.isEmpty()) {
            const auto prepared=watcher->result();error=prepared.error;
            if(error.isEmpty()){d->panels[id].renderContext=context;publishHtml(id,prepared.document,prepared.revision);refreshPanelContext(id);}
        }
        completed(error);
    });
    auto promise=std::make_shared<QPromise<PreparedHtml>>();promise->start();watcher->setFuture(promise->future());
    // Tasks contain value copies only: destroying a service never leaves a
    // worker holding a QObject, document bridge, page or RPC callback.
    htmlWorkers()->start([promise,html,images=panel.images,appearance=appearanceCss()] {
        if(!promise->isCanceled()) {
            auto result=prepareHtml(html,images,appearance);
            if(!promise->isCanceled())promise->addResult(std::move(result));
        }
        promise->finish();
    });
}

QString WebviewService::mapResource(const QString &id,const QJsonObject &resource,QJsonObject *error) {
    *error={}; if(!d->panels.contains(id)) { *error=rpcError(1004,"Panel closed"); return {}; }
    auto &panel=d->panels[id];
    if(panel.compatible){*error=rpcError(1005,"Compatible panels require mapAsset");return {};}
    QString root=d->root;
    if(resource["kind"]=="documentImage") {
        const auto doc=d->documents ? d->documents->info(resource["documentId"].toString()) : QJsonObject{};
        if(doc.isEmpty()) { *error=rpcError(1002,"DocumentClosed"); return {}; }
        const auto active=d->documents->active().toObject(); if(active["documentId"]!=resource["documentId"]) { *error=rpcError(1001,"VersionConflict"); return {}; }
        const auto uri=QUrl(doc["uri"].toString()); if(!uri.isLocalFile()) { *error=rpcError(-32602,"Untitled documents have no image resource root"); return {}; }
        root=QFileInfo(uri.toLocalFile()).canonicalPath();
    } else if(resource["kind"]!="package") { *error=rpcError(-32602,"Unknown image resource kind"); return {}; }
    QByteArray bytes,mime; QString reason;
    if(!HtmlPolicy::readImage(root,resource["path"].toString(),&bytes,&mime,&reason)) { *error=rpcError(-32602,reason); return {}; }
    const auto cacheKey=QCryptographicHash::hash(QJsonDocument(resource).toJson(QJsonDocument::Compact)+QCryptographicHash::hash(bytes,QCryptographicHash::Sha256),QCryptographicHash::Sha256);
    if(panel.imageCache.contains(cacheKey)) return panel.imageCache[cacheKey];
    if(panel.images.size()>=64 || d->imageBytes>=32*1024*1024) { *error=rpcError(1007,"Image resource limit reached"); return {}; }
    if(d->imageBytes+bytes.size()>32*1024*1024) { *error=rpcError(1008,"Image budget exceeded"); return {}; }
    const auto resourceId=token(), key="nnp-resource:"+resourceId; const QUrl url("nnp-plugin://"+d->owner+"/"+id+"/image-"+resourceId);
    panel.images.insert(key,url); panel.imageCache.insert(cacheKey,key); d->handler->resources.insert(url.path(),{bytes,mime,id}); d->imageBytes+=bytes.size(); return key;
}
bool WebviewService::postMessage(const QString &id,const QJsonValue &message,QJsonObject *error) {
    *error={}; if(!d->panels.contains(id)) { *error=rpcError(1004,"Panel closed"); return false; }
    auto &panel=d->panels[id]; const auto json=QJsonDocument(QJsonArray{message}).toJson(QJsonDocument::Compact);
    if(json.size()>64*1024) { *error=rpcError(1008,"Message exceeds 64 KiB"); return false; }
    if(!panel.page)return false;
    if(panel.compatible&&!panel.loaded&&!panel.url.isEmpty()){
        if(panel.outgoing.size()>=128||panel.outgoingBytes+json.size()>8*1024*1024){*error=rpcError(1007,"Outgoing Webview queue overflow");return false;}
        panel.outgoing.append(message);panel.outgoingBytes+=json.size();return true;
    }
    if(!panel.loaded || !panel.page->owns(panel.page->url())) return false;
    const auto encoded=QJsonDocument(QJsonArray{QString::fromUtf8(json)}).toJson(QJsonDocument::Compact);
    const auto literal=QString::fromUtf8(encoded.mid(1,encoded.size()-2));
    panel.page->runJavaScript(panel.compatible?"window.dispatchEvent(new MessageEvent('message',{data:JSON.parse("+literal+")[0]}));":"window.dispatchEvent(new CustomEvent('nnp-message',{detail:JSON.parse("+literal+")[0]}));"); return true;
}
void WebviewService::dispose(const QString &id) {
    if(!d->panels.contains(id)) return;
    auto panel=d->panels.take(id);disconnect(panel.focusConnection);
    if(panel.service&&panel.page){const QMutexLocker lock(&d->interceptor->mutex);const auto prefix=panel.page->servicePrefix;if(d->interceptor->services.value(prefix)>1)--d->interceptor->services[prefix];else d->interceptor->services.remove(prefix);}
    if(panel.pendingHtml)panel.pendingHtml->cancel();
    for(auto it=d->handler->resources.begin();it!=d->handler->resources.end();) {
        if(it->panelId==id) { if(it->mime!="text/html") d->imageBytes-=it->bytes.size(); it=d->handler->resources.erase(it); } else ++it;
    }
    if(!panel.restoreId.isEmpty()&&d->restoreSource&&d->restoreSource->d->panels.contains(panel.restoreId)){
        auto *source=d->restoreSource.data();auto &old=source->d->panels[panel.restoreId];old.claimed=false;
        if(panel.dock)panel.dock->closing=[source,key=panel.restoreId]{source->dispose(key);};
        delete panel.container;
    }else if(panel.dock) {
        panel.dock->closing={}; panel.dock->hide();
        // A user-close callback runs inside QWidget's close event. Reclaim the
        // page now, but leave the empty dock alive until that event unwinds.
        auto *current=panel.dock->widget();if(current!=panel.container)delete panel.container;
        delete current; panel.dock->deleteLater();
    }
    if(!d->suspended)emit event(id,"closed",QJsonValue(QJsonValue::Null));
    emit panelsChanged();
}
void WebviewService::setRestoreSource(WebviewService *source){d->restoreSource=source;}
void WebviewService::suspendForRestart(std::function<void()> completed){
    d->suspended=true;
    {const QMutexLocker lock(&d->interceptor->mutex);d->interceptor->suspended=true;}
    auto remaining=std::make_shared<int>(d->panels.size());auto done=std::make_shared<bool>(false);
    const auto finish=[done,completed]{if(!*done){*done=true;completed();}};
    if(!*remaining){finish();return;}
    const QPointer<WebviewService> safe=this;
    const QString script=QStringLiteral(R"JS((()=>{const snapshot=window[Symbol.for('NotepadNext.compatWebviewBootstrap')]?.snapshot?.();const result={scrollX,scrollY,snapshot,fields:[]};let bytes=new TextEncoder().encode(JSON.stringify(result)).length;const nodes=Array.from(document.querySelectorAll('input:not([type=password]):not([type=file]),textarea,select')).slice(0,128);for(let i=0;i<nodes.length;++i){const n=nodes[i];if(n.value.length>4096)continue;const field={i,id:n.id,tag:n.tagName,type:n.type,value:n.value,checked:n.checked,selected:n.selectedIndex};const size=new TextEncoder().encode(JSON.stringify(field)).length;if(bytes+size>120*1024)break;result.fields.push(field);bytes+=size;}return JSON.stringify(result);})())JS");
    for(auto it=d->panels.begin();it!=d->panels.end();++it){
        if(it->pendingHtml)it->pendingHtml->cancel();++it->htmlSequence;
        if(it->container)it->container->setEnabled(false);
        if(!it->page){if(--*remaining==0)finish();continue;}
        it->page->runJavaScript(script,[safe,id=it.key(),remaining,finish](const QVariant &value){
            if(safe&&safe->d->panels.contains(id)){
                const auto bytes=value.toString().toUtf8();const auto state=bytes.size()<=128*1024?QJsonDocument::fromJson(bytes).object():QJsonObject{};
                auto &panel=safe->d->panels[id];panel.browserState=state;
                const auto snapshot=state["snapshot"].toObject();if(snapshot.contains("hasState"))panel.state=snapshot["hasState"].toBool()?snapshot["state"]:QJsonValue(QJsonValue::Undefined);
            }
            if(--*remaining==0)finish();
        });
    }
    QTimer::singleShot(1000,this,finish);
}
QJsonArray WebviewService::restorationDescriptors() const{
    QVector<const Private::Panel*> ordered;for(const auto &panel:d->panels)if(panel.dock&&!panel.claimed)ordered.append(&panel);
    std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->order<b->order;});QJsonArray result;
    for(const auto *panel:ordered){
        QString id;for(auto it=d->panels.cbegin();it!=d->panels.cend();++it)if(&it.value()==panel){id=it.key();break;}
        QJsonObject descriptor{{"panelId",id},{"viewType",panel->viewType},{"title",panel->dock->windowTitle()},
            {"visible",panel->dock->isVisible()},{"active",false},{"kind",panel->service?"service":panel->compatible?"html":"fragment"},{"hasState",!panel->state.isUndefined()},{"state",panel->state.isUndefined()?QJsonValue(QJsonValue::Null):panel->state}};
        if(panel->compatible&&!panel->service){QStringList roots;for(const auto &root:panel->roots)roots.append(QDir(d->root).relativeFilePath(root));descriptor["options"]=QJsonObject{{"enableScripts",panel->enableScripts},{"localResourceRoots",QJsonArray::fromStringList(roots)}};}
        result.append(descriptor);
    }
    return result;
}
void WebviewService::restoreBrowserState(const QString &id){
    if(!d->panels.contains(id))return;auto &panel=d->panels[id];if(!panel.page||!panel.loaded)return;
    const auto json=QString::fromUtf8(QJsonDocument(panel.browserState).toJson(QJsonDocument::Compact));const QPointer<WebviewService> safe=this;const auto revision=panel.revision;
    panel.page->runJavaScript("(()=>{const s="+json+";const nodes=document.querySelectorAll('input:not([type=password]):not([type=file]),textarea,select');for(const f of s.fields||[]){const n=f.id?document.getElementById(f.id):nodes[f.i];if(n&&n.tagName===f.tag&&n.type===f.type){n.value=f.value;if(typeof f.checked==='boolean')n.checked=f.checked;if(typeof f.selected==='number')n.selectedIndex=f.selected;}}scrollTo(s.scrollX||0,s.scrollY||0);return true;})()",[safe,id,revision](const QVariant &value){if(safe&&safe->d->panels.contains(id)&&safe->d->panels[id].revision==revision)safe->d->panels[id].uiRestored=value.toBool();});
}
void WebviewService::finishRestoration(std::function<void(const QJsonObject &)> completed){
    const QPointer<WebviewService> safe=this;auto *poll=new QTimer(this);poll->setInterval(20);auto *deadline=new QTimer(poll);deadline->setSingleShot(true);
    auto done=std::make_shared<bool>(false);const auto finish=[poll,done,completed](const QJsonObject &error){if(*done)return;*done=true;poll->stop();poll->deleteLater();completed(error);};
    connect(deadline,&QTimer::timeout,poll,[finish]{finish(rpcError(1005,"Plugin panels could not be restored; the previous version will be kept."));});
    connect(poll,&QTimer::timeout,poll,[safe,finish]{
        if(!safe){finish(rpcError(1004,"Plugin session closed during restoration"));return;}
        if(!safe->d->restoreSource){finish({});return;}
        for(const auto &old:safe->d->restoreSource->d->panels)if(old.dock&&!old.claimed)return;
        for(const auto &panel:safe->d->panels)if(!panel.restoreId.isEmpty()&&panel.dock&&(!panel.loaded||!panel.uiRestored))return;
        finish({});
    });deadline->start(10000);poll->start();
}
void WebviewService::commitRestoration(){
    if(!d->restoreSource)return;auto *source=d->restoreSource.data();
    for(auto it=d->panels.begin();it!=d->panels.end();++it)if(!it->restoreId.isEmpty()&&it->dock&&source->d->panels.contains(it->restoreId)){
        auto old=source->d->panels.take(it->restoreId);disconnect(old.focusConnection);
        it->dock->setWidget(it->container);it->dock->setWindowTitle(it->title);it->container->setEnabled(true);it->container->show();delete old.container;
        const auto id=it.key();QTimer::singleShot(0,this,[this,id]{restoreBrowserState(id);});it->restoreId.clear();
    }
    d->restoreSource=nullptr;
}
void WebviewService::clear() { const auto ids=d->panels.keys(); for(const auto &id:ids) dispose(id); }
int WebviewService::panelCount() const { return d->panels.size(); }
int WebviewService::resourceCount() const { return d->handler->resources.size(); }
QWebEnginePage *WebviewService::page(const QString &id) const { return d->panels.value(id).page; }
QUrl WebviewService::pageUrl(const QString &id) const { return d->panels.value(id).url; }
}
#include "WebviewService.moc"
