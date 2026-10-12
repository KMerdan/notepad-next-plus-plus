#include "../PluginTestFilesystem.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "WebviewService.h"
#include "HtmlPolicy.h"
#include "DocumentBridge.h"
#include "PluginSession.h"
#include "PluginEngine.h"
#include "PluginDataStore.h"
#include "PluginDataService.h"
#include "ProcessIdentity.h"
#include "PluginManagerDialog.h"
#include "RegisteredResourceRegistry.h"
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QImage>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTest>
#include <QWebEnginePage>
#include <QWebEngineFindTextResult>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWebChannel>
#include <QJsonDocument>
#include <QProcess>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QElapsedTimer>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <QSaveFile>
#ifdef Q_OS_MACOS
#include <libproc.h>
#endif
class ObservedTestApplication:public QApplication {
public:
    using QApplication::QApplication;
    int focusConnections() const {return receivers(SIGNAL(focusChanged(QWidget*,QWidget*)));}
};
using namespace PluginWebviews;
namespace {
QJsonObject lifecycleNativeSample() {
    QJsonObject result{{"focusConnections",static_cast<ObservedTestApplication*>(qApp)->focusConnections()}};
#ifdef Q_OS_MACOS
    proc_taskinfo task{};const auto pid=int(QCoreApplication::applicationPid());
    proc_fdinfo descriptors[4096]{};const auto bytes=proc_pidinfo(pid,PROC_PIDLISTFDS,0,descriptors,sizeof(descriptors));
    result.insert("fileDescriptors",bytes>0&&bytes<int(sizeof(descriptors))?bytes/int(sizeof(proc_fdinfo)):-1);
    QJsonObject types;if(bytes>0&&bytes<int(sizeof(descriptors)))for(int index=0;index<bytes/int(sizeof(proc_fdinfo));++index){const auto key=QString::number(descriptors[index].proc_fdtype);types[key]=types[key].toInt()+1;}result.insert("descriptorTypes",types);
    if(proc_pidinfo(pid,PROC_PIDTASKINFO,0,&task,sizeof(task))==sizeof(task)){result.insert("rssBytes",qint64(task.pti_resident_size));result.insert("threads",task.pti_threadnum);}
#else
    result.insert("nativeHandlesAvailable",false);
#endif
    return result;
}
PluginSources::RegisteredResource runtimeFixture(const QString &root,const QString &id,const QByteArray &code,const QStringList &capabilities={"commands@1"},const QString &kind="node") {
    QDir().mkpath(root+"/dist");
    QJsonObject manifest{{"manifestVersion",1},{"id",id},{"name",id},{"version","1.0.0"},{"list",false},
        {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.1.0"}}},
        {"runtime",QJsonObject{{"kind",kind},{"entry",kind=="python"?"dist/index.py":"dist/index.js"}}},
        {"capabilities",QJsonObject{{"required",QJsonArray::fromStringList(capabilities)},{"optional",QJsonArray{}}}},
        {"activationEvents",QJsonArray{"onCommand:"+id+".open"}},
        {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id",id+".open"},{"title","Open"}}}}}}};
    const auto write=[&](const QString &name,const QByteArray &bytes){QFile f(root+'/'+name);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())qFatal("Runtime fixture write failed");};
    write("notepadnext-plugin.json",QJsonDocument(manifest).toJson());write("LICENSE","Test license");write(kind=="python"?"dist/index.py":"dist/index.js",code);
    PluginSources::SourceSnapshot source;QString error;
    if(!PluginSources::PluginMetadata::parse(QJsonDocument(manifest).toJson(),"https://github.com/tests/runtime",{},&source,&error))qFatal("%s",qPrintable(error));
    return source.resources[0];
}
}
class PluginWebviewTests : public QObject {
    Q_OBJECT
private slots:
    void firstPanelKeepsEditorWindow() {
        QMainWindow window;
        auto *editor=new QPlainTextEdit(&window);window.setCentralWidget(editor);
        editor->setPlainText("Unsaved Markdown 😀\n");editor->document()->setModified(true);
        // This fixture verifies programmatic preservation, not keyboard input.
        // setPlainText still works when read-only; desktop typing cannot race it.
        editor->setReadOnly(true);
        prepareWindow(&window);
        window.resize(900,600);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto nativeId=window.winId();const auto geometry=window.geometry();
        class Observer:public QObject {
        public:
            QObject *target=nullptr;int destroyed=0;
            bool eventFilter(QObject *watched,QEvent *event) override {
                if(watched==target&&event->type()==QEvent::PlatformSurface&&
                   static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)++destroyed;
                return false;
            }
        } observer;
        observer.target=window.windowHandle();qApp->installEventFilter(&observer);
        QTemporaryDir temp;PluginDocuments::DocumentBridge documents;
        WebviewService service("tests.stable-window",temp.path(),&window,&documents);
        QJsonObject error;const auto id=service.create("preview","Markdown Preview","right",&error)["panelId"].toString();
        QVERIFY2(error.isEmpty(),qPrintable(error["message"].toString()));
        QVERIFY(service.setHtml(id,"<h1>Preview</h1>",QJsonObject{{"documentId",QJsonValue::Null},{"version",QJsonValue::Null}},&error));
        QVERIFY(service.reveal(id,true,&error));
        QTRY_VERIFY_WITH_TIMEOUT(service.page(id)->url()==service.pageUrl(id),10000);
        QTest::qWait(100);
        qInfo()<<"Native surface replacements:"<<observer.destroyed;
        QCOMPARE(observer.destroyed,0);QCOMPARE(window.winId(),nativeId);
        QVERIFY(window.isVisible());QCOMPARE(window.geometry(),geometry);
        QCOMPARE(editor->toPlainText(),QString("Unsaved Markdown 😀\n"));QVERIFY(editor->document()->isModified());
        service.clear();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(observer.destroyed,0);
        for(int cycle=0;cycle<3;++cycle){const auto reopened=service.create("preview","Markdown Preview","right",&error)["panelId"].toString();QVERIFY(service.reveal(reopened,true,&error));QTest::qWait(20);service.clear();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(observer.destroyed,0);QCOMPARE(window.winId(),nativeId);}
    }
    void compatibleFullPagePolicyAndStateBridge() {
        if(QT_VERSION<QT_VERSION_CHECK(6,6,0))QSKIP("Compatible HTML requires Qt WebEngine 6.6+");
        QTemporaryDir temp;QDir().mkpath(temp.path()+"/dist/media");const auto write=[&](const QString &name,const QByteArray &data){QFile f(temp.path()+name);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())qFatal("Cannot write compatible asset");};
        write("/dist/media/main.js",R"JS(window.compatApi=acquireVsCodeApi();window.initialState=compatApi.getState();compatApi.setState({count:7});compatApi.postMessage({ready:true});window.compatReady=true;window.addEventListener('message',e=>window.received=e.data);try{acquireVsCodeApi();}catch(e){window.acquireOnce=true;})JS");
        write("/dist/media/main.css","h1 { color: rgb(12, 34, 56); }\n");write("/outside.js","window.outsideRan=true;\n");
        QMainWindow window;PluginDocuments::DocumentBridge documents;WebviewService service("tests.compatible",temp.path(),&window,&documents,nullptr,QString(32,'b'));service.setPublished(false);QJsonObject error;const auto id=QString(32,'a');QSignalSpy events(&service,&WebviewService::event);
        QVERIFY(!service.createCompatible(id,"catCoding","Cat",true,{"dist/media"},&error).isEmpty());const auto js=service.mapAsset(id,"dist/media/main.js",&error),css=service.mapAsset(id,"dist/media/main.css",&error);QVERIFY(error.isEmpty());QCOMPARE(service.mapAsset(id,"dist/media/main.js",&error),js);QCOMPARE(service.resourceCount(),2);
        QVERIFY(service.mapAsset(id,"outside.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1003);QVERIFY(service.mapAsset(id,"../outside.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),-32602);
        const auto html=QString("<!doctype html><html><head><meta charset=\"utf-8\"><link rel=\"stylesheet\" href=\"%1\"></head><body><h1>Full HTML</h1><script nonce=\"allowed\" src=\"%2\"></script><script>window.inlineWithoutNonce=true;</script><script src=\"https://example.com/must-not-load.js\"></script></body></html>").arg(css,js);
        QVERIFY(service.setDocument(id,html,&error));QVERIFY(service.postMessage(id,QJsonObject{{"early",true}},&error));QVERIFY(service.reveal(id,true,&error));QVERIFY(service.page(id)->url().isEmpty());QCOMPARE(service.resourceCount(),3);window.show();service.setPublished(true);
        auto result=std::make_shared<QJsonObject>();QPointer<QWebEnginePage> page=service.page(id);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("JSON.stringify({ready:window.compatReady,once:window.acquireOnce,inline:window.inlineWithoutNonce===true,color:document.querySelector('h1')?getComputedStyle(document.querySelector('h1')).color:''})",[result](const QVariant &value){*result=QJsonDocument::fromJson(value.toString().toUtf8()).object();});return (*result)["ready"].toBool();})(),10000);
        QVERIFY((*result)["once"].toBool());QVERIFY(!(*result)["inline"].toBool());QCOMPARE((*result)["color"].toString(),QString("rgb(12, 34, 56)"));QTRY_VERIFY_WITH_TIMEOUT(([&]{for(const auto &event:events)if(event[1].toString()=="message"&&event[2].toJsonValue().toObject()["ready"].toBool())return true;return false;})(),5000);
        auto received=std::make_shared<bool>(false);QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("window.received?.early===true",[received](const QVariant &v){*received=v.toBool();});return *received;})(),5000);*received=false;QVERIFY(service.postMessage(id,QJsonObject{{"fromHost",true}},&error));QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("window.received?.fromHost===true",[received](const QVariant &v){*received=v.toBool();});return *received;})(),5000);
        const auto oldRevision=service.pageUrl(id).fileName().mid(5,32);QTest::qWait(100);QVERIFY(service.setDocument(id,html,&error));auto state=std::make_shared<int>(0);QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("window.initialState?.count||0",[state](const QVariant &v){*state=v.toInt();});return *state==7;})(),10000);QCOMPARE(service.resourceCount(),3);
        auto *bridge=page->webChannel()->registeredObjects().value("panelBridge");QVERIFY(bridge);QVERIFY(QMetaObject::invokeMethod(bridge,"postMessage",Qt::DirectConnection,Q_ARG(QString,oldRevision),Q_ARG(QVariant,QVariant(QVariantMap{{"stale",true}}))));QVERIFY(QMetaObject::invokeMethod(bridge,"commitState",Qt::DirectConnection,Q_ARG(QString,oldRevision),Q_ARG(bool,true),Q_ARG(QVariant,QVariant(QVariantMap{{"count",999}}))));page->runJavaScript("compatApi.setState(JSON.parse('{\"count\":7,\"__proto__\":{\"polluted\":true}}'));try{compatApi.postMessage('x'.repeat(65537));}catch(e){window.messageBudgetRejected=true;}");QTest::qWait(100);for(const auto &event:events)QVERIFY(!event[2].toJsonValue().toObject()["stale"].toBool());auto budget=std::make_shared<bool>(false);QTRY_VERIFY(([&]{page->runJavaScript("window.messageBudgetRejected===true",[budget](const QVariant &v){*budget=v.toBool();});return *budget;})());QVERIFY(service.setDocument(id,html,&error));auto safe=std::make_shared<bool>(false);QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("window.initialState?.count===7&&Object.hasOwn(window.initialState,'__proto__')&&!({}).polluted",[safe](const QVariant &v){*safe=v.toBool();});return *safe;})(),10000);
        const auto revision=service.pageUrl(id);QVERIFY(!service.setHtml(id,"<p>wrong profile</p>",{{"documentId",QJsonValue(QJsonValue::Null)},{"version",QJsonValue(QJsonValue::Null)}},&error));QCOMPARE(error["code"].toInt(),1005);QCOMPARE(service.pageUrl(id),revision);
        service.dispose(id);QCOMPARE(service.panelCount(),0);QCOMPARE(service.resourceCount(),0);QVERIFY(!service.page(id));
    }
    void compatibleAssetBoundariesCrossPanelAndScriptPolicy() {
        if(QT_VERSION<QT_VERSION_CHECK(6,6,0))QSKIP("Compatible HTML requires Qt WebEngine 6.6+");
        QTemporaryDir temp;QDir().mkpath(temp.path()+"/dist/media");const auto write=[&](const QString &name,const QByteArray &data){QFile file(temp.path()+name);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())qFatal("Cannot write compatible boundary fixture");};
        write("/dist/media/own.js","window.ownRan=true;");write("/dist/media/foreign.js","window.foreignRan=true;");write("/dist/media/中文 space.js","window.unicodeRan=true;");write("/private.js","window.privateRan=true;");write("/dist/media/bad.js",QByteArray::fromHex("c080"));write("/dist/media/large.js",QByteArray(2*1024*1024+1,' '));QVERIFY(PluginTest::link(temp.path()+"/private.js",temp.path()+"/dist/media/escape.js"));
        QMainWindow window;PluginDocuments::DocumentBridge documents;WebviewService service("tests.boundaries",temp.path(),&window,&documents);QJsonObject error;const auto a=QString(32,'a'),b=QString(32,'b'),c=QString(32,'c'),d=QString(32,'d');
        QVERIFY(!service.createCompatible(a,"first","First",true,{"dist/media"},&error).isEmpty());QVERIFY(!service.createCompatible(b,"second","Second",true,{"dist/media"},&error).isEmpty());QVERIFY(!service.createCompatible(c,"disabled","Disabled",false,{"dist/media"},&error).isEmpty());QVERIFY(!service.createCompatible(d,"noRoots","No roots",true,{},&error).isEmpty());
        const auto own=service.mapAsset(a,"dist/media/own.js",&error),foreign=service.mapAsset(b,"dist/media/foreign.js",&error),disabled=service.mapAsset(c,"dist/media/own.js",&error),unicode=service.mapAsset(a,"dist/media/中文 space.js",&error);QVERIFY(error.isEmpty());QVERIFY(unicode.endsWith("/%E4%B8%AD%E6%96%87%20space.js"));
        QVERIFY(service.mapAsset(a,"dist/media/escape.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1003);QVERIFY(service.mapAsset(a,"dist/media/bad.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1009);QVERIFY(service.mapAsset(a,"dist/media/large.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1008);QVERIFY(service.mapAsset(d,"dist/media/own.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1003);
        QVERIFY(service.setDocument(a,QString("<html><body><h1 id=\"done\">First</h1><script nonce=\"allowed\" src=\"%1\"></script><script nonce=\"allowed\" src=\"%2\"></script><script nonce=\"allowed\" src=\"%3\"></script></body></html>").arg(own,foreign,unicode),&error));QVERIFY(service.setDocument(c,QString("<html><body><h1 id=\"done\">Disabled</h1><script nonce=\"allowed\" src=\"%1\"></script><script nonce=\"allowed\">window.inlineRan=true;</script></body></html>").arg(disabled),&error));
        window.show();QVERIFY(service.reveal(a,true,&error));QVERIFY(service.reveal(c,true,&error));auto first=std::make_shared<QJsonObject>(),blocked=std::make_shared<QJsonObject>();QTRY_VERIFY_WITH_TIMEOUT(([&]{service.page(a)->runJavaScript("JSON.stringify({own:window.ownRan===true,unicode:window.unicodeRan===true,foreign:window.foreignRan===true})",[first](const QVariant &value){*first=QJsonDocument::fromJson(value.toString().toUtf8()).object();});return (*first)["own"].toBool()&&(*first)["unicode"].toBool();})(),10000);QVERIFY(!(*first)["foreign"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(([&]{service.page(c)->runJavaScript("JSON.stringify({ready:!!document.querySelector('#done'),own:window.ownRan===true,inline:window.inlineRan===true})",[blocked](const QVariant &value){*blocked=QJsonDocument::fromJson(value.toString().toUtf8()).object();});return (*blocked)["ready"].toBool();})(),10000);QVERIFY(!(*blocked)["own"].toBool());QVERIFY(!(*blocked)["inline"].toBool());
        service.clear();QCOMPARE(service.panelCount(),0);QCOMPARE(service.resourceCount(),0);
        QVERIFY(!service.createCompatible(a,"quota","Quota",true,{"dist/media"},&error).isEmpty());for(int n=0;n<64;++n){const auto relative="dist/media/asset"+QString::number(n)+".js";write('/'+relative,"// bounded");QVERIFY(!service.mapAsset(a,relative,&error).isEmpty());}write("/dist/media/extra.js","// extra");QVERIFY(service.mapAsset(a,"dist/media/extra.js",&error).isEmpty());QCOMPARE(error["code"].toInt(),1007);QCOMPARE(service.resourceCount(),64);service.dispose(a);QCOMPARE(service.resourceCount(),0);
    }
    void focusNotificationsAfterDeferredDockCleanup() {
        if(QT_VERSION<QT_VERSION_CHECK(6,6,0))QSKIP("Compatible HTML requires Qt WebEngine 6.6+");
        QTemporaryDir temp;QDir().mkpath(temp.path()+"/dist");QMainWindow window;PluginDocuments::DocumentBridge documents;
        auto service=std::make_unique<WebviewService>("tests.focus-lifetime",temp.path(),&window,&documents);QJsonObject error;const auto id=QString(32,'a');QVERIFY(!service->createCompatible(id,"lifetime","Lifetime",false,{"dist"},&error).isEmpty());QPointer<QDockWidget> dock=window.findChild<QDockWidget*>();QVERIFY(dock);service.reset();QVERIFY(dock);
        // QApplication can emit while the empty dock is awaiting deleteLater.
        QVERIFY(QMetaObject::invokeMethod(qApp,"focusChanged",Qt::DirectConnection,Q_ARG(QWidget*,nullptr),Q_ARG(QWidget*,nullptr)));QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dock);
    }
    void adaptedVSCodeWebviewInstalledLifecycle() {
        if(QT_VERSION<QT_VERSION_CHECK(6,6,0))QSKIP("Compatible HTML requires Qt WebEngine 6.6+");
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;QProcess importer;importer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_LEGACY_STAGER,PLUGIN_TEST_VSCODE_WEBVIEW_SOURCE,temp.path()+"/input","--webviews","--resource=media"});QVERIFY(importer.waitForFinished(10000));QVERIFY2(importer.exitCode()==0,importer.readAllStandardError());
        QFile metadata(temp.path()+"/input/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));PluginSources::SourceSnapshot source;QString error;QVERIFY2(PluginSources::PluginMetadata::parse(metadata.readAll(),"https://github.com/microsoft/vscode-extension-samples",{},&source,&error),qPrintable(error));const auto resource=source.resources[0];const auto id=resource.id;const auto aliases=resource.metadata["compatibility"].toObject()["commands"].toArray();const auto start=aliases[0].toObject()["target"].toString(),refactor=aliases[1].toObject()["target"].toString();
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"text");documents.setActive(doc);auto *menu=window.menuBar()->addMenu("Plugins");PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,menu);QSignalSpy installed(&installer,&InstallationService::finished),failures(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(temp.path()+"/input",resource));QTRY_COMPARE(installed.size(),1);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(!index.plugin(id).enabled);QCOMPARE(engine.sessionCount(),0);QVERIFY(engine.setEnabled(id,true,&error));QCOMPARE(engine.sessionCount(),0);QAction *action=nullptr;for(auto *candidate:engine.findChildren<QAction*>())if(candidate->objectName()=="pluginCommand:"+start)action=candidate;QVERIFY(action);window.show();action->trigger();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QTRY_VERIFY(window.findChild<QWebEngineView*>());QPointer<QWebEnginePage> page=window.findChild<QWebEngineView*>()->page();auto preview=std::make_shared<QJsonObject>();
        // The profile retains hidden DOM. Exercise messaging in that state so
        // focus-driven upstream HTML replacement cannot erase the observer.
        window.hide();
        QTRY_VERIFY2_WITH_TIMEOUT(([&]{if(!page)return false;page->runJavaScript("Math.random=()=>1;window.refactorObserved=window.refactorObserved||false;if(!window.testListener){window.testListener=true;window.addEventListener('message',e=>{if(e.data.command==='refactor')window.refactorObserved=true;});}JSON.stringify({count:Number(document.querySelector('#lines-of-code-counter')?.textContent),image:document.querySelector('img')?.naturalWidth,styles:document.styleSheets.length})",[preview](const QVariant &value){*preview=QJsonDocument::fromJson(value.toString().toUtf8()).object();});return (*preview)["count"].toInt()>=8&&(*preview)["image"].toInt()==48&&(*preview)["styles"].toInt()==2;})(),qPrintable(engine.pluginLog(id)),10000);
        const auto before=(*preview)["count"].toInt();engine.invoke(id,refactor);QTRY_VERIFY2_WITH_TIMEOUT(([&]{if(!page)return false;page->runJavaScript("JSON.stringify({observed:window.refactorObserved,count:Number(document.querySelector('#lines-of-code-counter')?.textContent)})",[preview](const QVariant &value){*preview=QJsonDocument::fromJson(value.toString().toUtf8()).object();});return (*preview)["observed"].toBool();})(),qPrintable(engine.pluginLog(id)),5000);QVERIFY((*preview)["count"].toInt()<=before/2+3);
        page->runJavaScript("Math.random=()=>0;");QTRY_VERIFY_WITH_TIMEOUT(!window.findChildren<QMessageBox*>().isEmpty(),5000);QVERIFY(window.findChild<QMessageBox*>()->text().startsWith(QString::fromUtf8("🐛")));page->runJavaScript("Math.random=()=>1;");for(auto *message:window.findChildren<QMessageBox*>())message->button(QMessageBox::Ok)->click();
        QDockWidget *dock=nullptr;
        for(auto *ancestor=window.findChild<QWebEngineView*>()->parentWidget();ancestor&&!dock;ancestor=ancestor->parentWidget())dock=qobject_cast<QDockWidget*>(ancestor);
        QVERIFY(dock);dock->close();QTRY_VERIFY(!window.findChild<QWebEngineView*>());QTRY_COMPARE(engine.resourceCount(),3);QVERIFY(!page);
        // First-use verification rebuilds menu actions. Reacquire the current
        // action rather than retaining the deleted pre-verification pointer.
        action=nullptr;for(auto *candidate:engine.findChildren<QAction*>())if(candidate->objectName()=="pluginCommand:"+start)action=candidate;QVERIFY(action);action->trigger();QTRY_VERIFY2_WITH_TIMEOUT(window.findChild<QWebEngineView*>(),qPrintable(engine.pluginLog(id)),10000);QCOMPARE(engine.sessionCount(),1);QTRY_COMPARE_WITH_TIMEOUT(engine.resourceCount(),9,10000);engine.reload(id);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QCOMPARE(engine.resourceCount(),9);QVERIFY(window.findChild<QWebEngineView*>());engine.invoke(id,start);QTRY_VERIFY2_WITH_TIMEOUT(window.findChild<QWebEngineView*>(),qPrintable(engine.pluginLog(id)),10000);QVERIFY(engine.setEnabled(id,false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.resourceCount(),0);QVERIFY(!window.findChild<QWebEngineView*>());QVERIFY(menu->actions().isEmpty());engine.uninstall(id,true);QTRY_VERIFY(index.plugins().isEmpty());QVERIFY2(failures.isEmpty(),qPrintable(engine.pluginLog(id)));documents.detach(doc);window.takeCentralWidget();
    }
    void compatibleNativeCapabilityPair() {
        if(QT_VERSION<QT_VERSION_CHECK(6,6,0))QSKIP("Compatible HTML requires Qt WebEngine 6.6+");
        using namespace PluginRuntime;
        for(int scenario=0;scenario<3;++scenario){QTemporaryDir temp;const auto id=QString("tests.compat-caps"),root=temp.path()+"/input";QStringList caps{"commands@1"};if(scenario>0)caps.append("webviews.compat@1");if(scenario==2)caps.append("webviews@1");
            const auto resource=runtimeFixture(root,id,R"JS(exports.activate=async ctx=>{await ctx.commands.register('tests.compat-caps.open',async()=>{try{const panel=await ctx.webviews.createCompatible({panelId:'a'.repeat(32),viewType:'Caps',title:'Capability test',enableScripts:false,localResourceRoots:['dist']});await panel.dispose();return 0;}catch(error){return error.code;}});};)JS",caps);QMainWindow window;PluginDocuments::DocumentBridge documents;PluginSession session(resource.metadata,root,PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,&window,&documents);QSignalSpy finished(&session,&PluginSession::commandFinished),failed(&session,&PluginSession::failed),stopped(&session,&PluginSession::stopped);session.invoke(id+".open");QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QVERIFY(finished[0][2].toJsonObject().isEmpty());QCOMPARE(finished[0][1].toJsonValue().toInt(),scenario==2?0:1003);QCOMPARE(session.resourceCount(),1);session.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000);QCOMPARE(session.resourceCount(),0);QVERIFY(failed.isEmpty());}
    }
    void nativeMessagesEnforceCapabilitiesPublicationAndOwnership() {
        using namespace PluginRuntime;
        for(int granted=0;granted<2;++granted){
            QTemporaryDir temp;const auto id=QString("tests.message-boundary"),root=temp.path()+"/input";QStringList caps{"commands@1"};if(granted)caps.append("messages@1");
            const auto code=R"JS(exports.activate=async ctx=>{
  let activationDenied=0;try{await ctx.window.showMessage('info','Must not publish during activation');}catch(error){activationDenied=error.code;}
  await ctx.commands.register('tests.message-boundary.open',async()=>{
    if(ctx.capabilities.includes('messages@1'))for(let n=0;n<8;++n)void ctx.window.showMessage('info','Owned '+n).catch(error=>console.error(error));
    let denied=0;try{await ctx.window.showMessage('info','Over budget or denied');}catch(error){denied=error.code;}
    return {activationDenied,denied};
  });
};)JS";
            const auto resource=runtimeFixture(root,id,code,caps);QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"text");documents.setActive(doc);
            PluginSession session(resource.metadata,root,PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,&window,&documents);QSignalSpy failed(&session,&PluginSession::failed),finished(&session,&PluginSession::commandFinished),stopped(&session,&PluginSession::stopped);session.invoke(id+".open");QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QVERIFY(finished[0][2].toJsonObject().isEmpty());const auto result=finished[0][1].toJsonValue().toObject();QCOMPARE(result["activationDenied"].toInt(),granted?1004:1003);QCOMPARE(result["denied"].toInt(),granted?1007:1003);QCOMPARE(window.findChildren<QMessageBox*>().size(),granted?8:0);QCOMPARE(session.resourceCount(),granted?9:1);
            session.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000);QCOMPARE(session.resourceCount(),0);QTRY_VERIFY(window.findChildren<QMessageBox*>().isEmpty());QVERIFY(failed.isEmpty());documents.detach(doc);window.takeCentralWidget();
        }
    }
    void originalVSCodeHelloWorldInstalledLifecycle() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;QProcess importer;importer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_LEGACY_STAGER,PLUGIN_TEST_VSCODE_SOURCE,temp.path()+"/input"});QVERIFY(importer.waitForFinished(10000));QVERIFY2(importer.exitCode()==0,importer.readAllStandardError());
        QFile metadata(temp.path()+"/input/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));PluginSources::SourceSnapshot source;QString error;QVERIFY2(PluginSources::PluginMetadata::parse(metadata.readAll(),"https://github.com/microsoft/vscode-extension-samples",{},&source,&error),qPrintable(error));const auto resource=source.resources[0];const auto id=resource.id,command=resource.metadata["compatibility"].toObject()["commands"].toArray()[0].toObject()["target"].toString();
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"text");documents.setActive(doc);auto *menu=window.menuBar()->addMenu("Plugins");
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,menu);QSignalSpy installed(&installer,&InstallationService::finished),failures(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(temp.path()+"/input",resource));QTRY_COMPARE(installed.size(),1);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(!index.plugin(id).enabled);QCOMPARE(engine.sessionCount(),0);QVERIFY(menu->actions().isEmpty());
        QVERIFY(engine.setEnabled(id,true,&error));QCOMPARE(engine.sessionCount(),0);QVERIFY(!menu->actions().isEmpty());QAction *action=nullptr;for(auto *candidate:engine.findChildren<QAction*>())if(candidate->objectName()=="pluginCommand:"+command)action=candidate;QVERIFY(action);action->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QMessageBox*>().size(),1,5000);auto *message=window.findChild<QMessageBox*>();QCOMPARE(message->text(),QString("Hello World!"));QCOMPARE(message->textFormat(),Qt::PlainText);QCOMPARE(engine.resourceCount(),3);message->button(QMessageBox::Ok)->click();QTRY_VERIFY(window.findChildren<QMessageBox*>().isEmpty());QTRY_COMPARE(engine.resourceCount(),2);
        engine.reload(id);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QCOMPARE(engine.sessionCount(),1);engine.invoke(id,command);QTRY_COMPARE(window.findChildren<QMessageBox*>().size(),1);
        QVERIFY(engine.setEnabled(id,false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.resourceCount(),0);QVERIFY(menu->actions().isEmpty());QTRY_VERIFY(window.findChildren<QMessageBox*>().isEmpty());QVERIFY2(failures.isEmpty(),qPrintable(engine.pluginLog(id)));documents.detach(doc);window.takeCentralWidget();
    }
    void compatibleEditorTransactionAndHumanMessageWait() {
        using namespace PluginRuntime;
        QTemporaryDir temp;const auto root=temp.path()+"/input",id=QString("tests.compat-native");
        const auto code=R"JS(const vscode=require('vscode');
exports.activate=ctx=>{let count=0;ctx.subscriptions.push(vscode.commands.registerCommand('extension.edit',async()=>{
  const editor=vscode.window.activeTextEditor;
  if(++count===1){const applied=await editor.edit(edit=>{edit.replace(new vscode.Range(0,1,0,3),'X');edit.insert(new vscode.Position(1,2),'!');});if(!applied)throw new Error('edit failed');}
  const choice=await vscode.window.showInformationMessage('<literal> 😀', 'Accept', 'Cancel');
  return {choice:choice??null,text:editor.document.getText(),version:editor.document.version};
}));};)JS";
        auto resource=runtimeFixture(root,id,code,{"commands@1","documents.read@1","documents.delta@1","documents.write@1","messages@1"});auto manifest=resource.metadata;manifest["compatibility"]=QJsonObject{{"kind","vscode"},{"version","0.1.0"},{"api","1.100.0"},{"extensionRoot","dist"},{"commands",QJsonArray{QJsonObject{{"id","extension.edit"},{"target",id+".open"}}}}};QFile metadata(root+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::WriteOnly));metadata.write(QJsonDocument(manifest).toJson());metadata.close();
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);editor.setText("a😀中\r\né\n");editor.setSavePoint();editor.emptyUndoBuffer();PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"text");documents.setActive(doc);
        PluginSession session(manifest,root,PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,&window,&documents);QSignalSpy failures(&session,&PluginSession::failed),finished(&session,&PluginSession::commandFinished),stopped(&session,&PluginSession::stopped);session.invoke(id+".open");QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QMessageBox*>().size(),1,10000);QCOMPARE(editor.textRange(0,editor.length()),QByteArray("aX中\r\né!\n"));QCOMPARE(session.resourceCount(),3);
        QTest::qWait(5300);QCOMPARE(finished.size(),0);QVERIFY(failures.isEmpty());auto *message=window.findChild<QMessageBox*>();QCOMPARE(message->textFormat(),Qt::PlainText);for(auto *button:message->buttons())if(button->text()=="Accept")button->click();QTRY_COMPARE(finished.size(),1);QVERIFY(finished[0][2].toJsonObject().isEmpty());QCOMPARE(finished[0][1].toJsonValue().toObject()["choice"].toString(),QString("Accept"));QCOMPARE(finished[0][1].toJsonValue().toObject()["text"].toString(),QString::fromUtf8("aX中\r\né!\n"));
        editor.undo();QCOMPARE(editor.textRange(0,editor.length()),QByteArray("a😀中\r\né\n"));editor.redo();QCOMPARE(editor.textRange(0,editor.length()),QByteArray("aX中\r\né!\n"));QTRY_VERIFY(window.findChildren<QMessageBox*>().isEmpty());session.invoke(id+".open");QTRY_COMPARE(window.findChildren<QMessageBox*>().size(),1);session.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000);QCOMPARE(session.resourceCount(),0);QTRY_VERIFY(window.findChildren<QMessageBox*>().isEmpty());QVERIFY(failures.isEmpty());documents.detach(doc);window.takeCentralWidget();
    }
    void nativeDeltaCapabilitiesAndCensusLimit() {
        using namespace PluginPackages;using namespace PluginRuntime;
        for(int scenario=0;scenario<3;++scenario) {
            QTemporaryDir temp;const auto id=QString("tests.delta-boundary");QStringList caps{"commands@1","storage@1"};if(scenario!=1)caps.append("documents.delta@1");if(scenario!=0)caps.append("documents.read@1");
            const auto resource=runtimeFixture(temp.path()+"/input",id,"exports.activate=async ctx=>{await ctx.commands.register('tests.delta-boundary.open',async()=>{let code=0;try{const watch=await ctx.documents.watchAll(()=>{});await watch.dispose();}catch(error){code=error.code;}await ctx.storage.update('result',code);});};\n",caps);
            QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
            QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"text");documents.setActive(doc);
            QList<std::shared_ptr<ScintillaEdit>> extras;QStringList extraIds;if(scenario==2)for(int i=0;i<64;++i){auto extra=std::make_shared<ScintillaEdit>();extraIds.append(documents.attach(extra.get(),{},"text"));extras.append(extra);}
            PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);const PluginData::Store data(temp.path()+"/data",resource.repository);
            QVERIFY(installer.installDirectory(temp.path()+"/input",resource));QTRY_COMPARE(installed.size(),1);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(data.access(id,"storage","result").value.toInt(),scenario==2?1007:1003,5000);QCOMPARE(engine.resourceCount(),1);QVERIFY(errors.isEmpty());
            engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.resourceCount(),0);for(const auto &extra:extraIds)documents.detach(extra);documents.detach(doc);window.takeCentralWidget();
        }
    }
    void nativeDocumentMirrorLifecycle() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto id=QString("tests.document-mirror");
        const auto module=QJsonDocument(QJsonArray{QString(PLUGIN_TEST_MIRROR)}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
        const auto code=QByteArray("const {DocumentMirror}=require(")+module+R"JS();
exports.activate=async ctx=>{
  const events=[];let count=0;
  const mirror=await new DocumentMirror(ctx.documents,{onEvent:e=>{const entry={type:e.type};if(e.reason)entry.reason=e.reason;if(e.contentChanges)entry.changes=e.contentChanges.length;events.push(entry);if(events.length>64)events.shift();},fault:e=>console.error(e)}).start();
  ctx.subscriptions.push(mirror);
  await ctx.commands.register('tests.document-mirror.open',async()=>{
    await mirror.flush();
    const documents=mirror.documents.map(d=>({documentId:d.documentId,uri:d.uri,languageId:d.languageId,version:d.version,isDirty:d.isDirty,text:d.getText(),line0:d.lineAt(0).text,lineCount:d.lineCount}));
    await ctx.storage.update('snapshot',{count:++count,documents,activeId:mirror.activeDocument?.documentId??null,unsupported:mirror.unsupported,events});
  });
};
)JS";
        const auto resource=runtimeFixture(temp.path()+"/input",id,code,{"commands@1","documents.read@1","documents.delta@1","storage@1"});
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit first,second,invalid;first.setCodePage(SC_CP_UTF8);second.setCodePage(SC_CP_UTF8);invalid.setCodePage(SC_CP_UTF8);first.setText("a😀中\r\né\n");second.setText("second unsaved");first.emptyUndoBuffer();second.emptyUndoBuffer();window.setCentralWidget(&first);
        PluginDocuments::DocumentBridge documents;const auto firstId=documents.attach(&first,{},"text"),secondId=documents.attach(&second,{},"text");documents.setActive(firstId);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);
        connect(&engine,&PluginEngine::error,this,[&](const QString &plugin,const QString &message){qWarning()<<"Mirror acceptance failure"<<message<<engine.pluginLog(plugin);});
        int completed=0;QPointer<PluginSession> observed;
        connect(&engine,&PluginEngine::statusChanged,this,[&](const QString &){if(observed)return;observed=engine.findChild<PluginSession*>();if(observed)connect(observed,&PluginSession::commandFinished,this,[&](const QString &,const QJsonValue &,const QJsonObject &){++completed;});});
        const PluginData::Store data(temp.path()+"/data",resource.repository);
        const auto snapshot=[&]{return data.access(id,"storage","snapshot").value.toObject();};
        const auto record=[&](const QString &doc){for(const auto &item:snapshot()["documents"].toArray())if(item.toObject()["documentId"]==doc)return item.toObject();return QJsonObject();};
        const auto eventCount=[&](const QString &type){int count=0;for(const auto &event:snapshot()["events"].toArray())if(event.toObject()["type"]==type)++count;return count;};
        QVERIFY(installer.installDirectory(temp.path()+"/input",resource));QTRY_COMPARE(installed.size(),1);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_VERIFY_WITH_TIMEOUT(snapshot()["count"].toInt()==1||!errors.isEmpty(),5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),1);QCOMPARE(snapshot()["documents"].toArray().size(),2);QCOMPARE(record(firstId)["text"].toString(),QStringLiteral("a😀中\r\né\n"));QCOMPARE(record(firstId)["line0"].toString(),QStringLiteral("a😀中"));QCOMPARE(snapshot()["activeId"].toString(),firstId);QCOMPARE(engine.resourceCount(),2);QTRY_COMPARE_WITH_TIMEOUT(completed,1,5000);
        first.setTargetStart(1);first.setTargetEnd(8);first.replaceTarget(0,"");second.insertText(second.length()," tail");documents.setActive(secondId);engine.invoke(id,id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(completed,2,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),2);QCOMPARE(record(firstId)["text"].toString(),QStringLiteral("a\r\né\n"));QCOMPARE(record(secondId)["text"].toString(),"second unsaved tail");QCOMPARE(snapshot()["activeId"].toString(),secondId);QCOMPARE(eventCount("resynchronized"),0);
        const auto pos=[](int line,int character){return QJsonObject{{"line",line},{"character",character}};};const auto edit=[&](int line,int end,const QString &text){return QJsonObject{{"range",QJsonObject{{"start",pos(line,0)},{"end",pos(line,end)}}},{"text",text}};};
        QJsonObject editError;const auto changes=eventCount("changed");const auto result=documents.applyEdits(firstId,documents.info(firstId)["version"].toInteger(),{edit(0,1,"B"),edit(1,2,"new")},&editError);QVERIFY2(editError.isEmpty(),QJsonDocument(editError).toJson().constData());engine.invoke(id,id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(completed,3,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),3);QCOMPARE(record(firstId)["text"].toString(),QStringLiteral("B\r\nnew\n"));QCOMPARE(record(firstId)["version"].toInteger(),result["version"].toInteger());QCOMPARE(eventCount("changed"),changes+1);QCOMPARE(eventCount("resynchronized"),0);
        first.undo();engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(completed,4,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),4);QCOMPARE(record(firstId)["text"].toString(),QStringLiteral("a\r\né\n"));QCOMPARE(eventCount("resynchronized"),0);
        first.redo();documents.updateMetadata(firstId,QUrl::fromLocalFile("/tmp/mirror renamed.md"),"markdown");engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(completed,5,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),5);QCOMPARE(record(firstId)["text"].toString(),QStringLiteral("B\r\nnew\n"));QCOMPARE(record(firstId)["languageId"].toString(),"markdown");QVERIFY(record(firstId)["uri"].toString().contains("mirror%20renamed.md"));QCOMPARE(eventCount("resynchronized"),0);
        {const QSignalBlocker blocked(&first);first.setText("reset snapshot");}documents.reset(firstId);engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(completed,6,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),6);QCOMPARE(record(firstId)["text"].toString(),"reset snapshot");QCOMPARE(eventCount("resynchronized"),1);
        const char bytes[]={char(0xc3),char(0x28),0};invalid.setText(bytes);const auto invalidId=documents.attach(&invalid,{},"text");documents.detach(secondId);engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(completed,7,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),7);QCOMPARE(snapshot()["documents"].toArray().size(),1);QCOMPARE(snapshot()["unsupported"].toArray().size(),1);QCOMPARE(snapshot()["unsupported"].toArray()[0].toObject()["errorCode"].toInt(),1009);QCOMPARE(snapshot()["unsupported"].toArray()[0].toObject()["documentId"].toString(),invalidId);
        {const QSignalBlocker blocked(&invalid);invalid.setText("supported again");}documents.reset(invalidId);engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(completed,8,5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(snapshot()["count"].toInt(),8);QVERIFY(snapshot()["unsupported"].toArray().isEmpty());QCOMPARE(record(invalidId)["text"].toString(),"supported again");
        QVERIFY(engine.setEnabled(id,false,&error));QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("disabled"),5000);QCOMPARE(engine.resourceCount(),0);QVERIFY(errors.isEmpty());engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);documents.detach(firstId);documents.detach(invalidId);window.takeCentralWidget();
    }
    void pythonInspectorExample() {
        if(QString(PLUGIN_TEST_PYTHON).isEmpty())QSKIP("An explicit Python interpreter was not configured for this build.");
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;QProcess pack;pack.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_STAGER,PLUGIN_TEST_PYTHON_EXAMPLE,temp.path()+"/release"});QVERIFY(pack.waitForFinished(10000));QVERIFY2(pack.exitCode()==0,pack.readAllStandardError().constData());
        QFile metadata(QString(PLUGIN_TEST_PYTHON_EXAMPLE)+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));PluginSources::SourceSnapshot source;QString error;QVERIFY(PluginSources::PluginMetadata::parse(metadata.readAll(),"https://github.com/notepadnext/python-inspector",{},&source,&error));const auto resource=source.resources[0];const auto id=resource.id;
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("Python sees unsaved 😀中\n");window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,PLUGIN_TEST_PYTHON});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(temp.path()+"/release",resource));QTRY_COMPARE(installed.size(),1);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(!index.plugin(id).enabled);QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),5000);QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QWebEngineView*>(),5000);auto *page=window.findChild<QWebEngineView*>()->page();auto text=std::make_shared<QString>();
        QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("document.querySelector('pre')?.textContent",[text](const QVariant &value){*text=value.toString();});return *text==QStringLiteral("Python sees unsaved 😀中\n");})(),5000);
        const PluginData::Store store(temp.path()+"/data",resource.repository);QTRY_COMPARE_WITH_TIMEOUT(store.access(id,"storage","lastPreview").value.toObject()["text"].toString(),QStringLiteral("Python sees unsaved 😀中\n"),5000);
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(window.findChildren<QDockWidget*>().isEmpty());QVERIFY(errors.isEmpty());documents.detach(doc);window.takeCentralWidget();
    }
    void pythonPluginLifecycle() {
        if(QString(PLUGIN_TEST_PYTHON).isEmpty())QSKIP("An explicit Python interpreter was not configured for this build.");
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto nodeId=QString("tests.python-companion"),pyId=QString("tests.python");
        const auto native=runtimeFixture(temp.path()+"/node",nodeId,"exports.activate=async ctx=>{await ctx.commands.register('tests.python-companion.open',async()=>{const doc=await ctx.documents.getActive();await ctx.storage.update('snapshot',await ctx.documents.getSnapshot(doc.documentId));});};\n",{"commands@1","documents.read@1","storage@1"});
        const auto python=runtimeFixture(temp.path()+"/python",pyId,R"PY(import asyncio
import os
def spin():
    while True:
        pass
async def activate(ctx):
    async def read():
        doc = await ctx.documents.get_active()
        snapshot = await ctx.documents.get_snapshot(doc['documentId'])
        await ctx.storage.update('snapshot', snapshot)
        await ctx.storage.update('processId', os.getpid())
        if await ctx.storage.get('hungOnce') is None:
            await ctx.storage.update('hungOnce', True)
            asyncio.get_running_loop().call_later(0.1, spin)
        return None
    await ctx.commands.register('tests.python.open', read)
)PY",{"commands@1","documents.read@1","storage@1"},"python");
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("shared unsaved 😀中\r\n");window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,PLUGIN_TEST_PYTHON});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(temp.path()+"/node",native));QTRY_COMPARE(installed.size(),1);QVERIFY(installed[0][1].toString().isEmpty());QVERIFY(engine.setEnabled(nodeId,true,&error));engine.invoke(nodeId,nodeId+".open");QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(nodeId),QString("active"),5000);
        QPointer<PluginSession> companion=engine.findChild<PluginSession*>();QVERIFY(companion);const auto companionGeneration=companion->sessionId();
        QVERIFY(installer.installDirectory(temp.path()+"/python",python));QTRY_COMPARE(installed.size(),2);QVERIFY(installed[1][1].toString().isEmpty());
        PluginSources::RegisteredResourceRegistry sources(temp.path()+"/sources.json");QVERIFY(sources.load(&error));PluginManagerDialog manager(&sources,&window,nullptr,&index,&installer,&engine);
        auto *list=manager.findChild<QListWidget*>("installedPlugins");QVERIFY(list);for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole)==pyId)list->setCurrentRow(i);
        manager.findChild<QPushButton*>("togglePluginEnabled")->click();QVERIFY(index.plugin(pyId).enabled);manager.findChild<QPushButton*>("runPluginCommand")->click();
        const PluginData::Store nodeData(temp.path()+"/data",native.repository),pyData(temp.path()+"/data",python.repository);
        QTRY_COMPARE_WITH_TIMEOUT(pyData.access(pyId,"storage","snapshot").value.toObject()["text"].toString(),QStringLiteral("shared unsaved 😀中\r\n"),5000);
        QTRY_COMPARE_WITH_TIMEOUT(pyData.access(pyId,"storage","processId").value.isDouble(),true,5000);const auto oldPid=pyData.access(pyId,"storage","processId").value.toInteger();ProcessIdentity oldProcess;QVERIFY2(ProcessIdentity::capture(oldPid,&oldProcess,&error),qPrintable(error));
        QCOMPARE(pyData.access(pyId,"storage","snapshot").value,nodeData.access(nodeId,"storage","snapshot").value);QTest::qWait(200);
        editor.setText("new unsaved 😀中\n");manager.findChild<QPushButton*>("reloadPlugin")->click();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(pyId),QString("active"),8000);
        QTRY_COMPARE_WITH_TIMEOUT(oldProcess.state(),ProcessIdentity::State::Exited,3000);QVERIFY(companion);QCOMPARE(companion->sessionId(),companionGeneration);QCOMPARE(companion->state(),QString("active"));
        manager.findChild<QPushButton*>("runPluginCommand")->click();QTRY_COMPARE_WITH_TIMEOUT(pyData.access(pyId,"storage","snapshot").value.toObject()["text"].toString(),QStringLiteral("new unsaved 😀中\n"),5000);
        QTRY_VERIFY_WITH_TIMEOUT(pyData.access(pyId,"storage","processId").value.toInteger()!=oldPid,5000);QCOMPARE(engine.sessionCount(),2);QCOMPARE(engine.resourceCount(),2);
        manager.findChild<QPushButton*>("togglePluginEnabled")->click();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(pyId),QString("disabled"),5000);QCOMPARE(companion->state(),QString("active"));QVERIFY(errors.isEmpty());
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.sessionCount(),0);documents.detach(doc);window.takeCentralWidget();
    }
    void collectionPreservesRunningSession() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto input=temp.path()+"/input",store=temp.path()+"/store",path=temp.path()+"/installed.json",id=QString("tests.collection");
        const auto resource=runtimeFixture(input,id,"exports.activate=async ctx=>{await ctx.commands.register('tests.collection.open',()=>null);};\n");
        QString error;InstalledPluginRegistry index(path,store);QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(input,resource));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,5000);QVERIFY(installed[0][1].toString().isEmpty());
        const auto root=index.plugin(id).package.root;QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),5000);
        // Another registry owner removes the durable reference while this
        // application still has the old activation. Its lifetime lease, not
        // this GUI's stale snapshot, must keep the executing code in place.
        InstalledPluginRegistry other(path,store);QVERIFY(other.load(&error));QVERIFY(other.remove(id,&error));
        auto result=PackageStore(store).collect(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.packages,0);QVERIFY(QFileInfo::exists(root));QCOMPARE(engine.pluginState(id),QString("active"));
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{PackageStore(store).collect(path);return !QFileInfo::exists(root);})(),5000);
        QVERIFY(errors.isEmpty());documents.detach(doc);window.takeCentralWidget();
    }
    void persistentFailureDiagnostics() {
        using namespace PluginPackages;using namespace PluginRuntime;
        for(const int scenario:{0,1,2}) {
            QTemporaryDir temp;const auto id=QString("tests.diagnostics"),input=temp.path()+"/input";
            const QByteArray code=scenario==0?QByteArray("exports.activate=()=>{throw new Error('activation diagnostic fixture');};\n"):
                (scenario==1?QByteArray("exports.activate=()=>{process.stdout.write('broken protocol\\r\\n\\r\\n');};\n"):QByteArray("exports.activate=async ctx=>{await ctx.commands.register('tests.diagnostics.open',()=>null);};\n"));
            const auto resource=runtimeFixture(input,id,code);QString error;
            InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
            QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
            {
                PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished);
                QVERIFY(installer.installDirectory(input,resource));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,5000);QVERIFY(installed[0][1].toString().isEmpty());
                if(scenario==2){QFile file(index.plugin(id).package.root+"/dist/index.js");QVERIFY(file.open(QIODevice::Append));QVERIFY(file.write("tampered")>0);file.close();}
                QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("failed"),5000);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
                const auto category=scenario==0?PluginDiagnostics::Failure::ActivationFailed:(scenario==1?PluginDiagnostics::Failure::ProtocolFault:PluginDiagnostics::Failure::PackageInvalid);
                QCOMPARE(engine.pluginFailureCategory(id),PluginDiagnostics::label(category));QVERIFY(!engine.pluginFailureMessage(id).isEmpty());
                const auto snapshot=PluginDiagnostics::Store(temp.path()+"/diagnostics").read();QVERIFY2(snapshot.error.isEmpty(),qPrintable(snapshot.error));QCOMPARE(snapshot.failures.size(),1);QCOMPARE(snapshot.failures[0].toObject()["category"].toString(),PluginDiagnostics::category(category));
                if(scenario!=2)QVERIFY(!snapshot.failures[0].toObject()["sessionId"].toString().isEmpty());
                PluginSources::RegisteredResourceRegistry sources(temp.path()+"/sources.json");QVERIFY(sources.load(&error));
                PluginManagerDialog manager(&sources,&window,nullptr,&index,&installer,&engine);auto *details=manager.findChild<QTextBrowser*>("installedPluginDetails");QVERIFY(details);QVERIFY(details->toPlainText().contains(PluginDiagnostics::label(category)));
                bool inspected=false;QTimer::singleShot(0,&manager,[&]{auto *text=manager.findChild<QPlainTextEdit*>("pluginLogText");if(!text)return;inspected=text->toPlainText().contains(PluginDiagnostics::category(category));if(auto *dialog=qobject_cast<QDialog*>(text->parent()))dialog->reject();});
                auto *logs=manager.findChild<QPushButton*>("pluginLogs");QVERIFY(logs);logs->click();QVERIFY(inspected);
                engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
            }
            QMainWindow reopened;
            PluginEngine restarted(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});restarted.attachWindow(&reopened,reopened.menuBar()->addMenu("Plugins"));
            QTRY_VERIFY_WITH_TIMEOUT(restarted.pluginLog(id).contains("Previous recorded failure"),3000);QVERIFY(restarted.pluginLog(id).contains("[failure]"));QCOMPARE(restarted.sessionCount(),0);
            restarted.shutdown();QTRY_VERIFY_WITH_TIMEOUT(restarted.isIdle(),5000);documents.detach(doc);window.takeCentralWidget();
        }
    }
    void largeCommandFailureIsBounded() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto id=QString("tests.large-error"),input=temp.path()+"/input";
        const auto resource=runtimeFixture(input,id,"exports.activate=async ctx=>{await ctx.commands.register('tests.large-error.open',async()=>{let failure;try{await ctx.documents.getSnapshot('closed');}catch(error){failure=error;}failure.message='large fault '+ '😀'.repeat(1000000);throw failure;});};\n",{"commands@1","documents.read@1"});
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("PRIVATE BUFFER IS NOT A LOG RECORD");window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(input,resource));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,5000);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,5000);const auto message=errors[0][1].toString();QVERIFY(message.startsWith("large fault"));QVERIFY(message.size()<=2064);QVERIFY(message.endsWith("[truncated]"));QVERIFY(!message.contains(QChar(0xfffd)));QCOMPARE(engine.pluginState(id),QString("active"));
        PluginDiagnostics::Snapshot snapshot;QTRY_VERIFY_WITH_TIMEOUT(([&]{snapshot=PluginDiagnostics::Store(temp.path()+"/diagnostics").read();return snapshot.error.isEmpty()&&snapshot.failures.size()==1;})(),5000);
        QCOMPARE(snapshot.failures[0].toObject()["category"].toString(),QString("command-failed"));QVERIFY(!QJsonDocument(snapshot.events).toJson().contains("PRIVATE BUFFER IS NOT A LOG RECORD"));
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);documents.detach(doc);window.takeCentralWidget();
    }
    void retryWhileFailedProcessDrains() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto id=QString("tests.retry"),input=temp.path()+"/input",counter=temp.path()+"/counter";
        auto quoted=QJsonDocument(QJsonArray{counter}).toJson(QJsonDocument::Compact);quoted=quoted.mid(1,quoted.size()-2);
        const auto resource=runtimeFixture(input,id,"const fs=require('node:fs');const counter="+quoted+";exports.activate=async ctx=>{if(!fs.existsSync(counter)){fs.writeFileSync(counter,'first');throw new Error('first activation fails');}await ctx.commands.register('tests.retry.open',()=>null);};\n");
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);bool retried=false;
        connect(&engine,&PluginEngine::error,this,[&](const QString &,const QString &){if(!retried){retried=true;engine.invoke(id,id+".open");}});
        QVERIFY(installer.installDirectory(input,resource));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,5000);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_VERIFY_WITH_TIMEOUT(retried,5000);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),5000);QCOMPARE(errors.size(),1);QCOMPARE(engine.sessionCount(),1);QCOMPARE(engine.resourceCount(),1);QVERIFY(engine.pluginFailureMessage(id).isEmpty());
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);documents.detach(doc);window.takeCentralWidget();
    }
    void compatiblePersistenceLifecycle() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto root=temp.path()+"/plugin",id=QString("tests.compat-state");
        auto resource=runtimeFixture(root,id,R"JS(
const vscode=require('vscode');
exports.activate=async context=>{
  const initialGlobal=context.globalState.get('count',0),initialWorkspace=context.workspaceState.get('count',0);
  const settings=vscode.workspace.getConfiguration('state');
  if(!['light','dark'].includes(settings.get('theme')))throw new Error('configuration was not hydrated');
  let changes=0;context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event=>{if(event.affectsConfiguration('state.theme')&&!event.affectsConfiguration('other'))++changes;}));
  for(const write of [()=>context.globalState.update('activation',1),()=>settings.update('theme','dark',vscode.ConfigurationTarget.Global)]){
    let denied=false;try{await write();}catch(error){denied=error.code===1004;}if(!denied)throw new Error('activation persistence write was allowed');
  }
  context.subscriptions.push(vscode.commands.registerCommand('state.run',async()=>{
    if(context.globalState.get('count',0)!==initialGlobal||context.workspaceState.get('count',0)!==initialWorkspace)throw new Error('activation cache was not stable');
    await context.globalState.update('count',initialGlobal+1);await context.workspaceState.update('count',initialWorkspace+1);
    await context.globalState.update('__proto__',null);if(context.globalState.get('__proto__','fallback')!==null)throw new Error('null was lost');
    await context.globalState.update('temporary',true);await context.globalState.update('temporary',undefined);if(context.globalState.keys().includes('temporary'))throw new Error('deletion was lost');
    const next=settings.get('theme')==='light'?'dark':'light';await settings.update('theme',next,vscode.ConfigurationTarget.Global);
    if(changes!==1||settings.inspect('theme').globalValue!==next)throw new Error('configuration event or inspect failed');
    await context.globalState.update('report',{theme:next,changes,label:'😀中'});return null;
  }));
};
)JS",{"commands@1","documents.read@1","documents.delta@1","storage@1","configuration@1"});
        resource.metadata.insert("compatibility",QJsonObject{{"kind","vscode"},{"version","0.1.0"},{"api","1.100.0"},{"extensionRoot","dist"},{"commands",QJsonArray{QJsonObject{{"id","state.run"},{"target",id+".open"}}}}});
        QFile metadata(root+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::WriteOnly));metadata.write(QJsonDocument(resource.metadata).toJson());metadata.close();
        QFile declaration(root+"/dist/vscode-package.json");QVERIFY(declaration.open(QIODevice::WriteOnly));declaration.write(R"JSON({"contributes":{"configuration":{"title":"State fixture","properties":{"state.theme":{"type":"string","default":"light","enum":["light","dark"]}}}}})JSON");declaration.close();
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy installed(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error);int finished=0;QPointer<PluginSession> observed;
        connect(&engine,&PluginEngine::statusChanged,this,[&](const QString &){auto *session=engine.findChild<PluginSession*>();if(!session||session==observed)return;observed=session;connect(session,&PluginSession::commandFinished,this,[&](const QString &,const QJsonValue &,const QJsonObject &){++finished;});});
        const PluginData::Store data(temp.path()+"/data",resource.repository);const auto values=[&](const QString &space,const QString &key){return data.access(id,space,key).value.toObject()["values"].toObject();};
        const auto assertCount=[&](int count){QCOMPARE(values("storage","_vscode.globalState.v1")["count"].toInt(),count);QCOMPARE(values("storage","_vscode.workspaceState.mainSession.v1")["count"].toInt(),count);QVERIFY(!values("storage","_vscode.globalState.v1").contains("activation"));QCOMPARE(values("storage","_vscode.globalState.v1")["report"].toObject()["label"].toString(),QString("😀中"));};
        const auto install=[&](int installCount,int commandCount){QVERIFY(installer.installDirectory(root,resource));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),installCount,10000);QVERIFY2(installed.last()[1].toString().isEmpty(),qPrintable(installed.last()[1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");QTRY_VERIFY_WITH_TIMEOUT(finished==commandCount||!errors.isEmpty(),5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(finished,commandCount);};
        install(1,1);assertCount(1);QCOMPARE(values("configuration","_vscode.configuration.v1")["state.theme"].toString(),QString("dark"));
        engine.reload(id);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),5000);engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(finished,2,5000);assertCount(2);QCOMPARE(values("configuration","_vscode.configuration.v1")["state.theme"].toString(),QString("light"));
        engine.uninstall(id,true);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(index.plugins().size(),0);assertCount(2);
        install(2,3);assertCount(3);engine.uninstall(id,false);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(!QFileInfo::exists(data.filePath(id)));
        install(3,4);assertCount(1);engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY2(errors.isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(engine.resourceCount(),0);
        documents.detach(doc);window.takeCentralWidget();
    }
    void pluginDataLifecycle() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto root=temp.path()+"/plugin",id=QString("tests.data");
        const auto resource=runtimeFixture(root,id,R"JS(
exports.activate=async ctx=>{
  const read=await ctx.storage.get('count');
  let denied=false;try{await ctx.storage.update('activation',1);}catch(e){denied=e.code===1004;}
  if(!denied)throw new Error('activation data write was allowed');
  await ctx.commands.register('tests.data.open',async()=>{
    await ctx.configuration.update('theme',{name:'dark',label:'😀中'});
    await ctx.storage.update('temporary',true);await ctx.storage.update('temporary',undefined);
    if(await ctx.storage.get('temporary')!==null)throw new Error('key deletion failed');
    if(await ctx.storage.get('theme')!==null)throw new Error('configuration escaped namespace');
    await ctx.storage.update('count',(read??0)+1);return null;
  });
};
)JS",{"commands@1","storage@1","configuration@1"});
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy done(&installer,&InstallationService::finished),errors(&engine,&PluginEngine::error),operations(&engine,&PluginController::operationFinished);
        const PluginData::Store data(temp.path()+"/data",resource.repository);
        QList<QJsonObject> replies;QSet<QString> observedSessions;QObject commandObserver;
        connect(&engine,&PluginController::statusChanged,&commandObserver,[&](const QString &){
            for(auto *session:engine.findChildren<PluginSession*>()) {
                if(observedSessions.contains(session->sessionId()))continue;
                observedSessions.insert(session->sessionId());
                connect(session,&PluginSession::commandFinished,&commandObserver,[&](const QString &command,const QJsonValue &,const QJsonObject &failure){if(command==id+".open")replies.append(failure);});
            }
        });
        // A visible write can precede its command acknowledgement. Polling the
        // file while another RPC write commits also blocks Windows replacement.
        // Await the public command/lifecycle signals before inspecting storage.
        const auto install=[&](int count){const auto before=replies.size();QVERIFY(installer.installDirectory(root,resource));QTRY_COMPARE_WITH_TIMEOUT(done.size(),count,10000);QVERIFY(done.last()[1].toString().isEmpty());QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(replies.size(),before+1,10000);QVERIFY2(replies.last().isEmpty(),qPrintable(engine.pluginLog(id)));};
        install(1);QCOMPARE(data.access(id,"storage","count").value,QJsonValue(1));QVERIFY(data.access(id,"storage","activation").value.isNull());
        const auto beforeReload=operations.size();engine.reload(id);QTRY_COMPARE_WITH_TIMEOUT(operations.size(),beforeReload+1,10000);QVERIFY2(operations.last()[1].toString().isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(engine.pluginState(id),QString("active"));
        const auto beforeCommand=replies.size();engine.invoke(id,id+".open");QTRY_COMPARE_WITH_TIMEOUT(replies.size(),beforeCommand+1,10000);QVERIFY2(replies.last().isEmpty(),qPrintable(engine.pluginLog(id)));QCOMPARE(data.access(id,"storage","count").value,QJsonValue(2));
        engine.uninstall(id,true);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(index.plugins().size(),0);QCOMPARE(data.access(id,"storage","count").value,QJsonValue(2));
        install(2);QCOMPARE(data.access(id,"storage","count").value,QJsonValue(3));
        engine.uninstall(id,false);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(index.plugins().size(),0);QVERIFY(!QFileInfo::exists(data.filePath(id)));QVERIFY(data.access(id,"configuration","theme").value.isNull());
        install(3);QCOMPARE(data.access(id,"storage","count").value,QJsonValue(1));
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(errors.isEmpty());for(const auto &row:operations)QVERIFY(row[1].toString().isEmpty());
        documents.detach(doc);window.takeCentralWidget();
    }
    void activationResourceStaging() {
        QTemporaryDir temp;QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("original");window.setCentralWidget(&editor);window.show();
        PluginData::Service data(temp.path()+"/data");PluginData::Store store(temp.path()+"/data");
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        for(const int scenario:{0,1,2}) {
            const bool succeeds=scenario!=0,held=scenario==2;
            const QString id=held?"tests.staged-held":(succeeds?"tests.staged-good":"tests.staged-bad"),root=temp.path()+'/'+id;
            const auto code="exports.activate=async ctx=>{await ctx.commands.register('"+id.toUtf8()+".open',async()=>{await ctx.storage.update('published',true);return null;});const doc=await ctx.documents.getActive();let denied=false;try{await ctx.documents.applyEdits(doc.documentId,doc.version,[]);}catch(e){denied=e.code===1004;}if(!denied)throw new Error('activation write was allowed');denied=false;try{await ctx.storage.update('activation',true);}catch(e){denied=e.code===1004;}if(!denied)throw new Error('activation storage write was allowed');await ctx.documents.watch(()=>{});const p=await ctx.webviews.create('staged','Staged');await p.reveal();await new Promise(r=>setTimeout(r,500));"+(succeeds?QByteArray():QByteArray("throw new Error('activation failed after resources');"))+"};\n";
            const auto resource=runtimeFixture(root,id,code,{"commands@1","documents.read@1","documents.write@1","documents.events@1","webviews@1","storage@1"});
            PluginRuntime::PluginSession session(resource.metadata,root,PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,&window,&documents,nullptr,held,&data);QSignalSpy failed(&session,&PluginRuntime::PluginSession::failed),stopped(&session,&PluginRuntime::PluginSession::stopped),invoked(&session,&PluginRuntime::PluginSession::commandFinished);
            if(held)session.invoke(id+".open");else session.activate();
            QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QDockWidget*>().size(),1,5000);QCOMPARE(session.state(),QString("activating"));QVERIFY(!window.findChild<QDockWidget*>()->isVisible());QCOMPARE(editor.textRange(0,editor.length()),QByteArray("original"));
            if(succeeds){
                QTRY_COMPARE_WITH_TIMEOUT(session.state(),QString("active"),5000);
                if(held){QVERIFY(!window.findChild<QDockWidget*>()->isVisible());QVERIFY(invoked.isEmpty());QVERIFY(store.access(id,"storage","published").value.isNull());session.publishResources();QTRY_COMPARE_WITH_TIMEOUT(invoked.size(),1,5000);QVERIFY(invoked[0][2].toJsonObject().isEmpty());QCOMPARE(store.access(id,"storage","published").value,QJsonValue(true));}
                QVERIFY(window.findChild<QDockWidget*>()->isVisible());session.stop();
            }
            else {QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,5000);QCOMPARE(session.failureCategory(),PluginDiagnostics::Failure::ActivationFailed);}
            QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000);QCOMPARE(session.resourceCount(),0);QTRY_VERIFY(window.findChildren<QDockWidget*>().isEmpty());QVERIFY(store.access(id,"storage","activation").value.isNull());
        }
        documents.detach(doc);window.takeCentralWidget();
    }
    void healthTimeout() {
        QTemporaryDir temp;const auto root=temp.path()+"/plugin";
        const auto resource=runtimeFixture(root,"tests.health","exports.activate = async ctx => { await ctx.commands.register('tests.health.open', () => null); setTimeout(() => { while (true) {} }, 50); };\n");
        QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("untouched");window.setCentralWidget(&editor);
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginRuntime::PluginSession session(resource.metadata,root,PLUGIN_TEST_NODE,PLUGIN_TEST_HOST,&window,&documents);
        QSignalSpy failed(&session,&PluginRuntime::PluginSession::failed),stopped(&session,&PluginRuntime::PluginSession::stopped);
        int ticks=0;QTimer gui;gui.setInterval(20);connect(&gui,&QTimer::timeout,this,[&]{++ticks;});gui.start();
        session.activate();QTRY_COMPARE_WITH_TIMEOUT(session.state(),QString("active"),5000);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,16000);QVERIFY(failed[0][0].toString().contains("health check"));QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,3000);
        QCOMPARE(session.failureCategory(),PluginDiagnostics::Failure::HealthTimeout);
        QCOMPARE(session.resourceCount(),0);QVERIFY(ticks>100);QCOMPARE(editor.textRange(0,editor.length()),QByteArray("untouched"));documents.detach(doc);window.takeCentralWidget();
    }
    void boundedCrashRecovery() {
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;const auto root=temp.path()+"/recover";const auto counter=temp.path()+"/counter.json";
        auto quoted=QJsonDocument(QJsonArray{counter}).toJson(QJsonDocument::Compact);quoted=quoted.mid(1,quoted.size()-2);
        const auto recover=runtimeFixture(root,"tests.recover","const fs=require('node:fs');const file="+quoted+";exports.activate=async ctx=>{const n=fs.existsSync(file)?JSON.parse(fs.readFileSync(file,'utf8')):0;fs.writeFileSync(file,JSON.stringify(n+1));await ctx.commands.register('tests.recover.open',()=>{if(n!==0)return null;setTimeout(()=>process.exit(12),50);return new Promise(()=>{});});};\n");
        const auto repeatedRoot=temp.path()+"/repeated";const auto repeated=runtimeFixture(repeatedRoot,"tests.repeated","exports.activate=async ctx=>{await ctx.commands.register('tests.repeated.open',()=>null);setTimeout(()=>process.exit(12),500);};\n");
        QString error;InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));QSignalSpy done(&installer,&InstallationService::finished),failures(&engine,&PluginEngine::error);
        QVERIFY(installer.installDirectory(root,recover));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done[0][1].toString().isEmpty());QVERIFY(engine.setEnabled(recover.id,true,&error));engine.invoke(recover.id,recover.id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(failures.size(),1,5000);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(recover.id),QString("active"),6000);QCOMPARE(engine.sessionCount(),1);
        QFile count(counter);QVERIFY(count.open(QIODevice::ReadOnly));QCOMPARE(count.readAll(),QByteArray("2"));QVERIFY(engine.pluginLog(recover.id).contains("Automatic recovery in 1000 ms"));
        QVERIFY(engine.setEnabled(recover.id,false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
        QVERIFY(installer.installDirectory(repeatedRoot,repeated));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QVERIFY(done[1][1].toString().isEmpty());QVERIFY(engine.setEnabled(repeated.id,true,&error));engine.invoke(repeated.id,repeated.id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(failures.size(),5,16000);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.pluginState(repeated.id),QString("failed"));QCOMPARE(engine.resourceCount(),0);
        QCOMPARE(engine.pluginFailureCategory(repeated.id),PluginDiagnostics::label(PluginDiagnostics::Failure::ProcessCrash));
        const auto log=engine.pluginLog(repeated.id);QCOMPARE(log.count("Automatic recovery in"),3);QVERIFY(log.contains("stopped after three restarts"));
        // Disabling during backoff revokes the queued restart ticket.
        QVERIFY(engine.setEnabled(repeated.id,false,&error));QVERIFY(engine.setEnabled(repeated.id,true,&error));engine.invoke(repeated.id,repeated.id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(failures.size(),6,5000);QCOMPARE(engine.pluginState(repeated.id),QString("restarting"));QVERIFY(engine.setEnabled(repeated.id,false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
        engine.shutdown();QTest::qWait(1200);QCOMPARE(failures.size(),6);QCOMPARE(engine.sessionCount(),0);documents.detach(doc);window.takeCentralWidget();
    }
    void lifecycleStress100Cycles() {
        using namespace PluginPackages;using namespace PluginSources;using namespace PluginRuntime;
        QTemporaryDir temp;QProcess packer;packer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_STAGER,PLUGIN_TEST_PREVIEW,temp.path()+"/package"});QVERIFY(packer.waitForFinished(10000));QVERIFY(packer.exitCode()==0);
        QFile metadata(QString(PLUGIN_TEST_PREVIEW)+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));SourceSnapshot source;QString error;QVERIFY(PluginMetadata::parse(metadata.readAll(),"https://github.com/tests/contract-preview",{},&source,&error));
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        QMainWindow window;ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("# stress unsaved 😀");window.setCentralWidget(&editor);
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"markdown");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});auto *menu=window.menuBar()->addMenu("Plugins");engine.attachWindow(&window,menu);window.show();
        QSignalSpy done(&installer,&InstallationService::finished),failures(&engine,&PluginEngine::error);QVERIFY(installer.installDirectory(temp.path()+"/package",source.resources[0]));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done[0][1].toString().isEmpty());
        const auto id=source.resources[0].id,command=id+".open";QElapsedTimer duration;duration.start();
        const auto initial=lifecycleNativeSample();QJsonObject warm;QJsonArray samples;int exitedIdentities=0;
        for(int cycle=0;cycle<100;++cycle) {
            QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,command);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);
            QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QWebEngineView*>().size(),1,10000);QTRY_VERIFY_WITH_TIMEOUT(engine.resourceCount()>=3,5000);
            QList<ProcessIdentity> processes;
            for(auto *process:engine.findChildren<PluginRpc::RpcProcess*>())if(process->processId()>0){ProcessIdentity identity;QVERIFY(ProcessIdentity::capture(process->processId(),&identity,&error));processes.append(identity);}
            QVERIFY(!processes.isEmpty());
            engine.reload(id);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);
            QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QWebEngineView*>().size(),1,10000);QTRY_VERIFY_WITH_TIMEOUT(engine.resourceCount()>=3,5000);
            for(auto *process:engine.findChildren<PluginRpc::RpcProcess*>())if(process->processId()>0){ProcessIdentity identity;QVERIFY(ProcessIdentity::capture(process->processId(),&identity,&error));bool known=false;for(const auto &prior:processes)if(prior.pid==identity.pid&&prior.start==identity.start)known=true;if(!known)processes.append(identity);}
            QVERIFY(processes.size()>=2);
            QVERIFY(engine.setEnabled(id,false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
            QCOMPARE(engine.resourceCount(),0);QVERIFY(menu->actions().isEmpty());QTRY_VERIFY(window.findChildren<QWebEngineView*>().isEmpty());QTRY_VERIFY(window.findChildren<QDockWidget*>().isEmpty());QTRY_VERIFY(engine.findChildren<PluginSession*>().isEmpty());
            for(const auto &identity:processes){QTRY_VERIFY_WITH_TIMEOUT(identity.state()==ProcessIdentity::State::Exited,5000);++exitedIdentities;}
            const auto snapshot=lifecycleNativeSample();QCOMPARE(snapshot["focusConnections"],initial["focusConnections"]);if(cycle==2)warm=snapshot;
            samples.append(QJsonObject{{"cycle",cycle+1},{"resources",engine.resourceCount()},{"sessions",engine.sessionCount()},{"ownedNodeIdentitiesExited",processes.size()},{"native",snapshot}});
            if(cycle%10==0)qInfo()<<"lifecycle cycle"<<cycle+1<<"owned resources"<<engine.resourceCount();
            // This verifies ownership across all 100 cycles; shared-runner
            // speed is recorded below. CTest enforces the overall time limit.
        }
        // Let deferred provider cleanup settle; RSS is recorded, not required
        // to equal its allocator/cache baseline. Handles must return after
        // the first three warm-up cycles on the observed native platform.
        QTest::qWait(1000);const auto final=lifecycleNativeSample();
        const auto output=qEnvironmentVariable("NNP_LIFECYCLE_RESOURCE_OUTPUT");
        if(!output.isEmpty()){QSaveFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));const QJsonObject report{{"cycles",100},{"elapsedMs",duration.elapsed()},{"initial",initial},{"warmAfterCycles",3},{"warm",warm},{"final",final},{"exitedNodeIdentities",exitedIdentities},{"samples",samples},{"qualification","Real Scintilla/PluginEngine/WebEngine lifecycle. Focus connection and native handle counts after stop; RSS includes shared Qt/provider caches and is not a peak measurement."}};QVERIFY(file.write(QJsonDocument(report).toJson())>0);QVERIFY(file.commit());}
#ifdef Q_OS_MACOS
        QVERIFY(warm["fileDescriptors"].toInt()>0);QVERIFY(final["fileDescriptors"].toInt()>0);QVERIFY2(final["fileDescriptors"].toInt()<=warm["fileDescriptors"].toInt(),"Native handles did not return to their warm lifecycle baseline; raw counts preserved.");
#endif
        qInfo()<<"100 lifecycle cycles elapsed ms"<<duration.elapsed();QVERIFY2(failures.isEmpty(),qPrintable(engine.pluginLog(id)));
        engine.shutdown();QTRY_VERIFY(engine.isIdle());documents.detach(doc);window.takeCentralWidget();
    }
    void updateDuringInitialActivationDoesNotPreserveHiddenStagedPanels(){
        using namespace PluginPackages;using namespace PluginRuntime;QTemporaryDir temp;QString error;const QString id="tests.activation-update";
        const auto initial=runtimeFixture(temp.path()+"/first",id,"exports.activate=async ctx=>{await ctx.commands.register('tests.activation-update.open',()=>null);const p=await ctx.webviews.create('staged','Staged');await p.reveal();await new Promise(r=>setTimeout(r,2000));};",{"commands@1","webviews@1"});
        auto candidate=runtimeFixture(temp.path()+"/next",id,"exports.activate=async ctx=>{await ctx.commands.register('tests.activation-update.open',()=>null);};",{"commands@1"});candidate.metadata["version"]="1.1.0";QFile file(temp.path()+"/next/notepadnext-plugin.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(candidate.metadata).toJson());file.close();
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());QMainWindow window;PluginDocuments::DocumentBridge documents;PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});engine.attachWindow(&window,window.menuBar()->addMenu("Plugins"));
        QSignalSpy installed(&installer,&InstallationService::finished),operations(&engine,&PluginController::operationFinished);QVERIFY(installer.installDirectory(temp.path()+"/first",initial));QTRY_COMPARE(installed.size(),1);QVERIFY(installed[0][1].toString().isEmpty());QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QWebEngineView*>(),10000);QCOMPARE(engine.pluginState(id),QString("activating"));QVERIFY(installer.updateDirectory(temp.path()+"/next",candidate));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),1,10000);QVERIFY(operations[0][1].toString().isEmpty());QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(engine.pluginState(id),QString("ready"));QVERIFY(index.plugin(id).validationDeferred);QTRY_VERIFY(!window.findChild<QWebEngineView*>());engine.shutdown();
    }
    void serviceEndpointsRequireOwnedLoopbackPaths(){
        QTemporaryDir temp;QMainWindow window;PluginDocuments::DocumentBridge documents;WebviewService service("tests.endpoint",temp.path(),&window,&documents);
        for(const auto &url:QStringList{"https://example.com/index.html","http://localhost:3000/nnp/"+QString(32,'a')+"/index.html","http://127.0.0.1:3000/index.html","http://127.0.0.1:3000/nnp/"+QString(32,'a')+"/index.html?token=x"}) {
            bool done=false;service.createService("test","Test",QUrl(url),QString(32,'b'),"test-session",{},[&](const QJsonObject &,const QJsonObject &error){done=true;QCOMPARE(error["code"].toInt(),-32602);});QVERIFY(done);QCOMPARE(service.panelCount(),0);
        }
    }
    void universalHotUpdatePreservesPanelsAndDoesNotReplayCommands_data(){QTest::addColumn<bool>("service");QTest::addColumn<bool>("sharedService");QTest::newRow("compatible-html")<<false<<false;QTest::newRow("node-http-service")<<true<<false;QTest::newRow("shared-node-service")<<true<<true;}
    void universalHotUpdatePreservesPanelsAndDoesNotReplayCommands() {
        QFETCH(bool,service);QFETCH(bool,sharedService);
        using namespace PluginPackages;using namespace PluginRuntime;
        QTemporaryDir temp;QString error;const QString id="tests.universal-reload";
        const auto makePackage=[&](const QString &version,bool broken=false) {
            const auto root=temp.path()+"/input-"+version;
            QByteArray code=R"JS(exports.activate=async ctx=>{
 const open=async descriptor=>{
   const p=await ctx.webviews.createCompatible({panelId:require('node:crypto').randomBytes(16).toString('hex'),viewType:'counter',title:descriptor?.title??'Counter',enableScripts:true,localResourceRoots:['dist']});
   await p.setDocument('<!doctype html><style nonce="test">body{height:4000px}</style><input id="draft" value="initial"><h1>VERSION</h1><script nonce="test">const api=acquireVsCodeApi();window.restored=api.getState();if(window.restored?.draft)document.getElementById(\"draft\").value=window.restored.draft;</script>');await p.reveal(true);
 };
 ctx.subscriptions.push(ctx.webviews.registerRestorer('counter',open));
 await ctx.commands.register('tests.universal-reload.open',async()=>{await ctx.storage.update('runs',(await ctx.storage.get('runs')??0)+1);await open();await open();});
};)JS";
            if(service){
                code.replace("const p=await ctx.webviews.createCompatible({panelId:require('node:crypto').randomBytes(16).toString('hex'),viewType:'counter',title:descriptor?.title??'Counter',enableScripts:true,localResourceRoots:['dist']});\n   await p.setDocument(","const html=(");
                code.replace("');await p.reveal(true);", "');const server=await require('./local-service.cjs').startLocalService(ctx,(request,response,file)=>{if(file==='api/pid'){response.setHeader('Content-Type','application/json');response.end(JSON.stringify({pid:process.pid}));}else{response.setHeader('Content-Type','text/html');response.end(html);}});const p=await ctx.webviews.createService({viewType:'counter',title:descriptor?.title??'Counter',endpoint:server.endpoint,nonce:server.nonce});await p.reveal(true);");
            }
            if(sharedService){code.replace("const open=async descriptor=>{","let sharedServer;const open=async descriptor=>{");code.replace("const server=await require(","const server=sharedServer??await require(");code.replace("const p=await ctx.webviews.createService", "sharedServer=server;const p=await ctx.webviews.createService");}
            code.replace("VERSION",version.toUtf8());if(broken)code.replace("registerRestorer('counter',open)","registerRestorer('counter',()=>{throw new Error('intentional restore failure');})");
            auto resource=runtimeFixture(root,id,code,{"commands@1","storage@1","webviews@1","webviews.compat@1","webviews.service@1","webviews.restore@1"});
            if(service&&!QFile::copy(QString(PLUGIN_TEST_HOST).replace("plugin-host/host.cjs","plugin-sdk/local-service.cjs"),root+"/dist/local-service.cjs"))qFatal("service helper copy");resource.metadata["version"]=version;QFile file(root+"/notepadnext-plugin.json");if(!file.open(QIODevice::WriteOnly))qFatal("fixture");file.write(QJsonDocument(resource.metadata).toJson());return qMakePair(root,resource);
        };
        const auto v1=makePackage("1.0.0"),v2=makePackage("1.1.0"),bad=makePackage("1.2.0",true);
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));InstallationService installer(&index,PluginEngine::packagePolicy());
        PluginData::Store store(temp.path()+"/data",v1.second.repository);QMainWindow window;PluginDocuments::DocumentBridge documents;prepareWindow(&window);window.resize(1100,700);window.show();const auto nativeId=window.winId();
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});auto *menu=window.menuBar()->addMenu("Plugins");engine.attachWindow(&window,menu);
        QSignalSpy installed(&installer,&InstallationService::finished),operations(&engine,&PluginController::operationFinished);
        QVERIFY(installer.installDirectory(v1.first,v1.second));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,10000);QVERIFY2(installed[0][1].toString().isEmpty(),qPrintable(installed[0][1].toString()));QVERIFY(engine.setEnabled(id,true,&error));engine.invoke(id,id+".open");
        QTRY_COMPARE_WITH_TIMEOUT(engine.pluginPanelCount(id),2,10000);QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QWebEngineView*>().size(),2,10000);
        const auto docks=window.findChildren<QDockWidget*>();QCOMPARE(docks.size(),2);QPointer<QDockWidget> floating=docks[0],docked=docks[1];floating->setFloating(true);floating->resize(600,500);floating->move(180,160);QTest::qWait(100);const auto geometry=floating->geometry();
        const auto run=[&](QWebEnginePage *page,const QString &script){QVariant result;bool ready=false;page->runJavaScript(script,[&](const QVariant &value){result=value;ready=true;});QElapsedTimer timer;timer.start();while(!ready&&timer.elapsed()<5000)QTest::qWait(10);return result;};
        QTRY_VERIFY_WITH_TIMEOUT(run(floating->findChild<QWebEngineView*>()->page(),"document.querySelector('h1')?.textContent").toString()=="1.0.0",10000);
        QVERIFY(run(floating->findChild<QWebEngineView*>()->page(),"document.getElementById('draft').value='unsaved draft'+'x'.repeat(4500);api.setState({count:37,draft:document.getElementById('draft').value});scrollTo(0,420);true").toBool());
        QTest::qWait(100);const auto oldEndpoint=floating->findChild<QWebEngineView*>()->page()->url();QCOMPARE(store.access(id,"storage","runs").value,QJsonValue(1));
        QVERIFY(installer.updateDirectory(v2.first,v2.second));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),1,15000);QVERIFY2(operations[0][1].toString().isEmpty(),qPrintable(operations[0][1].toString()));
        QVERIFY(floating&&docked);QCOMPARE(window.winId(),nativeId);QVERIFY(floating->isFloating());QCOMPARE(floating->geometry(),geometry);QCOMPARE(window.dockWidgetArea(docked),Qt::RightDockWidgetArea);QCOMPARE(engine.pluginPanelCount(id),2);QCOMPARE(store.access(id,"storage","runs").value,QJsonValue(1));
        auto *page=floating->findChild<QWebEngineView*>()->page();
        if(service){
            QVERIFY(page->url()!=oldEndpoint);QNetworkAccessManager probe;probe.setProxy(QNetworkProxy::NoProxy);QNetworkRequest request(oldEndpoint);request.setTransferTimeout(2000);auto *reply=probe.get(request);QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(),3000);QVERIFY(reply->error()!=QNetworkReply::NoError);
            QVERIFY(page->url().toString().startsWith("http://127.0.0.1:"));QCOMPARE(run(page,"fetch('api/pid').then(r=>r.json()).then(v=>window.servicePid=v.pid);true").toBool(),true);QTRY_VERIFY(run(page,"window.servicePid>0").toBool());}
        QCOMPARE(run(page,"document.querySelector('h1').textContent").toString(),QString("1.1.0"));QCOMPARE(run(page,"document.getElementById('draft').value").toString(),QString("unsaved draft")+QString(4500,'x'));QCOMPARE(run(page,"window.restored.count").toInt(),37);QTRY_COMPARE(run(page,"scrollY").toInt(),420);
        QVERIFY(installer.updateDirectory(bad.first,bad.second));QTRY_VERIFY_WITH_TIMEOUT(operations.size()>=2,15000);QTRY_VERIFY2_WITH_TIMEOUT(engine.pluginState(id)=="active",qPrintable(engine.pluginLog(id)),15000);QCOMPARE(index.plugin(id).package.metadata["version"].toString(),QString("1.1.0"));QVERIFY(floating&&docked);QCOMPARE(floating->geometry(),geometry);QCOMPARE(run(floating->findChild<QWebEngineView*>()->page(),"window.restored.count").toInt(),37);QCOMPARE(store.access(id,"storage","runs").value,QJsonValue(1));
        docked->close();QTRY_COMPARE(engine.pluginPanelCount(id),1);
        if(sharedService){auto *remaining=floating->findChild<QWebEngineView*>()->page();QVERIFY(run(remaining,"window.stillConnected=false;fetch('api/pid').then(r=>r.json()).then(v=>window.stillConnected=v.pid>0);true").toBool());QTRY_VERIFY(run(remaining,"window.stillConnected").toBool());}
        engine.reload(id);QTRY_VERIFY2_WITH_TIMEOUT(engine.pluginState(id)=="active",qPrintable(engine.pluginLog(id)),15000);QCOMPARE(engine.pluginPanelCount(id),1);QVERIFY(floating);QCOMPARE(floating->geometry(),geometry);QCOMPARE(store.access(id,"storage","runs").value,QJsonValue(1));
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!floating);QCOMPARE(window.findChildren<QWebEngineView*>().size(),0);
    }
    void updateValidationAndRollback() {
        using namespace PluginPackages;using namespace PluginSources;using namespace PluginRuntime;
        QTemporaryDir temp;QString error;
        const auto makePackage=[&](const QString &version,const QByteArray &code) {
            const auto root=temp.path()+"/input-"+version;QDir().mkpath(root+"/dist");
            QJsonObject m{{"manifestVersion",1},{"id","tests.update"},{"name","Update Test"},{"version",version},{"list",false},
                {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.1.0"}}},
                {"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.js"}}},
                {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1"}},{"optional",QJsonArray{}}}},
                {"activationEvents",QJsonArray{"onCommand:tests.update.open"}},
                {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id","tests.update.open"},{"title","Open"}}}},
                    {"menus",QJsonArray{QJsonObject{{"command","tests.update.open"},{"location","plugins"}}}}}}};
            const auto write=[&](const QString &name,const QByteArray &bytes){QFile f(root+'/'+name);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())qFatal("Fixture write failed");};
            write("notepadnext-plugin.json",QJsonDocument(m).toJson());write("LICENSE","Test license");write("dist/index.js",code);
            SourceSnapshot source;if(!PluginMetadata::parse(QJsonDocument(m).toJson(),"https://github.com/tests/update",{},&source,&error))qFatal("%s",qPrintable(error));
            return qMakePair(root,source.resources[0]);
        };
        const QByteArray working="exports.activate = async ctx => { await ctx.commands.register('tests.update.open', () => null); };\n";
        const auto v1=makePackage("1.0.0",working),v2=makePackage("1.1.0",working),bad=makePackage("1.2.0","exports.activate = () => { throw new Error('bad update'); };\n");
        InstalledPluginRegistry index(temp.path()+"/installed.json",temp.path()+"/store");QVERIFY(index.load(&error));
        InstallationService installer(&index,PluginEngine::packagePolicy());QMainWindow window;ScintillaEdit editor;window.setCentralWidget(&editor);
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"plaintext");documents.setActive(doc);
        PluginEngine engine(&index,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});auto *menu=window.menuBar()->addMenu("Plugins");engine.attachWindow(&window,menu);window.show();
        QSignalSpy installed(&installer,&InstallationService::finished),operations(&engine,&PluginController::operationFinished);
        QVERIFY(installer.installDirectory(v1.first,v1.second));QTRY_COMPARE_WITH_TIMEOUT(installed.size(),1,10000);QVERIFY(installed[0][1].toString().isEmpty());
        QVERIFY(engine.setEnabled("tests.update",true,&error));engine.invoke("tests.update","tests.update.open");QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState("tests.update"),QString("active"),10000);
        const auto firstDigest=index.plugin("tests.update").package.digest;
        QVERIFY(installer.updateDirectory(v2.first,v2.second));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),1,10000);QVERIFY2(operations[0][1].toString().isEmpty(),qPrintable(operations[0][1].toString()));
        QCOMPARE(index.plugin("tests.update").package.metadata.value("version").toString(),QString("1.1.0"));QVERIFY(index.plugin("tests.update").enabled);QVERIFY(index.plugin("tests.update").previous);QCOMPARE(engine.sessionCount(),1);
        engine.rollback("tests.update");QTRY_COMPARE_WITH_TIMEOUT(operations.size(),2,10000);QVERIFY(operations[1][1].toString().isEmpty());QCOMPARE(index.plugin("tests.update").package.digest,firstDigest);
        QVERIFY(installer.updateDirectory(bad.first,bad.second));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),3,10000);QVERIFY(operations[2][1].toString().contains("previous version was restored"));
        QCOMPARE(index.plugin("tests.update").package.digest,firstDigest);QVERIFY(index.plugin("tests.update").enabled);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState("tests.update"),QString("active"),10000);QCOMPARE(engine.sessionCount(),1);
        QVERIFY(engine.setEnabled("tests.update",false,&error));QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);
        const auto v4=makePackage("1.4.0",working);QVERIFY(installer.updateDirectory(v4.first,v4.second));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),4,10000);QVERIFY(operations[3][1].toString().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(!index.plugin("tests.update").enabled);QVERIFY(menu->actions().isEmpty());QCOMPARE(index.plugin("tests.update").package.metadata.value("version").toString(),QString("1.4.0"));QVERIFY(index.plugin("tests.update").validationDeferred);QCOMPARE(engine.sessionCount(),0);
        engine.rollback("tests.update");QTRY_COMPARE_WITH_TIMEOUT(operations.size(),5,10000);QVERIFY(operations[4][1].toString().isEmpty());QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(index.plugin("tests.update").package.digest,firstDigest);
        const auto missing=makePackage("1.5.0","exports.activate = () => {};\n");QVERIFY(installer.updateDirectory(missing.first,missing.second));QTRY_COMPARE_WITH_TIMEOUT(operations.size(),6,10000);QVERIFY(operations[5][1].toString().isEmpty());QVERIFY(index.plugin("tests.update").validationDeferred);QCOMPARE(engine.sessionCount(),0);
        QVERIFY(engine.setEnabled("tests.update",true,&error));engine.invoke("tests.update","tests.update.open");QTRY_COMPARE_WITH_TIMEOUT(operations.size(),7,10000);QVERIFY(!operations[6][1].toString().isEmpty());
        QCOMPARE(index.plugin("tests.update").package.digest,firstDigest);QVERIFY(index.plugin("tests.update").enabled);QVERIFY(!index.plugin("tests.update").updatePending);QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState("tests.update"),QString("active"),10000);QVERIFY(window.findChildren<QWebEngineView*>().isEmpty());
        engine.shutdown();QTRY_VERIFY(engine.isIdle());documents.detach(doc);window.takeCentralWidget();
    }
    void installedManagerLifecycle() {
        using namespace PluginPackages;using namespace PluginSources;using namespace PluginRuntime;
        QVERIFY(QFileInfo(PLUGIN_TEST_NODE).isExecutable());QTemporaryDir temp;
        QProcess packer;packer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_STAGER,PLUGIN_TEST_PREVIEW,temp.path()+"/release"});
        QVERIFY(packer.waitForFinished(10000));QVERIFY2(packer.exitCode()==0,packer.readAllStandardError().constData());
        RegisteredResourceRegistry sources(temp.path()+"/sources.json");InstalledPluginRegistry installed(temp.path()+"/installed.json",temp.path()+"/store");QString error;
        QVERIFY(sources.load(&error));QVERIFY(installed.load(&error));SourceSnapshot snapshot;QFile metadata(QString(PLUGIN_TEST_PREVIEW)+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));
        QVERIFY(PluginMetadata::parse(metadata.readAll(),"https://github.com/tests/contract-preview",{},&snapshot,&error));snapshot.commit=QString(40,'a');QVERIFY(sources.registerSource(snapshot,&error));
        InstallationService installer(&installed,PluginEngine::packagePolicy());
        QMainWindow window;window.resize(900,600);ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("installed unsaved 😀中");window.setCentralWidget(&editor);
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"markdown");documents.setActive(doc);
        PluginEngine engine(&installed,&installer,&documents,{PLUGIN_TEST_NODE,PLUGIN_TEST_HOST});auto *menu=window.menuBar()->addMenu("Plugins");engine.attachWindow(&window,menu);QSignalSpy errors(&engine,&PluginEngine::error);
        PluginManagerDialog manager(&sources,&window,nullptr,&installed,&installer,&engine);
        auto *available=manager.findChild<QListWidget*>("discoverPlugins"),*list=manager.findChild<QListWidget*>("installedPlugins");QCOMPARE(available->count(),1);QCOMPARE(list->count(),0);QVERIFY(menu->actions().isEmpty());
        QSignalSpy complete(&installer,&InstallationService::finished);QVERIFY(installer.installDirectory(temp.path()+"/release",sources.resources()[0]));
        QTRY_COMPARE_WITH_TIMEOUT(complete.size(),1,10000);QVERIFY2(complete[0][1].toString().isEmpty(),qPrintable(complete[0][1].toString()));
        QCOMPARE(available->count(),1);QCOMPARE(list->count(),1);QVERIFY(!installed.plugins()[0].enabled);QCOMPARE(engine.sessionCount(),0);QVERIFY(menu->actions().isEmpty());
        const auto id=snapshot.resources[0].id;auto *enable=manager.findChild<QPushButton*>("togglePluginEnabled");QVERIFY(enable->isEnabled());enable->click();
        QTRY_COMPARE(menu->actions().size(),1);QVERIFY(installed.plugin(id).enabled);QCOMPARE(engine.sessionCount(),0);
        const auto trigger=[&]{QVERIFY(menu->actions()[0]->menu());menu->actions()[0]->menu()->actions()[0]->trigger();};
        window.show();trigger();trigger();
        QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QCOMPARE(engine.sessionCount(),1);
        QTRY_COMPARE_WITH_TIMEOUT(window.findChildren<QWebEngineView*>().size(),1,10000);auto *page=window.findChild<QWebEngineView*>()->page();QString heading;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("document.querySelector('h1')?.textContent",[&](const QVariant &v){heading=v.toString();});return heading==QStringLiteral("installed unsaved 😀中");})(),10000);
        QCOMPARE(engine.pluginPanelCount(id),1);
        manager.findChild<QPushButton*>("inspectInstalledPlugin")->click();
        auto *details=manager.findChild<QTextBrowser*>("installedPluginDetails");QVERIFY(details->toPlainText().contains("Runtime / entry"));QVERIFY(details->toPlainText().contains("dist/index.js"));QVERIFY(details->toPlainText().contains("Recent activity"));
        manager.findChild<QPushButton*>("pluginOverview")->click();QVERIFY(details->toPlainText().contains("Update policy"));QVERIFY(!details->toPlainText().contains(installed.plugin(id).package.digest));
        enable->click();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(!installed.plugin(id).enabled);QVERIFY(menu->actions().isEmpty());QVERIFY(window.findChildren<QWebEngineView*>().isEmpty());
        QCOMPARE(engine.pluginPanelCount(id),0);
        enable->click();QTRY_COMPARE(menu->actions().size(),1);trigger();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);
        manager.findChild<QPushButton*>("reloadPlugin")->click();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);QCOMPARE(engine.sessionCount(),1);
        const auto installedRoot=installed.plugin(id).package.root;
        bool confirmed=false;
        QTimer::singleShot(0,&manager,[&]{auto *confirm=manager.findChild<QDialog*>("uninstallPluginReview");if(!confirm)return;auto *data=confirm->findChild<QCheckBox*>("deletePluginData");confirmed=data&&!data->isChecked();confirm->done(confirmed?QDialog::Accepted:QDialog::Rejected);});
        manager.findChild<QPushButton*>("uninstallPlugin")->click();QVERIFY(confirmed);QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QCOMPARE(installed.plugins().size(),0);QCOMPARE(list->count(),0);QCOMPARE(available->count(),1);QVERIFY(menu->actions().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(installedRoot),5000);
        // Reinstallation stays disabled; removing the source preserves it.
        QVERIFY(installer.installDirectory(temp.path()+"/release",sources.resources()[0]));QTRY_COMPARE_WITH_TIMEOUT(complete.size(),2,10000);QVERIFY(complete[1][1].toString().isEmpty());QVERIFY(sources.removeSource(snapshot.key(),&error));QCOMPARE(installed.plugins().size(),1);
        QVERIFY(engine.setEnabled(id,true,&error));trigger();QTRY_COMPARE_WITH_TIMEOUT(engine.pluginState(id),QString("active"),10000);
        engine.shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine.isIdle(),5000);QVERIFY(installed.plugin(id).enabled);
        QStringList messages;for(const auto &row:errors)messages.append(row[1].toString());QVERIFY2(errors.isEmpty(),qPrintable(messages.join("; ")));
        documents.detach(doc);window.takeCentralWidget();
    }
    void htmlPolicy() {
        QString safe,error;
        QVERIFY(HtmlPolicy::sanitize("<h1>😀中</h1><p>&lt;script&gt;</p><pre><code class=\"language-js\">a &amp; b</code></pre>",{},&safe,&error));
        QVERIFY(safe.contains("😀中"));
        for(const auto &html:{"<script>alert(1)</script>","<p onclick=\"bad()\">x</p>","<iframe src=\"https://example.com\"/>","<svg/>","<img src=\"file:///tmp/a.png\"/>","<style>body{display:none}</style>","<!DOCTYPE x [<!ENTITY e SYSTEM 'file:///etc/passwd'>]><p>&e;</p>","<p>unclosed","<p xmlns=\"https://example.com\">x</p>"}) QVERIFY2(!HtmlPolicy::sanitize(html,{},&safe,&error),html);
        QVERIFY(HtmlPolicy::sanitize("<a href=\"javascript:alert(1)\">text</a>",{},&safe,&error)); QVERIFY(!safe.contains("href"));
        const QHash<QString,QUrl> images{{"nnp-resource:test",QUrl("nnp-plugin://owner/p/image")}};
        QVERIFY(HtmlPolicy::sanitize("<img src=\"nnp-resource:test\" alt=\"image\"/>",images,&safe,&error)); QVERIFY(safe.contains("nnp-plugin://owner/p/image"));
    }
    void imageContainment() {
        QTemporaryDir root,outside; QVERIFY(root.isValid()); QVERIFY(outside.isValid()); QImage image(10,10,QImage::Format_ARGB32); image.fill(Qt::green);
        QVERIFY(image.save(root.filePath("inside.png"))); QVERIFY(image.save(outside.filePath("outside.png")));
        QVERIFY(PluginTest::link(outside.filePath("outside.png"),root.filePath("escape.png")));
        QByteArray bytes,mime; QString error; QVERIFY(HtmlPolicy::readImage(root.path(),"inside.png",&bytes,&mime,&error)); QCOMPARE(mime,QByteArray("image/png"));
        for(const auto &path:{"../outside.png","/etc/passwd","escape.png","file:///tmp/x.png","sub\\x.png","missing.png"}) QVERIFY(!HtmlPolicy::readImage(root.path(),path,&bytes,&mime,&error));
        QFile fake(root.filePath("fake.png")); QVERIFY(fake.open(QIODevice::WriteOnly)); fake.write("<svg/>"); fake.close(); QVERIFY(!HtmlPolicy::readImage(root.path(),"fake.png",&bytes,&mime,&error));
    }
    void realPageAndCleanup() {
        QTemporaryDir root; QMainWindow window; window.resize(900,600); ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); window.setCentralWidget(&editor);
        PluginDocuments::DocumentBridge documents; const auto doc=documents.attach(&editor,{},"markdown"); documents.setActive(doc); editor.setText("# unsaved");
        WebviewService service("test.preview",root.path(),&window,&documents); QJsonObject error;
        const auto panel=service.create("preview","Markdown Preview","right",&error)["panelId"].toString(); QVERIFY(error.isEmpty()); QCOMPARE(service.panelCount(),1);
        QCOMPARE(service.create("preview","Markdown Preview","right",&error)["panelId"].toString(),panel); QCOMPARE(service.panelCount(),1);
        auto *page=service.page(panel); QVERIFY(page->profile()->isOffTheRecord()); QSignalSpy loaded(page,&QWebEnginePage::loadFinished), events(&service,&WebviewService::event);
        QJsonObject context{{"documentId",doc},{"version",documents.info(doc)["version"]}};
        QVERIFY(service.setHtml(panel,"<h1>unsaved 😀中</h1><p>live buffer</p>",context,&error)); QVERIFY(service.reveal(panel,true,&error)); window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000); QVERIFY(loaded.last()[0].toBool()); QCOMPARE(service.pageUrl(panel).scheme(),QString("nnp-plugin"));
        QString heading; page->runJavaScript("document.querySelector('h1').textContent",[&heading](const QVariant &value){heading=value.toString();}); QTRY_COMPARE(heading,QStringLiteral("unsaved 😀中"));
        bool bridgeReady=false;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("typeof window.notepadnext==='object'",[&bridgeReady](const QVariant &value){bridgeReady=value.toBool();});return bridgeReady;})(),5000);
        page->runJavaScript("window.notepadnext.postMessage({kind:'ready',text:'😀'});"); QTRY_VERIFY(!events.isEmpty()); QCOMPARE(events.last()[1].toString(),QString("message"));
        QVERIFY(service.postMessage(panel,QJsonObject{{"text",QStringLiteral("😀中")}},&error));
        const auto current=service.pageUrl(panel); editor.addText(1,"x"); QVERIFY(!service.setHtml(panel,"<p>stale</p>",context,&error)); QCOMPARE(error["code"].toInt(),1001); QCOMPARE(service.pageUrl(panel),current);
        documents.detach(doc); QVERIFY(!service.setHtml(panel,"<p>closed</p>",context,&error)); QCOMPARE(error["code"].toInt(),1002);
        // A large, line-bounded code block exercises scheme transport without
        // turning millions of glyphs into one pathological unbroken word.
        const auto large=QString("code sample with spaces 1234567890\n").repeated(80000);
        QVERIFY(large.toUtf8().size()>2*1024*1024);
        loaded.clear(); QVERIFY(service.setHtml(panel,"<pre><code>"+large+"</code></pre>",{{"documentId",QJsonValue(QJsonValue::Null)},{"version",QJsonValue(QJsonValue::Null)}},&error));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000); QVERIFY(loaded.last()[0].toBool());
        service.dispose(panel); QCOMPARE(service.panelCount(),0); QCOMPARE(service.resourceCount(),0); QVERIFY(!service.page(panel));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QVERIFY(window.findChildren<QDockWidget*>().isEmpty());
        window.takeCentralWidget(); // stack-owned ScintillaEdit
    }
    void nativeLongPageScrollsToOffscreenText() {
        QTemporaryDir root;QMainWindow window;window.resize(900,600);PluginDocuments::DocumentBridge documents;
        WebviewService service("test.long-page",root.path(),&window,&documents);QJsonObject error;
        const auto panel=service.create("preview","Preview","right",&error)["panelId"].toString();QVERIFY(error.isEmpty());
        const auto html=QString("<h1>Viewport</h1>")+QString("<p>Paragraph with <strong>bold</strong> and text.</p>").repeated(4000)+"<p class=\"tail\">Offscreen tail 😀中</p>";
        const QJsonObject context{{"documentId",QJsonValue(QJsonValue::Null)},{"version",QJsonValue(QJsonValue::Null)}};
        QVERIFY(service.setHtml(panel,html,context,&error));QVERIFY(service.reveal(panel,true,&error));window.show();
        auto *page=service.page(panel);QSignalSpy loaded(page,&QWebEnginePage::loadFinished);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);QVERIFY(loaded.last()[0].toBool());
        auto state=std::make_shared<QJsonObject>();
        const auto check=[page,state] {page->runJavaScript("(()=>{const tail=document.querySelector('.tail');tail.scrollIntoView();const r=tail.getBoundingClientRect();return {text:tail.textContent,visible:r.top>=0&&r.bottom<=innerHeight,policy:getComputedStyle(tail).contentVisibility};})()",[state](const QVariant &value){*state=QJsonObject::fromVariantMap(value.toMap());});};
        QTRY_VERIFY_WITH_TIMEOUT(([&]{check();return (*state)["visible"].toBool();})(),10000);
        QCOMPARE((*state)["text"].toString(),QStringLiteral("Offscreen tail 😀中"));QCOMPARE((*state)["policy"].toString(),QString("auto"));
        // Native find-in-page must still include content below the viewport.
        auto matches=std::make_shared<int>(-1);page->findText("Paragraph",{},[matches](const QWebEngineFindTextResult &result){*matches=result.numberOfMatches();});
        QTRY_COMPARE_WITH_TIMEOUT(*matches,4000,10000);service.clear();QCOMPARE(service.resourceCount(),0);
    }
    void nativeAppearanceTracksHostWithoutReload() {
        QTemporaryDir root;QMainWindow window;window.resize(900,600);ScintillaEdit editor;
        editor.styleSetFont(STYLE_DEFAULT,"Menlo");editor.styleSetSizeFractional(STYLE_DEFAULT,1250);
        PluginDocuments::DocumentBridge documents;const auto document=documents.attach(&editor,QUrl(),"markdown");documents.setActive(document);
        QPalette light=window.palette();light.setColor(QPalette::Base,QColor("#fafbfc"));light.setColor(QPalette::Text,QColor("#182430"));
        light.setColor(QPalette::AlternateBase,QColor("#edf0f3"));light.setColor(QPalette::Link,QColor("#123abc"));light.setColor(QPalette::Mid,QColor("#798087"));window.setPalette(light);
        QFont reading("Helvetica");reading.setPixelSize(17);window.setFont(reading);
        WebviewService service("test.appearance",root.path(),&window,&documents);QJsonObject error;
        const auto panel=service.create("preview","Preview","right",&error)["panelId"].toString();QVERIFY(error.isEmpty());
        const QJsonObject context{{"documentId",document},{"version",documents.info(document)["version"]}};
        const auto html=QString("<h1>Appearance 😀中</h1><p><code>inline</code> <a href=\"https://example.com\">link</a></p><pre><code>code</code></pre><blockquote>quote</blockquote>")+QString("<p>Scroll position stays here.</p>").repeated(200);
        QVERIFY(service.setHtml(panel,html,context,&error));QVERIFY(service.reveal(panel,true,&error));window.show();
        auto *page=service.page(panel);QSignalSpy loads(page,&QWebEnginePage::loadFinished),documentEvents(&documents,&PluginDocuments::DocumentBridge::event);
        auto state=std::make_shared<QJsonObject>();
        const auto read=[page,state] {page->runJavaScript(R"JS((()=>{if(!document.querySelector('code')||!document.querySelector('blockquote'))return {};const body=getComputedStyle(document.body),code=getComputedStyle(document.querySelector('code'));return {heading:document.querySelector('h1')?.textContent,background:body.backgroundColor,color:body.color,bodyFamily:body.fontFamily,bodySize:parseFloat(body.fontSize),codeFamily:code.fontFamily,codeSize:parseFloat(code.fontSize),codeBackground:getComputedStyle(document.querySelector('pre')).backgroundColor,link:getComputedStyle(document.querySelector('a')).color,border:getComputedStyle(document.querySelector('blockquote')).borderLeftColor,scheme:body.colorScheme,marker:window.appearanceMarker,scroll:scrollY};})())JS",[state](const QVariant &value){*state=QJsonObject::fromVariantMap(value.toMap());});};
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["background"]=="rgb(250, 251, 252)";})(),10000);
        QCOMPARE((*state)["color"].toString(),QString("rgb(24, 36, 48)"));QVERIFY((*state)["bodyFamily"].toString().contains("Helvetica"));QCOMPARE((*state)["bodySize"].toDouble(),17.0);
        QVERIFY((*state)["codeFamily"].toString().contains("Menlo"));QVERIFY(qAbs((*state)["codeSize"].toDouble()-12.5*window.logicalDpiY()/72.0)<0.1);
        QCOMPARE((*state)["codeBackground"].toString(),QString("rgb(237, 240, 243)"));QCOMPARE((*state)["link"].toString(),QString("rgb(18, 58, 188)"));QCOMPARE((*state)["border"].toString(),QString("rgb(121, 128, 135)"));
        page->runJavaScript("window.appearanceMarker='same-page';scrollTo(0,500)");
        QTRY_VERIFY(([&]{read();return (*state)["marker"]=="same-page"&&(*state)["scroll"].toDouble()>0;})());
        const auto url=service.pageUrl(panel);const auto loadCount=loads.size(),eventCount=documentEvents.size();const auto scroll=(*state)["scroll"].toDouble();
        QPalette dark=light;dark.setColor(QPalette::Base,QColor("#1b2025"));dark.setColor(QPalette::Text,QColor("#f0f2f4"));dark.setColor(QPalette::AlternateBase,QColor("#282e34"));dark.setColor(QPalette::Link,QColor("#8eb8ff"));window.setPalette(dark);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["background"]=="rgb(27, 32, 37)";})(),5000);
        QCOMPARE((*state)["color"].toString(),QString("rgb(240, 242, 244)"));QCOMPARE((*state)["scheme"].toString(),QString("dark"));QCOMPARE((*state)["marker"].toString(),QString("same-page"));QCOMPARE((*state)["scroll"].toDouble(),scroll);
        reading.setFamily("Arial");reading.setPixelSize(19);window.setFont(reading);
        editor.styleSetFont(STYLE_DEFAULT,"Courier New");editor.styleSetSizeFractional(STYLE_DEFAULT,1800);documents.refreshAppearance();
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["bodySize"].toDouble()==19&&(*state)["codeFamily"].toString().contains("Courier New");})(),5000);
        QVERIFY(qAbs((*state)["codeSize"].toDouble()-18.0*window.logicalDpiY()/72.0)<0.1);
        QCOMPARE(service.pageUrl(panel),url);QCOMPARE(loads.size(),loadCount);QCOMPARE(documentEvents.size(),eventCount);QCOMPARE(documents.info(document)["version"],context["version"]);QCOMPARE((*state)["heading"].toString(),QStringLiteral("Appearance 😀中"));
        ScintillaEdit second;second.styleSetFont(STYLE_DEFAULT,"Monaco");second.styleSetSizeFractional(STYLE_DEFAULT,1000);
        const auto secondId=documents.attach(&second,QUrl(),"markdown");documents.setActive(secondId);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["codeFamily"].toString().contains("Monaco");})(),5000);
        QCOMPARE(service.pageUrl(panel),url);QCOMPARE(loads.size(),loadCount);service.clear();QCOMPARE(service.resourceCount(),0);
    }
    void nativeAppearancePendingAndFontEscapes_data() {
        QTest::addColumn<bool>("async");QTest::newRow("initial-font-data")<<false;QTest::newRow("theme-during-preparation")<<true;
    }
    void nativeAppearancePendingAndFontEscapes() {
        QFETCH(bool,async);QTemporaryDir root;QMainWindow window;ScintillaEdit editor;
        const auto hostile=QStringLiteral("Host\"</style><script>window.fontInjected=true</script>&\\\n😀\";}body{color:red}/*");
        editor.styleSetFont(STYLE_DEFAULT,hostile.toUtf8().constData());editor.styleSetSizeFractional(STYLE_DEFAULT,1350);
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,QUrl(),"markdown");documents.setActive(doc);
        QFont font(hostile);font.setPixelSize(18);window.setFont(font);
        QPalette palette=window.palette();palette.setColor(QPalette::Base,QColor("#ffffff"));palette.setColor(QPalette::Text,QColor("#203040"));window.setPalette(palette);
        WebviewService service("test.font-data",root.path(),&window,&documents);QJsonObject error;const auto panel=service.create("preview","Preview","right",&error)["panelId"].toString();QVERIFY(error.isEmpty());
        const QJsonObject context{{"documentId",doc},{"version",documents.info(doc)["version"]}};const auto html=QString("<h1>Unchanged 😀中</h1><pre><code>safe</code></pre>")+QString("<p>Prepared content.</p>").repeated(4000);
        auto completed=std::make_shared<bool>(!async);auto completionError=std::make_shared<QJsonObject>();
        if(async)service.setHtmlAsync(panel,html,context,[completed,completionError](const QJsonObject &value){*completionError=value;*completed=true;});
        else QVERIFY(service.setHtml(panel,html,context,&error));
        // No event delivery between starting preparation and changing the host.
        palette.setColor(QPalette::Base,QColor("#101820"));palette.setColor(QPalette::Text,QColor("#e1e8ef"));window.setPalette(palette);font.setPixelSize(21);window.setFont(font);
        QVERIFY(service.reveal(panel,true,&error));window.show();QTRY_VERIFY_WITH_TIMEOUT(*completed,10000);QVERIFY(completionError->isEmpty());
        auto state=std::make_shared<QJsonObject>();auto *page=service.page(panel);QVERIFY(page);
        const auto read=[page,state]{page->runJavaScript(R"JS((()=>{const s=document.getElementById('nnp-host-appearance');if(!s||!document.querySelector('code'))return {};const style=getComputedStyle(document.body);return {heading:document.querySelector('h1')?.textContent,background:style.backgroundColor,color:style.color,size:parseFloat(style.fontSize),injected:window.fontInjected===true,styles:document.querySelectorAll('style').length,scripts:document.querySelectorAll('script').length,rawClosing:s?.textContent.includes('</style>'),csp:document.querySelector('meta[http-equiv]').content,codeFamily:getComputedStyle(document.querySelector('code')).fontFamily};})())JS",[state](const QVariant &value){*state=QJsonObject::fromVariantMap(value.toMap());});};
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["background"]=="rgb(16, 24, 32)"&&(*state)["size"].toDouble()==21;})(),10000);
        QCOMPARE((*state)["color"].toString(),QString("rgb(225, 232, 239)"));QCOMPARE((*state)["heading"].toString(),QStringLiteral("Unchanged 😀中"));QVERIFY(!(*state)["injected"].toBool());QVERIFY(!(*state)["rawClosing"].toBool());QCOMPARE((*state)["styles"].toInt(),2);QCOMPARE((*state)["scripts"].toInt(),1);
        QVERIFY((*state)["codeFamily"].toString().contains("body{color:red}"));QVERIFY((*state)["csp"].toString().contains("default-src 'none'"));QVERIFY(!(*state)["csp"].toString().contains("unsafe-inline"));
        // Exercise the same escaping in the live, isolated-world update path.
        font.setFamily(hostile+" updated");font.setPixelSize(22);window.setFont(font);const auto url=service.pageUrl(panel);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["size"].toDouble()==22;})(),5000);QCOMPARE(service.pageUrl(panel),url);QVERIFY(!(*state)["rawClosing"].toBool());QVERIFY(!(*state)["injected"].toBool());QCOMPARE((*state)["styles"].toInt(),2);QCOMPARE((*state)["scripts"].toInt(),1);service.clear();
    }
    void asyncHtmlOwnership_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("publish-and-bounded-overlap")<<0;
        QTest::newRow("document-edited-before-publication")<<1;
        QTest::newRow("active-document-changed")<<2;
        QTest::newRow("document-closed")<<3;
        QTest::newRow("panel-disposed")<<4;
        QTest::newRow("synchronous-publication-wins")<<5;
        QTest::newRow("service-destroyed")<<6;
        QTest::newRow("unsafe-html-rejected")<<7;
    }
    void asyncHtmlOwnership() {
        QFETCH(int,scenario);QTemporaryDir root;QMainWindow window;
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("# source");
        PluginDocuments::DocumentBridge documents;const auto doc=documents.attach(&editor,{},"markdown");documents.setActive(doc);
        auto service=std::make_unique<WebviewService>("test.async",root.path(),&window,&documents);
        QJsonObject error;const auto panel=service->create("preview","Preview","right",&error)["panelId"].toString();QVERIFY(error.isEmpty());
        const QJsonObject context{{"documentId",doc},{"version",documents.info(doc)["version"]}};
        struct Completion {int calls=0;QJsonObject error;};auto result=std::make_shared<Completion>();
        const auto html=scenario==7?QString("<script>bad()</script>"):QString("<h1>prepared</h1>")+QString("<p>paragraph with bold <strong>text</strong></p>").repeated(10000);
        service->setHtmlAsync(panel,html,context,[result](const QJsonObject &error){++result->calls;result->error=error;});
        // No GUI events have been pumped. These races are independent of
        // whether the background worker has already finished its preparation.
        if(scenario==0){int overlap=0;service->setHtmlAsync(panel,"<p>overlap</p>",context,[&overlap](const QJsonObject &error){overlap=error["code"].toInt();});QCOMPARE(overlap,1007);}
        if(scenario==1)editor.addText(1,"x");
        if(scenario==2)documents.setActive({});
        if(scenario==3)documents.detach(doc);
        if(scenario==4)service->dispose(panel);
        QUrl winningUrl;
        if(scenario==5){QVERIFY(service->setHtml(panel,"<h1>newer publication</h1>",context,&error));winningUrl=service->pageUrl(panel);}
        if(scenario==6){service.reset();QTest::qWait(200);QCOMPARE(result->calls,0);return;}
        QTRY_COMPARE_WITH_TIMEOUT(result->calls,1,10000);
        if(scenario==0){QVERIFY(result->error.isEmpty());QVERIFY(!service->pageUrl(panel).isEmpty());QCOMPARE(service->resourceCount(),1);}
        else {
            const auto expected=scenario==3?1002:scenario==4?1004:scenario==7?-32602:1001;
            QCOMPARE(result->error["code"].toInt(),expected);
            if(scenario==5)QCOMPARE(service->pageUrl(panel),winningUrl);
            else {QVERIFY(service->pageUrl(panel).isEmpty());QCOMPARE(service->resourceCount(),0);}
        }
        service->clear();QCOMPARE(service->resourceCount(),0);QCOMPARE(service->panelCount(),0);
    }
    void realMarkdownImagesAndDocumentStates() {
        const auto consumer=qEnvironmentVariable("NNP_TEST_CONSUMER_MARKDOWN_PACKAGE");
        if(consumer.isEmpty())QSKIP("Marketplace consumer acceptance is opt-in; provide an executable Markdown package.");
        QTemporaryDir root;QImage image(24,16,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(root.filePath(QStringLiteral("diagram中.png"))));
        QFile onDisk(root.filePath("unsaved.md"));QVERIFY(onDisk.open(QIODevice::WriteOnly));QVERIFY(onDisk.write("# saved disk content\n")>0);onDisk.close();
        QFile metadata(consumer+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));
        QMainWindow window;window.resize(1000,600);ScintillaEdit first,second;first.setCodePage(SC_CP_UTF8);second.setCodePage(SC_CP_UTF8);
        const QByteArray text="# unsaved rich preview 😀中\n\n## Section\n\n- one\n- two\n\n```text\ncode 😀中\n```\n\n![local](diagram%E4%B8%AD.png)\n\n![missing](absent.png)\n\n![remote](https://example.com/remote.png)\n\n<script>window.markdownInjected=true</script>\n";
        first.setText(text.constData());second.setText("# second unsaved buffer\n");
        PluginDocuments::DocumentBridge documents;const auto a=documents.attach(&first,QUrl::fromLocalFile(root.filePath("unsaved.md")),"markdown"),b=documents.attach(&second,QUrl(),"markdown");documents.setActive(a);
        const auto manifest=QJsonDocument::fromJson(metadata.readAll()).object();
        PluginRuntime::PluginSession session(manifest,consumer,QStringLiteral(PLUGIN_TEST_NODE),QStringLiteral(PLUGIN_TEST_HOST),&window,&documents);
        QSignalSpy commands(&session,&PluginRuntime::PluginSession::commandFinished),stopped(&session,&PluginRuntime::PluginSession::stopped);
        session.invoke(manifest["id"].toString()+".open");QTRY_COMPARE_WITH_TIMEOUT(commands.size(),1,10000);QVERIFY(commands.last()[2].toJsonObject().isEmpty());
        QPointer<WebviewService> service=session.webviews();QVERIFY(service);QCOMPARE(service->panelCount(),1);auto *view=window.findChild<QWebEngineView*>();QVERIFY(view);QPointer<QWebEnginePage> page=view->page();window.show();
        auto state=std::make_shared<QJsonObject>();
        const auto read=[page,state]{if(page)page->runJavaScript(R"JS((()=>{if(!document.body)return {};const image=document.querySelector('img');return {heading:document.querySelector('h1')?.textContent,subheading:document.querySelector('h2')?.textContent,items:document.querySelectorAll('li').length,code:document.querySelector('pre code')?.textContent,width:image?.naturalWidth,height:image?.naturalHeight,imageSource:image?.src,placeholders:Array.from(document.querySelectorAll('.image-error'),e=>e.textContent),body:document.body.innerText,error:document.querySelector('.preview-error')?.textContent,injected:window.markdownInjected===true};})())JS",[state](const QVariant &value){*state=QJsonObject::fromVariantMap(value.toMap());});};
        QTRY_VERIFY2_WITH_TIMEOUT(([&]{read();return (*state)["heading"]==QStringLiteral("unsaved rich preview 😀中")&&(*state)["width"].toInt()==24;})(),QJsonDocument(*state).toJson(QJsonDocument::Compact).constData(),10000);
        QCOMPARE((*state)["subheading"].toString(),QString("Section"));QCOMPARE((*state)["items"].toInt(),2);QCOMPARE((*state)["code"].toString(),QStringLiteral("code 😀中\n"));QCOMPARE((*state)["height"].toInt(),16);QVERIFY((*state)["imageSource"].toString().startsWith("nnp-plugin://"));
        const auto placeholders=(*state)["placeholders"].toArray();QCOMPARE(placeholders.size(),2);QVERIFY(placeholders[0].toString().contains("missing"));QVERIFY(placeholders[1].toString().contains("remote"));QVERIFY(!(*state)["injected"].toBool());
        QJsonObject snapshotError;QVERIFY(onDisk.open(QIODevice::ReadOnly));QCOMPARE(onDisk.readAll(),QByteArray("# saved disk content\n"));onDisk.close();QCOMPARE(documents.snapshot(a,&snapshotError)["text"].toString(),QString::fromUtf8(text));QVERIFY(snapshotError.isEmpty());
        documents.setActive(b);first.setText("# obsolete document A\n");
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["heading"]=="second unsaved buffer";})(),10000);QCOMPARE(service->panelCount(),1);
        documents.updateMetadata(b,QUrl(),"plaintext");
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return !(*state)["heading"].isString()&&(*state)["body"].toString().contains("Open a Markdown document");})(),10000);
        documents.updateMetadata(b,QUrl(),"markdown");second.setText("\xffinvalid UTF-8\n");
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["error"].toString().contains("cannot be decoded as UTF-8");})(),10000);
        const QByteArray tooLarge(PluginDocuments::DocumentBridge::MaxSnapshotBytes+1,'x');second.setText(tooLarge.constData());
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["error"].toString().contains("exceeds the preview size limit");})(),10000);
        second.setText("# recovered preview\n");
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["heading"]=="recovered preview";})(),10000);QVERIFY(!(*state)["error"].isString());
        documents.detach(b);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return !(*state)["heading"].isString()&&(*state)["body"].toString().contains("Open a Markdown document");})(),10000);
        documents.setActive(a);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{read();return (*state)["heading"]=="obsolete document A";})(),10000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000);QCOMPARE(session.resourceCount(),0);QVERIFY(!service);QTRY_VERIFY(!page);QTRY_VERIFY(window.findChildren<QDockWidget*>().isEmpty());QCOMPARE(first.textRange(0,first.length()),QByteArray("# obsolete document A\n"));QCOMPARE(second.textRange(0,second.length()),QByteArray("# recovered preview\n"));
    }
    void realMarkdownSession() {
        const auto consumer=qEnvironmentVariable("NNP_TEST_CONSUMER_MARKDOWN_PACKAGE");
        if(consumer.isEmpty())QSKIP("Marketplace consumer acceptance is opt-in; provide an executable Markdown package.");
        QFile metadata(consumer+"/notepadnext-plugin.json"); QVERIFY(metadata.open(QIODevice::ReadOnly));
        QVERIFY2(QFile::exists(consumer+"/dist/index.js"),"Provide the prebuilt Marketplace Markdown package.");
        QMainWindow window; window.resize(1000,600); ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); editor.setText("# unsaved preview\n\n- item\n"); window.setCentralWidget(&editor); window.show();
        PluginDocuments::DocumentBridge documents; const auto document=documents.attach(&editor,{},"markdown"); documents.setActive(document);
        const auto manifest=QJsonDocument::fromJson(metadata.readAll()).object();
        PluginRuntime::PluginSession session(manifest,consumer,QStringLiteral(PLUGIN_TEST_NODE),QStringLiteral(PLUGIN_TEST_HOST),&window,&documents);
        connect(&session,&PluginRuntime::PluginSession::stderrOutput,this,[](const QByteArray &bytes) { qWarning().noquote()<<QString::fromUtf8(bytes); });
        QSignalSpy commands(&session,&PluginRuntime::PluginSession::commandFinished),stopped(&session,&PluginRuntime::PluginSession::stopped),failures(&session,&PluginRuntime::PluginSession::failed);
        session.invoke(manifest["id"].toString()+".open"); session.invoke(manifest["id"].toString()+".open");
        QTRY_COMPARE_WITH_TIMEOUT(commands.size(),1,15000); QVERIFY2(failures.isEmpty(),qPrintable(failures.isEmpty() ? QString() : failures.first()[0].toString())); QVERIFY(commands[0][2].toJsonObject().isEmpty()); QCOMPARE(session.state(),QString("active"));
        auto *service=session.webviews(); QVERIFY(service); QCOMPARE(service->panelCount(),1); auto docks=window.findChildren<QDockWidget*>(); QCOMPARE(docks.size(),1);
        auto *view=docks.first()->findChild<QWebEngineView*>(); QVERIFY(view); auto *page=view->page();
        QString heading;
        const auto readHeading=[page,&heading] { page->runJavaScript("document.querySelector('h1')?.textContent ?? ''",[&heading](const QVariant &value){heading=value.toString();}); };
        QTRY_VERIFY_WITH_TIMEOUT(([&]{readHeading();return heading=="unsaved preview";})(),10000);
        editor.setText("# changed buffer\n"); QTRY_VERIFY_WITH_TIMEOUT(([&]{readHeading();return heading=="changed buffer";})(),10000);
        // Closing reclaims the panel and watch, then the command recreates both.
        docks.first()->close(); QTRY_COMPARE(service->panelCount(),0); const auto before=commands.size(); session.invoke(manifest["id"].toString()+".open"); QTRY_COMPARE(commands.size(),before+1);
        QVERIFY2(commands.last()[2].toJsonObject().isEmpty(),qPrintable(QString::fromUtf8(QJsonDocument(commands.last()[2].toJsonObject()).toJson(QJsonDocument::Compact))));
        QCOMPARE(service->panelCount(),1);
        session.stop(); QCOMPARE(session.resourceCount(),0); QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,5000); QCOMPARE(session.state(),QString("stopped")); QVERIFY(failures.isEmpty());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QVERIFY(window.findChildren<QDockWidget*>().isEmpty()); window.takeCentralWidget();
    }
};
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts); registerScheme(); ObservedTestApplication app(argc,argv);
    PluginWebviewTests tests;
    const auto selected=qEnvironmentVariable("NNP_TEST_WEBVIEW_FUNCTIONS");
    if(!selected.isEmpty())return QTest::qExec(&tests,QStringList{QString::fromLocal8Bit(argv[0])}+selected.split(',',Qt::SkipEmptyParts));
    return QTest::qExec(&tests,argc,argv);
}
#include "PluginWebviewTests.moc"
