// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotepadNextApplication.h"
#include "MainWindow.h"
#include "ScintillaNext.h"
#include "TextEncoding.h"
#include "SessionManager.h"
#include <QAction>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QElapsedTimer>
#include <QEvent>
#include <QUuid>
#include <memory>
#include <QLibraryInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPalette>
#include <QScopeGuard>
#include <QToolBar>
#include <QToolButton>
#include "PreferencesDialog.h"
#include "FindReplaceDialog.h"
#include "MacroSaveDialog.h"
#include "MacroRunDialog.h"
#include "MacroEditorDialog.h"
#include "ColumnEditorDialog.h"
#include "MacroManager.h"
#include "Macro.h"
#include "NativeAppearance.h"
#include <QStackedWidget>
#include <QRadioButton>
#include <QMenu>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QListWidget>
#ifdef NOTEPADNEXT_PLUGINS
#include <QNetworkAccessManager>
#include <QNetworkRequest>

// Authenticated live tests avoid consuming the shared anonymous API quota.
// Never attach the token to raw files, release redirects or plugin resources.
class LiveGitHubNetwork final:public QNetworkAccessManager {
public:
    explicit LiveGitHubNetwork(QObject *parent):QNetworkAccessManager(parent),token(qgetenv("NNP_TEST_GITHUB_TOKEN")){}
protected:
    QNetworkReply *createRequest(Operation operation,const QNetworkRequest &original,QIODevice *outgoing=nullptr)override {
        auto request=original;
        if(!token.isEmpty()&&request.url().scheme()=="https"&&request.url().host()=="api.github.com")request.setRawHeader("Authorization","Bearer "+token);
        return QNetworkAccessManager::createRequest(operation,request,outgoing);
    }
private:
    QByteArray token;
};
#endif
#ifdef PLUGIN_APPLICATION_EXIT_PROBE
#include "ApplicationExitProbe.h"
#endif
#ifdef Q_OS_MACOS
#include <mach-o/dyld.h>
#endif
#ifdef NOTEPADNEXT_PLUGINS
#include "PluginEngine.h"
#include "PluginSession.h"
#include "PluginDataService.h"
#include "RegisteredResourceRegistry.h"
#include "PluginManagerDialog.h"
#include "SourceEntryResolver.h"
#include "DocumentBridge.h"
#include "WireContract.h"
#include <QJsonDocument>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QMessageBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QTabWidget>
#include <QLabel>
#include <QSignalSpy>
#endif
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
#include "WebviewService.h"
#include <QWebEngineView>
#include <QDockWidget>
#include <QWebEnginePage>
#endif
class PluginApplicationTests:public QObject {
    Q_OBJECT
public:
    NotepadNextApplication *app;
    QString directory;
    MainWindow *window=nullptr;
    bool saveDialogPreview(QWidget *dialog,const QString &name) {
        const auto dir=qEnvironmentVariable("NNP_MAC_DIALOG_SCREENSHOTS");
        if(dir.isEmpty())return true;
        if(!QDir().mkpath(dir))return false;
        QTest::qWait(30);return dialog->grab().save(dir+'/'+name+".png");
    }
    QPalette dialogPalette(bool dark) {
        auto palette=app->palette();
        palette.setColor(QPalette::Window,QColor(dark?"#29292c":"#f1f1f1"));
        palette.setColor(QPalette::WindowText,QColor(dark?"#eeeeef":"#242424"));
        palette.setColor(QPalette::Text,palette.color(QPalette::WindowText));
        palette.setColor(QPalette::ButtonText,palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Base,QColor(dark?"#202023":"#ffffff"));
        palette.setColor(QPalette::Button,palette.color(QPalette::Window));
        return palette;
    }
private slots:
    void initTestCase() {
        QNetworkAccessManager *network=nullptr;
#ifdef NOTEPADNEXT_PLUGINS
        if(qEnvironmentVariableIsSet("NNP_TEST_GITHUB_TOKEN")){network=new LiveGitHubNetwork(app);qunsetenv("NNP_TEST_GITHUB_TOKEN");}
#endif
        QVERIFY(app->isPrimary());QVERIFY(app->init(network));
        for(auto *widget:QApplication::topLevelWidgets())if((window=qobject_cast<MainWindow*>(widget)))break;
        QVERIFY(window);app->getSessionManager()->setSessionFileTypes(SessionManager::None);QVERIFY(window->currentEditor());
    }

    void nativeSdkSelectionAndCoreLifecycle() {
#ifdef NOTEPADNEXT_PLUGINS
        auto *engine=app->getPluginEngine();auto *documents=app->getPluginDocumentBridge();auto *installer=app->getPluginInstaller();
        const auto id=QStringLiteral("test.native-tools"),package=directory+"/native-package";QVERIFY(QDir().mkpath(package+"/dist"));
        const auto cleanup=qScopeGuard([&]{
            for(const auto &installedId:{id,QStringLiteral("test.legacy-range")})
                if(!app->getInstalledPluginRegistry()->plugin(installedId).id().isEmpty())engine->uninstall(installedId,true);
            QElapsedTimer deadline;deadline.start();
            while((!app->getInstalledPluginRegistry()->plugin(id).id().isEmpty()||!engine->isIdle())&&deadline.elapsed()<5000)QTest::qWait(10);
        });
        const auto write=[&](const QString &name,const QByteArray &bytes){QFile file(name);if(!file.open(QIODevice::WriteOnly))return false;return file.write(bytes)==bytes.size();};
        const auto ready=directory+"/native-ready",gate=directory+"/native-gate",trace=directory+"/native-trace";
        const auto literal=[](const QString &value){return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1).chopped(1);};
        const auto diagnostic=[&]{QFile file(trace);file.open(QIODevice::ReadOnly);return engine->pluginLog(id)+"\nNative fixture steps: "+QString::fromUtf8(file.readAll());};
        QJsonObject manifest{{"manifestVersion",1},{"id",id},{"name","Native Text Tools"},{"version","0.1.0"},{"list",false},{"repository","https://github.com/KMerdan/native-sdk-test"},
            {"engines",QJsonObject{{"notepadnext",">=0.15.0 <0.16.0"},{"api","^0.3.0"}}},{"runtime",QJsonObject{{"kind","node"},{"entry","dist/index.cjs"}}},
            {"capabilities",QJsonObject{{"required",QJsonArray{"commands@1","documents.read@1","documents.write@1","editors.selection.read@1","editors.selection.write@1","core.commands@1"
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
                ,"webviews@1","webviews.html@1"
#endif
            }},{"optional",QJsonArray{}}}},
            {"activationEvents",QJsonArray{"onCommand:test.native-tools.upper"}},
            {"contributes",QJsonObject{{"commands",QJsonArray{QJsonObject{{"id","test.native-tools.upper"},{"title","Uppercase"}},QJsonObject{{"id","test.native-tools.deduplicate"},{"title","Deduplicate"}}
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
                ,QJsonObject{{"id","test.native-tools.html"},{"title","Native HTML"}}
#endif
            }}}}};
        QVERIFY(write(package+"/notepadnext-plugin.json",QJsonDocument(manifest).toJson()));QVERIFY(write(package+"/LICENSE","GPL-3.0-or-later native integration fixture\n"));
        QByteArray code="const fs=require('node:fs');exports.activate=async ctx=>{if(ctx.host.apiVersion!=='0.3.0')throw new Error('native negotiation');"
            "await ctx.commands.register('test.native-tools.upper',async inv=>{const trace="+literal(trace)+";fs.writeFileSync(trace,'entered');const c=await ctx.editors.capture(inv);fs.appendFileSync(trace,' > captured');const list=await ctx.documents.list();fs.appendFileSync(trace,' > listed');const info=await ctx.documents.getInfo(c.document.documentId);fs.appendFileSync(trace,' > info');if(!list.some(d=>d.documentId===info.documentId)||!Number.isSafeInteger(info.byteLength))throw new Error('native metadata');c.selection=await ctx.editors.setSelections(c.editor.editorId,c.document.version,c.selection.revision,c.selection.selections,c.selection.mainIndex);fs.appendFileSync(trace,' > selected');if((await ctx.core.list()).length!==18)throw new Error('native catalog');fs.writeFileSync("+literal(ready)+",'captured');"
            "const deadline=Date.now()+3000;while(!fs.existsSync("+literal(gate)+")){if(Date.now()>deadline)throw new Error('gate timeout');await new Promise(r=>setTimeout(r,5));}"
            "await ctx.documents.applyEdits(c.document.documentId,c.document.version,c.selection.selections.map((s,n)=>({range:s.range,text:c.selectedText[n].toUpperCase()})),{selectionGuard:{editorId:c.editor.editorId,revision:c.selection.revision}});return ctx.host.apiVersion;});"
            "await ctx.commands.register('test.native-tools.deduplicate',async inv=>{const c=await ctx.editors.capture(inv);return ctx.core.execute('edit.lines.deduplicate',{target:{editorId:c.editor.editorId,documentId:c.document.documentId,expectedVersion:c.document.version,selectionRevision:c.selection.revision}});});";
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        code+="await ctx.commands.register('test.native-tools.html',async()=>{const p=await ctx.webviews.createHtml({viewType:'native-html',title:'Native HTML',enableScripts:true,resourceRoots:['dist']});const src=await p.mapAsset('dist/bridge.js');await p.setDocument('<!doctype html><html><body><h1>Native panel</h1><script src='+JSON.stringify(src)+'></script></body></html>');await p.reveal(true);});";
#endif
        code+="};";QVERIFY(write(package+"/dist/index.cjs",code));
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        QVERIFY(write(package+"/dist/bridge.js","const api=acquireNotepadNextApi();api.setState({native:true});document.body.dataset.bridge=String(api.getState().native);"));
#endif
        PluginSources::SourceSnapshot source;QString error;QVERIFY2(PluginSources::PluginMetadata::parse(QJsonDocument(manifest).toJson(),manifest["repository"].toString(),{},&source,&error),qPrintable(error));
        QVERIFY(installer->installDirectory(package,source.resources[0]));QTRY_VERIFY_WITH_TIMEOUT(!installer->isBusy(),10000);QVERIFY2(!app->getInstalledPluginRegistry()->plugin(id).id().isEmpty(),qPrintable(error));
        QVERIFY(engine->setEnabled(id,true,&error));
        auto *first=window->currentEditor();first->setCodePage(SC_CP_UTF8);first->setText(QStringLiteral("ab😀中\r\ncd\r\n").toUtf8().constData());first->setMultipleSelection(true);first->setSelection(9,0);first->addSelection(13,11);first->emptyUndoBuffer();
        QJsonObject rpcFailure;const auto documentId=documents->idFor(first);documents->setActive(documentId);const auto nativeWindow=window->winId();
        QSignalSpy commandErrors(engine,&PluginRuntime::PluginEngine::error);
        engine->invoke(id,"test.native-tools.upper");
        window->findChild<QAction*>("actionNew")->trigger();QTRY_VERIFY(window->currentEditor()!=first);auto *second=window->currentEditor();second->setText("keep me");
        QTRY_VERIFY2_WITH_TIMEOUT(QFileInfo::exists(ready)||!commandErrors.isEmpty(),qPrintable(diagnostic()),10000);
        QVERIFY2(QFileInfo::exists(ready),qPrintable(diagnostic()));
        auto *session=engine->findChild<PluginRuntime::PluginSession*>();QVERIFY(session);QSignalSpy finished(session,&PluginRuntime::PluginSession::commandFinished);
        QVERIFY(write(gate,"continue"));QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),5000);QVERIFY2(finished.last()[2].toJsonObject().isEmpty(),qPrintable(finished.last()[2].toJsonObject()["message"].toString()));
        QCOMPARE(documents->snapshot(documentId,&rpcFailure)["text"].toString(),QStringLiteral("AB😀中\r\nCD\r\n"));QCOMPARE(documents->snapshot(documents->idFor(second),&rpcFailure)["text"].toString(),"keep me");
        first->undo();QCOMPARE(documents->snapshot(documentId,&rpcFailure)["text"].toString(),QStringLiteral("ab😀中\r\ncd\r\n"));
        // A selection-only change expires the captured transform before commit.
        QVERIFY(QFile::remove(ready));QVERIFY(QFile::remove(gate));documents->setActive(documentId);finished.clear();engine->invoke(id,"test.native-tools.upper");QTRY_VERIFY(QFileInfo::exists(ready));first->setSelection(1,0);QVERIFY(write(gate,"continue"));QTRY_VERIFY(!finished.isEmpty());QCOMPARE(finished.last()[2].toJsonObject()["code"].toInt(),1011);
        QCOMPARE(documents->snapshot(documentId,&rpcFailure)["text"].toString(),QStringLiteral("ab😀中\r\ncd\r\n"));
        first->setText("b\na\nb\n");first->setEOLMode(SC_EOL_LF);first->emptyUndoBuffer();finished.clear();engine->invoke(id,"test.native-tools.deduplicate");QTRY_VERIFY(!finished.isEmpty());QVERIFY(finished.last()[2].toJsonObject().isEmpty());QCOMPARE(documents->snapshot(documentId,&rpcFailure)["text"].toString(),"b\na\n");first->undo();QCOMPARE(documents->snapshot(documentId,&rpcFailure)["text"].toString(),"b\na\nb\n");
        first->setReadOnly(true);finished.clear();engine->invoke(id,"test.native-tools.deduplicate");QTRY_VERIFY(!finished.isEmpty());QCOMPARE(finished.last()[2].toJsonObject()["code"].toInt(),1003);first->setReadOnly(false);
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        finished.clear();engine->invoke(id,"test.native-tools.html");QTRY_VERIFY(!finished.isEmpty());QVERIFY2(finished.last()[2].toJsonObject().isEmpty(),qPrintable(finished.last()[2].toJsonObject()["message"].toString()));
        QWebEnginePage *page=nullptr;for(auto *view:window->findChildren<QWebEngineView*>())if(view->parentWidget()->objectName().contains(id)){page=view->page();break;}
        if(!page)for(auto *view:window->findChildren<QWebEngineView*>())page=view->page();QVERIFY(page);
        auto bridged=std::make_shared<bool>(false);QTRY_VERIFY_WITH_TIMEOUT(([&]{page->runJavaScript("document.body?.dataset.bridge==='true'",[bridged](const QVariant &value){*bridged=value.toBool();});return *bridged;})(),5000);
#endif
        engine->uninstall(id,true);QTRY_VERIFY_WITH_TIMEOUT(app->getInstalledPluginRegistry()->plugin(id).id().isEmpty(),5000);QTRY_VERIFY(engine->isIdle());QCOMPARE(window->winId(),nativeWindow);

#else
        QSKIP("Plugins disabled");
#endif
    }

    void nativeSdkPhaseBProtocolLifecycle() {
#ifdef NOTEPADNEXT_PLUGINS
        auto *engine=app->getPluginEngine();auto *documents=app->getPluginDocumentBridge();auto *installer=app->getPluginInstaller();
        const auto id=QStringLiteral("test.native-b"),package=directory+"/native-b-package";
        const auto fixture=QDir(QFileInfo(QStringLiteral(PLUGIN_TEST_STAGER)).absolutePath()).filePath("../fixtures/native-phase-b");
        QVERIFY(QDir().mkpath(package+"/dist"));
        for(const auto &name:QStringList{"notepadnext-plugin.json","LICENSE","dist/index.cjs"})QVERIFY(QFile::copy(fixture+'/'+name,package+'/'+name));
        QFile metadata(package+"/notepadnext-plugin.json");QVERIFY(metadata.open(QIODevice::ReadOnly));auto manifest=QJsonDocument::fromJson(metadata.readAll()).object();metadata.close();manifest["repository"]="https://github.com/KMerdan/native-sdk-test";
        QVERIFY(metadata.open(QIODevice::WriteOnly));metadata.write(QJsonDocument(manifest).toJson());metadata.close();
        PluginSources::SourceSnapshot source;QString error;QVERIFY2(PluginSources::PluginMetadata::parse(QJsonDocument(manifest).toJson(),manifest["repository"].toString(),{},&source,&error),qPrintable(error));
        const auto cleanup=qScopeGuard([&]{engine->uninstall(id,true);QElapsedTimer timer;timer.start();while(!engine->isIdle()&&timer.elapsed()<5000)QTest::qWait(10);});
        window->newFile();auto *editor=window->currentEditor();editor->setText(QStringLiteral("TODO 😀中\nTODO\n").toUtf8().constData());editor->setSelection(1,0);app->setEditorLanguage(editor,"Text");
        const auto nativeWindow=window->winId();QVERIFY(installer->installDirectory(package,source.resources[0]));QTRY_VERIFY_WITH_TIMEOUT(!installer->isBusy(),10000);QVERIFY(engine->setEnabled(id,true,&error));QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),10000);
        PluginRuntime::PluginSession *session=nullptr;for(auto *candidate:engine->findChildren<PluginRuntime::PluginSession*>())if(candidate->state()=="active")session=candidate;QVERIFY(session);
        QSignalSpy result(session,&PluginRuntime::PluginSession::commandFinished);const auto saved=directory+"/native-b-result.txt";
        session->invoke("test.native-b.run",{QUrl::fromLocalFile(saved).toString()});QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty(),15000);
        QVERIFY2(result[0][2].toJsonObject().isEmpty(),qPrintable(QJsonDocument(result[0][2].toJsonObject()).toJson()));const auto report=result[0][1].toJsonValue().toObject();QCOMPARE(report["apiVersion"].toString(),"0.3.0");QCOMPARE(report["queryCount"].toInt(),2);QVERIFY(report["events"].toArray().contains("saved"));
        QFile file(saved);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QString::fromUtf8(file.readAll()),QStringLiteral("Generated 😀中\n"));QCOMPARE(window->winId(),nativeWindow);QCOMPARE(documents->ownedResourceCount(session->sessionId()),0);QCOMPARE(session->resourceCount(),1);
        editor->setSavePoint();
#else
        QSKIP("Plugin host disabled");
#endif
    }
    void nativeSdkPhaseBFileDialogCancellationAndStaleTarget() {
#ifdef NOTEPADNEXT_PLUGINS
        auto *documents=app->getPluginDocumentBridge();window->newFile();QPointer<ScintillaNext> editor=window->currentEditor();editor->setText("unsaved draft");const auto id=documents->idFor(editor);QJsonObject error;
        const auto nul=QString::fromUtf8(QByteArray("a\0b",3));const auto created=documents->extension("nnp.files.create",{{"text",nul},{"activate",false}},window,"creating",[]{return true;},&error).toObject();QVERIFY(error.isEmpty());const auto createdId=created["document"].toObject()["documentId"].toString();QCOMPARE(documents->snapshot(createdId,&error)["text"].toString(),nul);auto *createdEditor=qobject_cast<ScintillaNext*>(documents->editorForDocument(createdId));QVERIFY(createdEditor);createdEditor->setSavePoint();createdEditor->close();
        const bool previous=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);const auto restore=qScopeGuard([&]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,previous);if(editor)editor->setSavePoint();});
        QTimer driver;driver.setInterval(10);bool dialogSeen=false;
        connect(&driver,&QTimer::timeout,window,[&]{for(auto *dialog:window->findChildren<QFileDialog*>())if(dialog->isVisible()){dialogSeen=true;dialog->reject();driver.stop();}});driver.start();
        const auto cancelled=documents->extension("nnp.files.saveAs",{{"documentId",id},{"expectedVersion",documents->info(id)["version"]}},window,"dialogs",[]{return true;},&error).toObject();QVERIFY(error.isEmpty());QVERIFY(dialogSeen);QCOMPARE(cancelled["status"].toString(),"cancelled");QCOMPARE(editor->get_text_range(0,editor->length()),QByteArray("unsaved draft"));
        driver.disconnect();const auto path=directory+"/stale-save.txt";dialogSeen=false;
        connect(&driver,&QTimer::timeout,window,[&]{for(auto *dialog:window->findChildren<QFileDialog*>())if(dialog->isVisible()){dialogSeen=true;driver.stop();editor->insertText(0,"changed ");dialog->selectFile(path);static_cast<QDialog*>(dialog)->accept();}});driver.start();
        documents->extension("nnp.files.saveAs",{{"documentId",id},{"expectedVersion",documents->info(id)["version"]}},window,"dialogs",[]{return true;},&error);QVERIFY(dialogSeen);QCOMPARE(error["code"].toInt(),1001);QVERIFY(!QFileInfo::exists(path));QVERIFY(editor->get_text_range(0,editor->length()).startsWith("changed "));
        driver.disconnect();dialogSeen=false;connect(&driver,&QTimer::timeout,window,[&]{for(auto *dialog:window->findChildren<QMessageBox*>())if(dialog->isVisible()){dialogSeen=true;dialog->reject();driver.stop();}});driver.start();
        const auto closed=documents->extension("nnp.files.close",{{"documentId",id},{"expectedVersion",documents->info(id)["version"]},{"policy","prompt"}},window,"dialogs",[]{return true;},&error).toObject();QVERIFY(error.isEmpty());QVERIFY(dialogSeen);QCOMPARE(closed["status"].toString(),"cancelled");QVERIFY(documents->editorForDocument(id));
        driver.disconnect();dialogSeen=false;bool permitted=true;connect(&driver,&QTimer::timeout,window,[&]{for(auto *dialog:window->findChildren<QFileDialog*>())if(dialog->isVisible()){dialogSeen=true;permitted=false;driver.stop();}});driver.start();
        documents->extension("nnp.files.saveAs",{{"documentId",id},{"expectedVersion",documents->info(id)["version"]}},window,"dialogs",[&]{return permitted;},&error);QVERIFY(dialogSeen);QCOMPARE(error["code"].toInt(),1004);QVERIFY(!QFileInfo::exists(path));
#else
        QSKIP("Plugin host disabled");
#endif
    }
    void nativeSdkPhaseBOwnedDecorationsAndLastTabClose() {
#ifdef NOTEPADNEXT_PLUGINS
        auto *documents=app->getPluginDocumentBridge();window->newFile();auto *editor=window->currentEditor();editor->setText("one two\n");const auto id=documents->idFor(editor);const auto version=documents->info(id)["version"].toInteger();QJsonObject error;
        const auto create=[&](const QString &owner){return documents->extension("nnp.decorations.create",{{"documentId",id},{"style","highlight"},{"color","#007aff"}},window,owner,[]{return true;},&error).toObject()["resourceId"].toString();};
        const auto first=create("owner-one"),second=create("owner-two");QVERIFY(error.isEmpty());QVERIFY(!first.isEmpty());QVERIFY(!second.isEmpty());
        const auto set=[&](const QString &owner,const QString &handle,int start){return documents->extension("nnp.decorations.set",{{"resourceId",handle},{"expectedVersion",version},{"ranges",QJsonArray{QJsonObject{{"start",QJsonObject{{"line",0},{"character",start}}},{"end",QJsonObject{{"line",0},{"character",start+1}}}}}}},window,owner,[]{return true;},&error);};
        const auto indicator=editor->indicatorCurrent();set("owner-one",first,0);set("owner-two",second,4);QVERIFY(error.isEmpty());QCOMPARE(editor->indicatorAllOnFor(0),sptr_t(0));
        documents->publishOwner("owner-one");documents->publishOwner("owner-two");QVERIFY(editor->indicatorAllOnFor(0)!=0);QVERIFY(editor->indicatorAllOnFor(4)!=0);QCOMPARE(editor->indicatorCurrent(),indicator);
        documents->extension("nnp.resources.dispose",{{"resourceId",second}},window,"owner-one",[]{return true;},&error);QCOMPARE(error["code"].toInt(),1003);documents->releaseOwner("owner-one");QCOMPARE(editor->indicatorAllOnFor(0),sptr_t(0));QVERIFY(editor->indicatorAllOnFor(4)!=0);
        documents->releaseOwner("owner-two");QCOMPARE(editor->indicatorAllOnFor(4),sptr_t(0));
        for(int n=0;n<100;++n){const auto handle=create("reloading");QVERIFY(error.isEmpty());QVERIFY(!handle.isEmpty());set("reloading",handle,0);QVERIFY(error.isEmpty());documents->publishOwner("reloading");documents->releaseOwner("reloading");QCOMPARE(documents->ownedResourceCount("reloading"),0);QCOMPARE(editor->indicatorCurrent(),indicator);}
        QStringList handles;for(int n=0;n<8;++n){handles<<create("quota");QVERIFY(error.isEmpty());}QVERIFY(create("quota").isEmpty());QCOMPARE(error["code"].toInt(),1007);documents->releaseOwner("quota");
        const auto windowId=window->winId();const bool exitOnClose=app->getSettings()->exitOnLastTabClosed();app->getSettings()->setExitOnLastTabClosed(true);const auto restore=qScopeGuard([&]{app->getSettings()->setExitOnLastTabClosed(exitOnClose);});
        for(const auto *other:window->editors())const_cast<ScintillaNext*>(other)->setSavePoint();
        while(window->editorCount()>1){auto *other=window->editors().last();other->close();}
        auto *last=window->currentEditor();const auto lastId=documents->idFor(last);const auto lastVersion=documents->info(lastId)["version"].toInteger();
        const auto closed=documents->extension("nnp.files.close",{{"documentId",lastId},{"expectedVersion",lastVersion},{"policy","ifClean"}},window,"closing",[]{return true;},&error).toObject();QVERIFY(error.isEmpty());QCOMPARE(closed["status"].toString(),"completed");QCOMPARE(window->winId(),windowId);QCOMPARE(window->editorCount(),1);QVERIFY(window->currentEditor());
#else
        QSKIP("Plugin host disabled");
#endif
    }
    void productionNativeConsumerTools() {
#if defined(NOTEPADNEXT_PLUGINS) && defined(NOTEPADNEXT_PLUGIN_WEBVIEWS)
        const auto inspector=qEnvironmentVariable("NNP_TEST_CONSUMER_INSPECTOR_PACKAGE"),todo=qEnvironmentVariable("NNP_TEST_CONSUMER_TODO_PACKAGE");
        if(inspector.isEmpty()||todo.isEmpty())QSKIP("Native consumer acceptance is opt-in; provide executable Marketplace packages.");
        auto *engine=app->getPluginEngine();auto *installer=app->getPluginInstaller();const auto nativeWindow=window->winId();
        window->newFile();auto *editor=window->currentEditor();QByteArray text="TODO 😀中\n";for(int n=0;n<120;++n)text+="TODO again item "+QByteArray::number(n)+"\n";editor->setText(text.constData());editor->setSelection(12,0);
        const auto read=[&](const QString &script){QPointer<QWebEngineView> view=window->findChild<QWebEngineView*>();if(!view)return QVariant();auto value=std::make_shared<QVariant>();auto ready=std::make_shared<bool>(false);view->page()->runJavaScript(script,[value,ready](const QVariant &result){*value=result;*ready=true;});QElapsedTimer wait;wait.start();while(!*ready&&wait.elapsed()<3000)QTest::qWait(10);return *value;};
        for(const auto &package:QStringList{inspector,todo}) {
            PluginSources::SourceSnapshot source;QString error;QVERIFY2(PluginSources::PluginMetadata::readLocalFile(package+"/notepadnext-plugin.json",&source,&error),qPrintable(error));const auto id=source.resources[0].id;const auto command=source.resources[0].metadata["contributes"].toObject()["commands"].toArray()[0].toObject()["id"].toString();
            const auto cleanup=qScopeGuard([&]{engine->uninstall(id,true);QElapsedTimer wait;wait.start();while(!engine->isIdle()&&wait.elapsed()<5000)QTest::qWait(10);});
            QVERIFY(installer->installDirectory(package,source.resources[0]));QTRY_VERIFY(!installer->isBusy());QVERIFY(engine->setEnabled(id,true,&error));engine->invoke(id,command);QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),15000);QTRY_VERIFY(window->findChild<QWebEngineView*>());
            if(id.endsWith("text-inspector")){QTRY_VERIFY_WITH_TIMEOUT(read("document.body?.textContent.includes('TODO 😀中')").toBool(),10000);}
            else {
                QTRY_COMPARE_WITH_TIMEOUT(read("document.querySelector('#results')?.children.length").toInt(),121,10000);
                read("document.querySelector('#query').value='again';document.querySelector('#query').dispatchEvent(new Event('input'))");QTRY_COMPARE_WITH_TIMEOUT(read("document.querySelector('#results')?.children.length").toInt(),120,10000);
                read("window.scrollTo(0,160)");QTRY_VERIFY(read("window.scrollY").toInt()>0);const auto scroll=read("window.scrollY").toInt();QDockWidget *dock=nullptr;for(auto *candidate:window->findChildren<QDockWidget*>())if(candidate->objectName().startsWith("plugin:"+id+":"))dock=candidate;QVERIFY(dock);const auto geometry=dock->geometry();QSignalSpy changed(engine,&PluginPackages::PluginController::operationFinished);engine->reload(id);QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(),15000);QVERIFY2(changed.last()[1].toString().isEmpty(),qPrintable(changed.last()[1].toString()));
                QTRY_COMPARE_WITH_TIMEOUT(read("document.querySelector('#query')?.value").toString(),QString("again"),10000);QTRY_COMPARE_WITH_TIMEOUT(read("document.querySelector('#results')?.children.length").toInt(),120,10000);QTRY_COMPARE_WITH_TIMEOUT(read("window.scrollY").toInt(),scroll,10000);QCOMPARE(dock->geometry(),geometry);QVERIFY(editor->indicatorAllOnFor(editor->positionFromLine(1)+5)!=0);
            }
            QCOMPARE(window->winId(),nativeWindow);engine->uninstall(id,true);QTRY_VERIFY(app->getInstalledPluginRegistry()->plugin(id).id().isEmpty());QTRY_VERIFY(engine->isIdle());QVERIFY(!window->findChild<QWebEngineView*>());
        }
        editor->setSavePoint();
#else
        QSKIP("Native HTML panels unavailable");
#endif
    }
    void nativeTextEncodingRoundTripsAndLossProtection() {
        const QString unicode=QString::fromUtf8("日本語 中文 😀 café\r\n");
        for(const auto &encoding:QStringList{"UTF-8","UTF-8 BOM","UTF-16LE","UTF-16LE BOM","UTF-16BE","UTF-16BE BOM"}) {
            QByteArray bytes;QString text,error;QVERIFY2(TextEncoding::encode(unicode,encoding,&bytes,&error),qPrintable(error));
            QVERIFY2(TextEncoding::decode(bytes,encoding,&text,&error),qPrintable(error));QCOMPARE(text,unicode);
        }
        for(const auto &sample:QList<QPair<QString,QString>>{{"Shift_JIS",QString::fromUtf8("日本語")},{"Windows-1252",QString::fromUtf8("café €")},{"GB18030",QString::fromUtf8("中文")}}) {
            QByteArray bytes;QString text,error;QVERIFY2(TextEncoding::encode(sample.second,sample.first,&bytes,&error),qPrintable(error));
            QVERIFY(TextEncoding::decode(bytes,sample.first,&text,&error));QCOMPARE(text,sample.second);
        }
        QString text,error;QVERIFY(!TextEncoding::decode(QByteArray::fromHex("eda080"),"UTF-8",&text,&error));
        QTemporaryDir dir;const auto file=dir.filePath("encoding.txt");{QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write("original");}
        std::unique_ptr<ScintillaNext> editor(ScintillaNext::fromFile(file));QVERIFY(editor);
        QVERIFY(editor->convertEncoding("Windows-1252",&error));QVERIFY(editor->modify());
#ifdef NOTEPADNEXT_PLUGINS
        auto *documents=app->getPluginDocumentBridge();const auto doc=documents->attach(editor.get(),QUrl::fromLocalFile(file),"plaintext");
        const auto encodingOwner=qScopeGuard([&]{documents->detach(doc);});
        QCOMPARE(documents->nativeInfo(doc)["encoding"].toString(),QString("windows-1252"));
        QString contractError;int contractCode;QVERIFY2(PluginRpc::WireContract::validate("nnp.documents.getInfo","result",documents->nativeInfo(doc),&contractError,&contractCode),qPrintable(contractError));
#endif
        editor->appendText(4,"\xf0\x9f\x98\x80");
        QVERIFY(editor->save()!=QFileDevice::NoError);QVERIFY(editor->modify());QVERIFY(editor->encodingError().contains("cannot represent"));
        {QFile f(file);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("original"));}
        const auto before=QByteArray((char*)editor->characterPointer(),editor->textLength());
        {QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QByteArray::fromHex("eda080"));}
        QVERIFY(!editor->reloadEncoding("UTF-8",&error));QCOMPARE(QByteArray((char*)editor->characterPointer(),editor->textLength()),before);
        QVERIFY(editor->convertEncoding("UTF-16LE BOM",&error));QVERIFY(editor->save()==QFileDevice::NoError);QVERIFY(!editor->modify());
        std::unique_ptr<ScintillaNext> reopened(ScintillaNext::fromFile(file));QVERIFY(reopened);QCOMPARE(reopened->textEncoding(),QString("UTF-16LE BOM"));
        QCOMPARE(QByteArray((char*)reopened->characterPointer(),reopened->textLength()),before);
    }
    void editorOpenSaveReloadAndClose() {
        const auto file=directory+"/sample.md";QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write("# saved 😀中\n");f.close();
        // Programmatic tab selection cannot depend on macOS foreground
        // focus, and closing the initial tab must not leave a stale pointer.
        window->hide();
        window->openFile(file);
        QTRY_VERIFY(window->currentEditor()&&window->currentEditor()->isFile());
        auto *editor=window->currentEditor();QCOMPARE(QDir::fromNativeSeparators(editor->getFilePath()),QFileInfo(file).canonicalFilePath());
        window->show();
        editor->setText("# unsaved 😀中\n");QVERIFY(!editor->isSavedToDisk());QCOMPARE(editor->save(),QFileDevice::NoError);QVERIFY(editor->isSavedToDisk());
#ifdef NOTEPADNEXT_PLUGINS
        auto *documents=app->getPluginDocumentBridge();const auto id=documents->idFor(editor);QVERIFY(!id.isEmpty());QCOMPARE(documents->active().toObject()["documentId"].toString(),id);
        const auto before=documents->info(id)["version"].toInteger();QJsonObject error;QCOMPARE(documents->snapshot(id,&error)["text"].toString(),QStringLiteral("# unsaved 😀中\n"));
#endif
        QVERIFY(f.open(QIODevice::WriteOnly));f.write("# reloaded\n");f.close();editor->reload();
#ifdef NOTEPADNEXT_PLUGINS
        QVERIFY(documents->info(id)["version"].toInteger()>before);QCOMPARE(documents->snapshot(id,&error)["text"].toString(),QString("# reloaded\n"));
        const auto renamed=directory+"/renamed.md";QVERIFY(editor->rename(renamed));QCOMPARE(documents->idFor(editor),id);QVERIFY(documents->info(id)["uri"].toString().endsWith("renamed.md"));
#endif
        window->closeFile(editor);
#ifdef NOTEPADNEXT_PLUGINS
        QVERIFY(documents->info(id).isEmpty());
#endif
        QVERIFY(window->currentEditor());
    }
    void deployedLibraryAudit() {
#ifdef Q_OS_MACOS
        const auto auditPath=qEnvironmentVariable("NNP_DEPLOYMENT_AUDIT_PATH");if(auditPath.isEmpty())return;
        const auto bundle=QFileInfo(QCoreApplication::applicationDirPath()+"/../..").canonicalFilePath();QJsonArray images;
        for(uint32_t index=0;index<_dyld_image_count();++index) {
            const auto path=QString::fromUtf8(_dyld_get_image_name(index));if(!path.contains("/Qt")||!path.contains(".framework/"))continue;
            const auto canonical=QFileInfo(path).canonicalFilePath();QVERIFY2(canonical.startsWith(bundle+'/'),qPrintable("Qt loaded outside deployed bundle: "+path));images.append(canonical);
        }
        QVERIFY(images.size()>=6);
        const auto plugins=QFileInfo(QLibraryInfo::path(QLibraryInfo::PluginsPath)).canonicalFilePath();QVERIFY2(plugins.startsWith(bundle+'/'),qPrintable(plugins));
        QJsonObject record{{"bundle",bundle},{"qtImages",images},{"pluginsPath",plugins},{"qtVersion",qVersion()}};
#ifdef NOTEPADNEXT_PLUGINS
        const auto paths=PluginRuntime::RuntimePaths::bundled();QVERIFY(QFileInfo(paths.node).canonicalFilePath().startsWith(bundle+'/'));record.insert("nodePath",paths.node);record.insert("hostPath",paths.hostEntry);
        QProcess node;node.start(paths.node,{"-e","console.log(JSON.stringify({execPath:process.execPath,version:process.version,jit:Array.from({length:10000},(_,i)=>i).reduce((a,b)=>a+b,0)}))"});QVERIFY(node.waitForFinished(5000));QCOMPARE(node.exitCode(),0);const auto runtime=QJsonDocument::fromJson(node.readAllStandardOutput()).object();QVERIFY(QFileInfo(runtime["execPath"].toString()).canonicalFilePath().startsWith(bundle+'/'));QCOMPARE(runtime["jit"].toInt(),49995000);record.insert("runtime",runtime);
#endif
        QFile file(auditPath);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(record).toJson());
#endif
    }
    void macOSChrome() {
#ifdef Q_OS_MACOS
        auto *toolbar=window->findChild<QToolBar*>("mainToolBar");QVERIFY(toolbar);
        QCOMPARE(toolbar->iconSize(),QSize(20,20));
        for(const auto &name:{"actionNew","actionOpen","actionSave","actionUndo","actionFind","actionWordWrap"}) {
            auto *action=window->findChild<QAction*>(name);QVERIFY(action);QVERIFY2(!action->icon().name().isEmpty(),name);
            const auto image=action->icon().pixmap(QSize(20,20)).toImage();int visible=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)visible+=qAlpha(image.pixel(x,y))>0;
            QVERIFY2(visible>10,name);
        }
        for(auto *button:toolbar->findChildren<QToolButton*>())if(button->defaultAction()){QCOMPARE(button->focusPolicy(),Qt::StrongFocus);QVERIFY(!button->accessibleName().isEmpty());}
        auto *more=toolbar->findChild<QToolButton*>("nativeMoreCommands");QVERIFY(more);QVERIFY(more->menu());
        QVERIFY(!toolbar->actions().contains(window->findChild<QAction*>("actionMacroRecording")));
        QVERIFY(more->menu()->actions().contains(window->findChild<QAction*>("actionMacroRecording")));
        const auto iconDir=qEnvironmentVariable("NNP_MAC_ICON_ASSETS");
        for(const auto &name:{"ellipsis","minus"}){
            const auto image=NativeAppearance::symbolIcon(name,{},dialogPalette(false)).pixmap(QSize(64,64)).toImage();QRect ink;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))>127)ink=ink.united(QRect(x,y,1,1));
            QVERIFY2(ink.width()>3*ink.height(),"Wide system symbols must retain their natural proportions");
        }
        if(!iconDir.isEmpty()){
            QVERIFY(QDir().mkpath(iconDir));
            for(bool dark:{false,true})for(const auto &name:{"doc.badge.plus","folder","square.and.arrow.down","arrow.uturn.backward","arrow.uturn.forward","magnifyingglass","arrow.left.arrow.right","arrow.turn.down.left","ellipsis","gearshape","textformat","doc","chevron.right","plus","minus","chevron.up","chevron.down","trash","doc.on.doc"}){
                auto file=QString(name);file.replace('.','_');
                QVERIFY(NativeAppearance::symbolIcon(name,{},dialogPalette(dark)).pixmap(QSize(64,64)).save(iconDir+'/'+file+(dark?"-dark.png":"-light.png")));
            }
        }
        const auto originalPalette=window->palette();const auto originalSize=window->size();
        const auto restore=qScopeGuard([&]{window->setPalette(originalPalette);window->resize(originalSize);window->currentEditor()->setText("");window->currentEditor()->setSavePoint();});
        window->currentEditor()->setText("# Writing on macOS\n\nThe editor keeps your unsaved work.\n\n- Native system symbols\n- Clear tabs and readable status\n");
        app->setEditorLanguage(window->currentEditor(),"Markdown");
        const auto screenshotDir=qEnvironmentVariable("NNP_MAC_CHROME_SCREENSHOTS");
        if(!screenshotDir.isEmpty())QVERIFY(QDir().mkpath(screenshotDir));
        for(const bool dark:{false,true}) {
            QPalette palette=originalPalette;
            palette.setColor(QPalette::Window,dark?QColor("#29292c"):QColor("#f1f1f1"));
            palette.setColor(QPalette::WindowText,dark?QColor("#eeeeef"):QColor("#242424"));
            palette.setColor(QPalette::Text,palette.color(QPalette::WindowText));
            palette.setColor(QPalette::Base,dark?QColor("#202023"):QColor("#ffffff"));
            window->setPalette(palette);window->resize(1200,760);QTest::qWait(40);
            QVERIFY(window->styleSheet().contains(dark?"#29292c":"#f1f1f1"));
            const auto assertGlyphColor=[&](QIcon::State state,QColor expected){
                const auto image=window->findChild<QAction*>("actionWordWrap")->icon().pixmap(QSize(32,32),QIcon::Normal,state).toImage();
                bool solid=false;
                for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))>250){
                    solid=true;const auto actual=image.pixelColor(x,y);
                    QVERIFY(qAbs(actual.red()-expected.red())<=2&&qAbs(actual.green()-expected.green())<=2&&qAbs(actual.blue()-expected.blue())<=2);
                }
                QVERIFY(solid);
            };
            assertGlyphColor(QIcon::Off,palette.color(QPalette::WindowText));
            assertGlyphColor(QIcon::On,QColor(dark?"#85baff":"#005ec5"));
            const auto background=window->currentEditor()->styleBack(STYLE_DEFAULT);
            QCOMPARE(QColor(background&255,(background>>8)&255,(background>>16)&255),palette.color(QPalette::Base));
            if(!screenshotDir.isEmpty())QVERIFY(window->grab().save(screenshotDir+(dark?"/editor-dark.png":"/editor-light.png")));
        }
        window->resize(820,560);QTest::qWait(40);
        if(!screenshotDir.isEmpty())QVERIFY(window->grab().save(screenshotDir+"/editor-small.png"));
#endif
    }
    void macOSSettingsDialog() {
#ifdef Q_OS_MACOS
        auto *settings=app->getSettings();const auto originalSize=settings->fontSize();const auto originalRecenter=settings->centerSearchDialog();
        const auto restore=qScopeGuard([&]{settings->setFontSize(originalSize);settings->setCenterSearchDialog(originalRecenter);});
        PreferencesDialog dialog(settings,window);dialog.show();
        auto *categories=dialog.findChild<QListWidget*>("settingsCategories");auto *pages=dialog.findChild<QStackedWidget*>("settingsPages");
        QVERIFY(categories);QVERIFY(pages);QCOMPARE(categories->count(),4);
        categories->setFocus();QTest::keyClick(categories,Qt::Key_Down);QCOMPARE(pages->currentIndex(),1);
        auto *fontSize=dialog.findChild<QSpinBox*>("spbDefaultFontSize");QVERIFY(fontSize);fontSize->setValue(originalSize+1);
        QCOMPARE(settings->fontSize(),originalSize+1);ApplicationSettings reread;QCOMPARE(reread.fontSize(),originalSize+1);
        settings->setFontSize(originalSize);QCOMPARE(fontSize->value(),originalSize);
        categories->setCurrentRow(3);auto *recenter=dialog.findChild<QCheckBox*>("checkBoxRecenterSearchDialog");QVERIFY(recenter);
        recenter->click();QCOMPARE(settings->centerSearchDialog(),!originalRecenter);
        settings->setCenterSearchDialog(originalRecenter);QCOMPARE(recenter->isChecked(),originalRecenter);
        for(bool dark:{false,true}){
            dialog.setPalette(dialogPalette(dark));dialog.resize(840,590);
            for(int page=0;page<4;++page){categories->setCurrentRow(page);QVERIFY(saveDialogPreview(&dialog,QString("settings-%1-%2").arg(page).arg(dark?"dark":"light")));}
        }
        dialog.resize(720,500);categories->setCurrentRow(2);QVERIFY(saveDialogPreview(&dialog,"settings-files-small"));
        dialog.setLayoutDirection(Qt::RightToLeft);QVERIFY(saveDialogPreview(&dialog,"settings-files-rtl"));
        QVERIFY(categories->geometry().center().x()>pages->geometry().center().x());dialog.setLayoutDirection(Qt::LeftToRight);
        const auto categoryName=categories->item(0)->text();categories->item(0)->setText("[General application preferences — expanded label]");
        QVERIFY(categories->item(0)->sizeHint().height()>40);QVERIFY(saveDialogPreview(&dialog,"settings-long-category"));categories->item(0)->setText(categoryName);
        // Qt::Tool windows share Cocoa activation with their owner. Establish
        // Qt focus explicitly so this test also works from background CTest.
        QApplication::setActiveWindow(&dialog);
        categories->setCurrentRow(0);auto *language=dialog.findChild<QComboBox*>("comboBoxTranslation");categories->setFocus();QVERIFY(categories->hasFocus());QTest::keyClick(categories,Qt::Key_Tab);QTRY_VERIFY2(language->hasFocus(),qPrintable(QString("Focus moved to %1 (language policy %2)").arg(QApplication::focusWidget()?QApplication::focusWidget()->objectName():"none").arg(language->focusPolicy())));
        auto *folder=dialog.findChild<QToolButton*>("btnSelectHardCodedPath");QVERIFY(folder);QVERIFY(!folder->accessibleName().isEmpty());
        auto *done=dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);QVERIFY(done->isDefault());
        QTest::keyClick(&dialog,Qt::Key_Escape);QVERIFY(!dialog.isVisible());
#endif
    }
    void macOSSearchDialogs() {
#ifdef Q_OS_MACOS
        struct Results:ISearchResultsHandler {
            int hits=0;void newSearch(const QString)override{hits=0;}void newFileEntry(ScintillaNext*)override{}
            void newResultsEntry(const QString,int,int,int,int count=1)override{hits+=count;}void completeSearch()override{}
        } results;
        auto *editor=window->currentEditor();const auto restore=qScopeGuard([&]{editor->setText("");editor->setSavePoint();});
        editor->setText("apple apple\nApple pear\n");editor->setEmptySelection(0);const auto nativeWindow=window->winId();
        FindReplaceDialog dialog(&results,window);dialog.show();dialog.activateWindow();
        auto *find=dialog.findChild<QComboBox*>("comboFind"),*replace=dialog.findChild<QComboBox*>("comboReplace");
        auto *feedback=dialog.findChild<QStatusBar*>("searchFeedback");QVERIFY(find);QVERIFY(replace);QVERIFY(feedback);
        dialog.findChild<QRadioButton*>("radioNormalSearch")->setChecked(true);
        dialog.findChild<QCheckBox*>("checkBoxMatchCase")->setChecked(true);dialog.setFindString("apple");
        find->setFocus();QTest::keyClick(find->lineEdit(),Qt::Key_Return);
        QCOMPARE(editor->get_text_range(editor->selectionStart(),editor->selectionEnd()),QString("apple"));
        dialog.count();QVERIFY(feedback->currentMessage().contains('2'));
        for(bool dark:{false,true}){dialog.setPalette(dialogPalette(dark));dialog.resize(740,540);QVERIFY(saveDialogPreview(&dialog,dark?"find-dark":"find-light"));}
        dialog.setTab(FindReplaceDialog::REPLACE_TAB);replace->setCurrentText("orange");
        QVERIFY(replace->isVisible());QVERIFY(dialog.findChild<QPushButton*>("buttonReplace")->isDefault());
        QTest::keyClick(replace->lineEdit(),Qt::Key_Return);QVERIFY(editor->get_text_range(0,editor->length()).startsWith("orange"));
        dialog.replaceAll();QCOMPARE(editor->get_text_range(0,editor->length()),QString("orange orange\nApple pear\n"));
        QVERIFY(saveDialogPreview(&dialog,"replace-dark"));
        dialog.findChild<QRadioButton*>("radioRegexSearch")->setChecked(true);
        QVERIFY(!dialog.findChild<QCheckBox*>("checkBoxMatchWholeWord")->isEnabled());
        QVERIFY(dialog.findChild<QCheckBox*>("checkBoxRegexMatchesNewline")->isEnabled());
        dialog.findChild<QRadioButton*>("radioNormalSearch")->setChecked(true);
        dialog.setTab(FindReplaceDialog::MARK_TAB);dialog.setFindString("orange");dialog.markAll();
        QVERIFY(!replace->isVisible());QVERIFY(dialog.findChild<QPushButton*>("buttonMarkAll")->isDefault());
        QVERIFY(feedback->currentMessage().contains('2'));QVERIFY(saveDialogPreview(&dialog,"mark-dark"));dialog.clearAllMarks();
        dialog.setTab(FindReplaceDialog::FIND_TAB);dialog.setFindString("not present 😀中");dialog.find();
        QVERIFY(feedback->currentMessage().contains("No matches"));QCOMPARE(feedback->accessibleName(),feedback->currentMessage());
        QVERIFY(saveDialogPreview(&dialog,"find-no-matches-dark"));
        dialog.resize(660,460);dialog.findChild<QToolButton*>("advancedSearchOptions")->click();
        QVERIFY(dialog.findChild<QGroupBox*>("transparency")->isVisible());QVERIFY(saveDialogPreview(&dialog,"find-advanced-small"));
        dialog.setFindString(QString(600,'a'));QVERIFY(dialog.width()<900);
        QCOMPARE(window->winId(),nativeWindow);QVERIFY(!editor->isSavedToDisk());
        dialog.close();
        FindReplaceDialog reopened(&results,window);QVERIFY(reopened.findChild<QComboBox*>("comboFind")->findText("apple")>=0);
#endif
    }
    void macOSUtilityDialogs() {
#ifdef Q_OS_MACOS
        MacroSaveDialog save(window);save.show();auto *name=save.findChild<QLineEdit*>("editName");auto *ok=save.findChild<QPushButton*>("buttonOk");
        QVERIFY(!ok->isEnabled());name->setText("   ");QVERIFY(!ok->isEnabled());name->setText("Format selection — 中文");QVERIFY(ok->isEnabled());QCOMPARE(save.getName(),name->text());
        save.setPalette(dialogPalette(false));QVERIFY(saveDialogPreview(&save,"macro-save-light"));save.close();
        auto sample=std::make_unique<Macro>();sample->setName("Format selection — 中文");sample->addMacroStep(Scintilla::Message::LineDown,0,0);
        MacroManager manager;MacroRunDialog run(window,&manager);run.show();
        QVERIFY(!run.findChild<QPushButton*>("buttonRun")->isEnabled());QVERIFY(saveDialogPreview(&run,"macro-run-empty-light"));run.hide();
        manager.availableMacros().append(sample.get());run.show();QVERIFY(run.findChild<QPushButton*>("buttonRun")->isEnabled());
        QSignalSpy execute(&run,&MacroRunDialog::execute);run.findChild<QRadioButton*>("radioExecute")->setChecked(true);run.findChild<QSpinBox*>("spinTimes")->setValue(3);
        run.findChild<QPushButton*>("buttonRun")->click();QCOMPARE(execute.size(),1);QCOMPARE(execute[0][0].value<Macro*>(),sample.get());QCOMPARE(execute[0][1].toInt(),3);
        run.setPalette(dialogPalette(true));QVERIFY(saveDialogPreview(&run,"macro-run-dark"));run.close();
        MacroEditorDialog macros(window,&manager);macros.show();macros.setPalette(dialogPalette(true));
        for(const auto &id:{"btnInsertMacroStep","btnDeleteMacroStep","btnMoveMacroStepUp","btnMoveMacroStepDown","btnCopyMacro","btnDeleteMacro"}){
            auto *button=macros.findChild<QPushButton*>(id);QVERIFY(button);QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->icon().name().isEmpty());
        }
        QVERIFY(saveDialogPreview(&macros,"macro-editor-dark"));macros.close();
        auto *editor=window->currentEditor();const auto restore=qScopeGuard([&]{editor->setText("");editor->setSavePoint();});
        editor->setText("one\ntwo");editor->setEmptySelection(1);ColumnEditorDialog columns(window);columns.show();columns.setPalette(dialogPalette(false));
        columns.findChild<QGroupBox*>("gbxText")->setChecked(true);columns.findChild<QLineEdit*>("txtText")->setText("!");QVERIFY(saveDialogPreview(&columns,"column-editor-light"));
        columns.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(editor->get_text_range(0,editor->length()),QString("o!ne\nt!wo"));editor->undo();QCOMPARE(editor->get_text_range(0,editor->length()),QString("one\ntwo"));
        manager.availableMacros().clear();
#endif
    }
    void productionHtmlServiceAndHotReload() {
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        const auto project=qEnvironmentVariable("NNP_TEST_CONSUMER_HTML_PACKAGE");
        if(project.isEmpty())QSKIP("Marketplace consumer acceptance is opt-in; provide an executable HTML package.");
        QProcess packer;packer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_STAGER,project,directory+"/html-release"});QVERIFY(packer.waitForFinished(10000));QVERIFY2(packer.exitCode()==0,packer.readAllStandardError().constData());
        PluginSources::SourceSnapshot source;QString error;QVERIFY(PluginSources::PluginMetadata::readLocalFile(project+"/notepadnext-plugin.json",&source,&error));const auto id=source.resources[0].id;
        auto *engine=app->getPluginEngine();auto *installer=app->getPluginInstaller();auto *installed=app->getInstalledPluginRegistry();QVERIFY(app->getRegisteredResourceRegistry()->registerSource(source,&error));
        QVERIFY(installer->installDirectory(directory+"/html-release",source.resources[0]));QTRY_VERIFY_WITH_TIMEOUT(!installer->isBusy(),10000);QVERIFY(!installed->plugin(id).id().isEmpty());
        auto *editor=window->currentEditor();editor->setText("<h1>Unsaved HTML</h1><script>window.previewExecuted=42;</script>");app->setEditorLanguage(editor,"HTML");const auto nativeWindow=window->winId();
        window->showPluginManager();auto *manager=window->findChild<PluginManagerDialog*>();QVERIFY(manager);auto *list=manager->findChild<QListWidget*>("installedPlugins");for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole)==id)list->setCurrentRow(i);
        manager->findChild<QPushButton*>("togglePluginEnabled")->click();QCOMPARE(engine->sessionCount(),0);manager->findChild<QPushButton*>("runPluginCommand")->click();QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),10000);QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QWebEngineView*>(),10000);
        QPointer<QDockWidget> dock;for(auto *candidate:window->findChildren<QDockWidget*>())if(candidate->findChild<QWebEngineView*>())dock=candidate;QVERIFY(dock);dock->setFloating(true);dock->resize(700,680);dock->move(160,140);QTest::qWait(100);const auto geometry=dock->geometry();
        const auto read=[&](const QString &script){auto result=std::make_shared<QVariant>();auto ready=std::make_shared<bool>(false);dock->findChild<QWebEngineView*>()->page()->runJavaScript(script,[result,ready](const QVariant &value){*result=value;*ready=true;});QElapsedTimer timer;timer.start();while(!*ready&&timer.elapsed()<5000)QTest::qWait(10);return *result;};
        QTRY_VERIFY_WITH_TIMEOUT(read("document.querySelector('h1')?.textContent==='Unsaved HTML'").toBool(),10000);QTRY_COMPARE(read("window.previewExecuted").toInt(),42);
        QVERIFY(read("window.testApi=acquireNotepadNextApi();window.testApi.setState({counter:2});true").toBool());
        QVERIFY(saveDialogPreview(dock,"html-service-before-reload"));engine->reload(id);QTRY_VERIFY2_WITH_TIMEOUT(engine->pluginState(id)=="active",qPrintable(engine->pluginLog(id)),15000);
        QVERIFY(dock);QCOMPARE(dock->geometry(),geometry);QCOMPARE(window->winId(),nativeWindow);QTRY_COMPARE(read("(window.testApi??=acquireNotepadNextApi()).getState()?.counter").toInt(),2);QCOMPARE(read("document.querySelector('h1').textContent").toString(),QString("Unsaved HTML"));QCOMPARE(read("window.previewExecuted").toInt(),42);
        QVERIFY(!read("document.querySelector('textarea')").isValid());QVERIFY(saveDialogPreview(dock,"html-service-after-reload"));
        engine->uninstall(id,true);QTRY_VERIFY_WITH_TIMEOUT(engine->isIdle(),10000);QTRY_VERIFY(!dock);QVERIFY(!window->findChild<QWebEngineView*>());QCOMPARE(editor->textRange(0,editor->length()),QByteArray("<h1>Unsaved HTML</h1><script>window.previewExecuted=42;</script>"));editor->setText("");editor->setSavePoint();manager->close();
#endif
    }
    void realGitHubMarketplaceLifecycle() {
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        const auto repository=qEnvironmentVariable("NNP_TEST_LIVE_MARKETPLACE_REPOSITORY");
        if(repository.isEmpty())QSKIP("Live marketplace validation is opt-in.");
        auto *sources=app->getRegisteredResourceRegistry();auto *installed=app->getInstalledPluginRegistry();auto *engine=app->getPluginEngine();
        QString error;for(const auto &source:sources->sources())QVERIFY(sources->removeSource(source.key(),&error));
        window->showPluginManager();auto *manager=window->findChild<PluginManagerDialog*>();QVERIFY(manager);
        auto *catalog=manager->findChild<QListWidget*>("discoverPlugins"),*local=manager->findChild<QListWidget*>("installedPlugins");auto *tabs=manager->findChild<QTabWidget*>("pluginManagerTabs");
        bool inside=false;QStringList dialogs;QTimer driver;
        connect(&driver,&QTimer::timeout,manager,[&]{
            if(inside)return;auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            const auto name=dialog->objectName();
            if(name=="pluginUpToDate"){dialogs.append(name);dialog->accept();return;}
            if(name!="registerSourceReview"&&name!="releaseInstallReview"&&name!="uninstallPluginReview")return;
            inside=true;const auto guard=qScopeGuard([&]{inside=false;});dialogs.append(name);saveDialogPreview(dialog,"native-marketplace-"+name);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });driver.start(15);const auto guard=qScopeGuard([&]{driver.stop();});
        QTimer::singleShot(0,manager,[&]{auto *entry=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(entry);entry->findChild<QLineEdit*>("repositoryInput")->setText(repository);entry->findChild<QLineEdit*>("sourceRefInput")->clear();entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        auto *registerButton=manager->findChild<QPushButton*>("registerResources");QVERIFY(registerButton);registerButton->click();QTRY_VERIFY_WITH_TIMEOUT(dialogs.contains("registerSourceReview"),35000);tabs->setCurrentIndex(1);QTRY_COMPARE(catalog->count(),2);
        const auto select=[](QListWidget *list,const QString &id){for(int n=0;n<list->count();++n)if(list->item(n)->data(Qt::UserRole).toString()==id){list->setCurrentRow(n);return true;}return false;};
        const auto nativeWindow=window->winId();QList<ScintillaNext*> previewEditors;
        const QStringList ids{"kmerdan.markdown-preview","kmerdan.html-preview"};
        const auto cleanup=qScopeGuard([&]{for(const auto &id:ids)if(!installed->plugin(id).id().isEmpty())engine->uninstall(id,true);});
        for(const auto &id:ids){
            tabs->setCurrentIndex(1);QVERIFY(select(catalog,id));const auto installs=dialogs.count("releaseInstallReview");manager->findChild<QPushButton*>("installPlugin")->click();QTRY_VERIFY_WITH_TIMEOUT(dialogs.count("releaseInstallReview")>installs,35000);QTRY_VERIFY_WITH_TIMEOUT(!installed->plugin(id).id().isEmpty(),35000);
            QCOMPARE(installed->plugin(id).package.metadata["engines"].toObject()["api"].toString(),"^0.3.0");QCOMPARE(installed->plugin(id).package.metadata["category"].toString(),"editor-preview");QVERIFY(engine->setEnabled(id,true,&error));
            window->newFile();auto *editor=window->currentEditor();previewEditors.append(editor);const bool html=id.endsWith("html-preview");editor->setText(html?"<h1>Native Marketplace</h1>":"# Native Marketplace\n");app->setEditorLanguage(editor,html?"HTML":"Markdown");editor->setSavePoint();
            window->showPluginManager();tabs->setCurrentIndex(0);QVERIFY(select(local,id));manager->findChild<QPushButton*>("runPluginCommand")->click();QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),15000);
            QTRY_VERIFY_WITH_TIMEOUT(([&]{for(auto *dock:window->findChildren<QDockWidget*>())if(dock->objectName().contains(id)&&dock->findChild<QWebEngineView*>())return true;return false;})(),10000);
            window->showPluginManager();tabs->setCurrentIndex(0);QVERIFY(select(local,id));const auto upToDate=dialogs.count("pluginUpToDate");manager->findChild<QPushButton*>("updatePlugin")->click();QTRY_VERIFY_WITH_TIMEOUT(dialogs.count("pluginUpToDate")>upToDate,35000);
        }
        QCOMPARE(engine->sessionCount(),2);QCOMPARE(window->winId(),nativeWindow);QVERIFY(saveDialogPreview(manager,"native-marketplace-installed"));
        for(const auto &id:ids){window->showPluginManager();tabs->setCurrentIndex(0);QVERIFY(select(local,id));manager->findChild<QPushButton*>("uninstallPlugin")->click();QTRY_VERIFY_WITH_TIMEOUT(installed->plugin(id).id().isEmpty(),10000);}
        QTRY_VERIFY(engine->isIdle());QCOMPARE(catalog->count(),2);QCOMPARE(window->winId(),nativeWindow);manager->close();for(auto *editor:previewEditors)window->closeFile(editor);
        qInfo()<<"Live native Marketplace: canonical repo URL, two product previews, checksummed release installs, explicit activation, panels, independent up-to-date checks and uninstall; window retained.";
#endif
    }
    void productionManagerAndExit() {
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
        const auto previewProject=QString(PLUGIN_TEST_PREVIEW);
        QProcess packer;packer.start(PLUGIN_TEST_NODE,{PLUGIN_TEST_STAGER,previewProject,directory+"/release"});QVERIFY(packer.waitForFinished(10000));QVERIFY2(packer.exitCode()==0,packer.readAllStandardError().constData());
        PluginSources::SourceSnapshot snapshot;QString error;
        QVERIFY2(PluginSources::PluginMetadata::readLocalFile(previewProject+"/notepadnext-plugin.json",&snapshot,&error),qPrintable(error));
        auto *sources=app->getRegisteredResourceRegistry();auto *installed=app->getInstalledPluginRegistry();auto *installer=app->getPluginInstaller();auto *engine=app->getPluginEngine();
        window->showPluginManager();auto *manager=window->findChild<PluginManagerDialog*>();QVERIFY(manager);
        QTimer modalDriver;int confirmations=0;bool selectedPackage=false;QString selectedPackagePath;
        QSignalSpy installationResult(installer,&PluginPackages::InstallationService::finished);
        connect(&modalDriver,&QTimer::timeout,manager,[&]{
            if(auto *box=qobject_cast<QDialog*>(QApplication::activeModalWidget());box&&(box->objectName()=="registerSourceReview"||box->objectName()=="localInstallReview")) { ++confirmations;box->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); }
            else if(auto *picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if(!selectedPackage){selectedPackage=true;picker->setDirectory(directory+"/release");}
                selectedPackagePath=picker->selectedFiles().value(0);
                // The QFileSystemModel loads asynchronously; accept only when
                // the selection is the package, rather than its parent folder.
                if(QFileInfo(selectedPackagePath).canonicalFilePath()==QFileInfo(directory+"/release").canonicalFilePath())QMetaObject::invokeMethod(picker,"accept");
            }
        });
        modalDriver.start(10);
        QTimer::singleShot(0,manager,[&]{
            auto *entry=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(entry);
            entry->findChild<QComboBox*>("sourceKindInput")->setCurrentIndex(1);
            entry->findChild<QLineEdit*>("localMetadataInput")->setText(snapshot.localPath);
            entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        manager->findChild<QPushButton*>("registerResources")->click();
        qInfo()<<"Local registration reviewed";
        QCOMPARE(confirmations,1);QCOMPARE(installed->plugins().size(),0);QCOMPARE(engine->sessionCount(),0);
        bool registered=false;for(const auto &source:sources->sources())if(source.key()==snapshot.key())registered=true;QVERIFY(registered);
        auto *available=manager->findChild<QListWidget*>("discoverPlugins");
        for(int row=0;row<available->count();++row)if(available->item(row)->data(Qt::UserRole).toString()==snapshot.resources[0].id)available->setCurrentRow(row);
        QVERIFY(!manager->findChild<QPushButton*>("installPlugin")->isEnabled());
        QVERIFY(manager->findChild<QPushButton*>("installLocalPlugin")->isEnabled());
        const bool nativeDialogsDisabled=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        qInfo()<<"Opening local package picker";
        manager->findChild<QPushButton*>("installLocalPlugin")->click();
        qInfo()<<"Local package review completed";
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDialogsDisabled);modalDriver.stop();
        QVERIFY(selectedPackage);QCOMPARE(confirmations,2);
        QTRY_VERIFY_WITH_TIMEOUT(!installer->isBusy(),10000);
        QVERIFY2(installed->plugins().size()==1,qPrintable(QString("Selected=%1; status=%2; result=%3").arg(selectedPackagePath,manager->findChild<QLabel*>("registrationStatus")->text(),installationResult.isEmpty()?QString("no installation result"):installationResult.last().last().toString())));
        QCOMPARE(engine->sessionCount(),0);QVERIFY(!installed->plugins()[0].enabled);
        manager->findChild<QTabWidget*>("pluginManagerTabs")->setCurrentIndex(0);
        QCOMPARE(manager->findChild<QListWidget*>("installedPlugins")->count(),1);
        manager->findChild<QPushButton*>("togglePluginEnabled")->click();QVERIFY(installed->plugins()[0].enabled);
        auto *editor=window->currentEditor();editor->setText("production app unsaved 😀中\n\n- one\n- two\n\n```text\nproduction code\n```\n");
        app->setEditorLanguage(editor,"Markdown");
        auto *documents=app->getPluginDocumentBridge();
        QCOMPARE(documents->active().toObject()["documentId"].toString(),documents->idFor(editor));
        QAction *command=nullptr;for(auto *action:engine->findChildren<QAction*>())if(action->objectName().startsWith("pluginCommand:")){command=action;break;}QVERIFY(command);command->trigger();
        const auto id=snapshot.resources[0].id;QTRY_COMPARE_WITH_TIMEOUT(engine->pluginState(id),QString("active"),10000);QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QWebEngineView*>(),10000);
        QTRY_COMPARE(manager->findChild<QLabel*>("pluginPanelCountState")->text(),QString("1 open"));
        QPointer<QWebEnginePage> page=window->findChild<QWebEngineView*>()->page();auto preview=std::make_shared<QJsonObject>();
        QTRY_VERIFY2_WITH_TIMEOUT(([&]{
            if(!page)return false;
            page->runJavaScript("JSON.stringify({heading:document.querySelector('h1')?.textContent,body:document.body?.innerText})",[preview](const QVariant &v){*preview=QJsonDocument::fromJson(v.toString().toUtf8()).object();});
            return (*preview)["heading"].toString()==QStringLiteral("production app unsaved 😀中");
        })(),qPrintable(QString("Preview=%1; active=%2; target=%3; plugin=%4").arg(QString::fromUtf8(QJsonDocument(*preview).toJson(QJsonDocument::Compact)),QString::fromUtf8(QJsonDocument(documents->active().toObject()).toJson(QJsonDocument::Compact)),documents->idFor(editor),engine->pluginLog(id))),10000);
        const auto previousFamily=app->getSettings()->fontName();const auto previousSize=app->getSettings()->fontSize();const auto previousPalette=window->palette();
        const auto restoreAppearance=qScopeGuard([&]{app->getSettings()->setFontName(previousFamily);app->getSettings()->setFontSize(previousSize);if(window)window->setPalette(previousPalette);});
        const auto previewUrl=page->url();const auto version=documents->info(documents->idFor(editor)).value("version");
        app->getSettings()->setFontName("Menlo");app->getSettings()->setFontSize(18);
        QPalette palette=previousPalette;palette.setColor(QPalette::Base,QColor("#161d24"));palette.setColor(QPalette::Text,QColor("#edf1f5"));window->setPalette(palette);
        QTRY_VERIFY_WITH_TIMEOUT(([&]{
            if(!page)return false;page->runJavaScript("JSON.stringify({family:getComputedStyle(document.querySelector('code')).fontFamily,size:parseFloat(getComputedStyle(document.querySelector('code')).fontSize),background:getComputedStyle(document.body).backgroundColor,heading:document.querySelector('h1').textContent})",[preview](const QVariant &value){*preview=QJsonDocument::fromJson(value.toString().toUtf8()).object();});
            return (*preview)["family"].toString().contains("Menlo")&&(*preview)["background"]=="rgb(22, 29, 36)";
        })(),5000);
        QVERIFY(qAbs((*preview)["size"].toDouble()-18.0*window->logicalDpiY()/72.0)<0.1);QCOMPARE(editor->styleFont(STYLE_DEFAULT),QByteArray("Menlo"));QCOMPARE(page->url(),previewUrl);QCOMPARE(documents->info(documents->idFor(editor))["version"],version);QCOMPARE((*preview)["heading"].toString(),QStringLiteral("production app unsaved 😀中"));
        QVERIFY(manager->grab().save(PLUGIN_TEST_SCREENSHOT));manager->close();editor->setSavePoint();
        // Exercise the real last-window path while a plugin is active. Its
        // aboutToQuit barrier must finish the process before exec() returns.
        app->setQuitOnLastWindowClosed(true);QTimer::singleShot(0,window,&QWidget::close);QElapsedTimer deadline;deadline.start();
        app->exec();QVERIFY(deadline.elapsed()<8000);QVERIFY(engine->isIdle());QCOMPARE(engine->sessionCount(),0);QVERIFY(installed->plugin(id).enabled);
#else
        window->currentEditor()->setSavePoint();app->setQuitOnLastWindowClosed(true);QTimer::singleShot(0,window,&QWidget::close);app->exec();
#endif
        window=nullptr;
        // macOS may deliver activation after the last window has closed.
        // The application must tolerate that signal while exiting.
        app->applicationStateChanged(Qt::ApplicationActive);
    }
    void cleanupTestCase() {
        // Also release the real process/profile when a preceding assertion
        // failed. JavaScript callbacks above own their result independently
        // of the test stack, so late replies cannot access a dead reference.
#ifdef NOTEPADNEXT_PLUGINS
        if(auto *engine=app->getPluginEngine()){engine->shutdown();QTRY_VERIFY_WITH_TIMEOUT(engine->isIdle(),5000);}
#endif
        if(window){if(auto *editor=window->currentEditor())editor->setSavePoint();window->close();window=nullptr;}
    }
};
// Live UI automation operates a temporary profile. Avoid desktop keystrokes
// entering those fixtures while the user works in another app. Programmatic
// controls used by the live driver are unaffected; regular keyboard tests do
// not opt into this guard.
class ExternalInputGuard final:public QObject {
    bool eventFilter(QObject *,QEvent *event)override {
        switch(event->type()) {
        case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::InputMethod:
        case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::MouseButtonDblClick:case QEvent::Wheel:return true;
        default:return false;
        }
    }
};
int main(int argc,char **argv) {
    QTemporaryDir profile;
    QString profilePath=profile.path();
#ifdef PLUGIN_APPLICATION_EXIT_PROBE
    const auto exitScenario=qEnvironmentVariable("NNP_APPLICATION_EXIT_SCENARIO");
    if(!exitScenario.isEmpty()) {
        profilePath=qEnvironmentVariable("NNP_APPLICATION_EXIT_PROFILE");
        if(profilePath.isEmpty()||!QFileInfo(profilePath).isDir())return 22;
    }
#endif
    QGuiApplication::setApplicationDisplayName(APP_DISPLAY_NAME);QApplication::setOrganizationName("NotepadNextPluginTests");QApplication::setApplicationName("App-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profilePath);
#ifdef NOTEPADNEXT_PLUGIN_WEBVIEWS
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);PluginWebviews::registerScheme();
#endif
    int appArgc=1;char name[]="PluginApplicationTests";char *appArgv[]={name,nullptr};NotepadNextApplication app(appArgc,appArgv,profilePath+"/data");
    ExternalInputGuard inputGuard;if(qEnvironmentVariableIsSet("NNP_TEST_BLOCK_EXTERNAL_INPUT"))app.installEventFilter(&inputGuard);
#ifdef PLUGIN_APPLICATION_EXIT_PROBE
    if(!exitScenario.isEmpty())return runApplicationExitProbe(app,profilePath,exitScenario);
#endif
    PluginApplicationTests tests;tests.app=&app;tests.directory=profile.path();
    const auto selected=qEnvironmentVariable("NNP_TEST_APPLICATION_FUNCTIONS");
    QStringList testArgs{QString::fromLocal8Bit(argv[0])};
    if(!selected.isEmpty())testArgs+=selected.split(',',Qt::SkipEmptyParts);
    else for(int i=1;i<argc;++i)testArgs.append(QString::fromLocal8Bit(argv[i]));
    const auto log=qEnvironmentVariable("NNP_TEST_QT_LOG_FILE");
    if(!log.isEmpty())testArgs<<"-o"<<"-,txt"<<"-o"<<log+",txt";
    return QTest::qExec(&tests,testArgs);
}
#include "PluginApplicationTests.moc"
