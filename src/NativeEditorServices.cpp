// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeEditorServices.h"
#include "NotepadNextApplication.h"
#include "dialogs/MainWindow.h"
#include "EditorManager.h"
#include "ScintillaNext.h"
#include "FileOperations.h"
#include "BookMarkDecorator.h"
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include <QFileInfo>
#include <QApplication>
#include <QSet>
#include <QUuid>
#include <QUrl>
using PluginRpc::rpcError;
namespace {
QString languageId(const QString &name){return name=="Normal Text"?QStringLiteral("plaintext"):name.toLower();}
QString languageName(NotepadNextApplication *app,const QString &id){for(const auto &name:app->getLanguages())if(languageId(name)==id)return name;return {};}
QJsonArray lines(BookMarkDecorator *bookmarks){QJsonArray result;for(const auto line:bookmarks->bookMarkedLines())result.append(line);return result;}
QString filePath(const QJsonValue &value){const QUrl url(value.toString());if(!url.isLocalFile())return {};const auto path=url.toLocalFile();return QFileInfo(path).isAbsolute()?path:QString();}
}
NativeEditorServices::NativeEditorServices(NotepadNextApplication *application,PluginDocuments::DocumentBridge *bridge)
    :QObject(application),app(application),documents(bridge) {
    connect(documents,&PluginDocuments::DocumentBridge::event,this,[this](const QString &type,const QJsonValue &value,qint64){
        if(type!="closed")return;const auto id=value.toObject()["documentId"].toString();
        const auto handles=decorations.keys();for(const auto &handle:handles)if(decorations[handle].documentId==id)release(handle);
    });
}
void NativeEditorServices::release(const QString &id){
    if(!decorations.contains(id))return;const auto value=decorations.take(id);
    if(value.editor&&documents->editorForDocument(value.documentId)){
        const auto current=value.editor->indicatorCurrent(),number=value.editor->indicatorValue();
        value.editor->setIndicatorCurrent(value.indicator);value.editor->indicatorClearRange(0,value.editor->length());value.editor->releaseIndicator(value.name);
        value.editor->setIndicatorCurrent(current);value.editor->setIndicatorValue(number);
    }
    emit documents->resourceEvent(value.owner,{{"resourceId",id},{"type","disposed"}});
}
void NativeEditorServices::releaseOwner(const QString &owner){publishedOwners.remove(owner);const auto handles=decorations.keys();for(const auto &id:handles)if(decorations[id].owner==owner)release(id);}
void NativeEditorServices::draw(const Decoration &value){
    if(!value.editor||!documents->editorForDocument(value.documentId))return;
    const auto current=value.editor->indicatorCurrent(),number=value.editor->indicatorValue();
    value.editor->setIndicatorCurrent(value.indicator);value.editor->setIndicatorValue(1);value.editor->indicatorClearRange(0,value.editor->length());
    QJsonObject error;if(documents->checkVersion(value.documentId,value.version,&error))for(const auto &range:value.ranges)value.editor->indicatorFillRange(range.first,range.second-range.first);
    value.editor->setIndicatorCurrent(current);value.editor->setIndicatorValue(number);
}
void NativeEditorServices::publishOwner(const QString &owner){publishedOwners.insert(owner);for(const auto &value:decorations)if(value.owner==owner)draw(value);}
int NativeEditorServices::resourceCount(const QString &owner)const{int count=0;for(const auto &value:decorations)if(value.owner==owner)++count;return count;}
QJsonValue NativeEditorServices::dispatch(const QString &method,const QJsonObject &params,QMainWindow *value,const QString &owner,std::function<bool()> allowed,QJsonObject *error) {
    *error={};const QPointer<MainWindow> window=qobject_cast<MainWindow*>(value);
    if(!window){*error=rpcError(1005,"Native window unavailable");return {};}
    if(method=="nnp.resources.dispose") {
        const auto id=params["resourceId"].toString();if(!decorations.contains(id))return QJsonValue(QJsonValue::Null);
        if(decorations[id].owner!=owner){*error=rpcError(1003,"Resource belongs to another session");return {};}
        release(id);return QJsonValue(QJsonValue::Null);
    }
    if(method=="nnp.languages.list") {QJsonArray result;for(const auto &name:app->getLanguages())result.append(QJsonObject{{"id",languageId(name)},{"name",name}});return result;}
    if(method=="nnp.languages.getKeywords") {
        const auto name=languageName(app,params["languageId"].toString());if(name.isEmpty()){*error=rpcError(-32602,"Unknown language ID");return {};}
        const auto words=app->getLanguageKeywords(name);if(words.size()>8192){*error=rpcError(1008,"Keyword list exceeds budget");return {};}
        return QJsonArray::fromStringList(words);
    }
    if(method=="nnp.files.create"||method=="nnp.files.open") {
        if(!allowed()){*error=rpcError(1004,"PluginStopping");return {};}
        QPointer<ScintillaNext> previous=window->currentEditor();QPointer<QWidget> focus=QApplication::focusWidget();QPointer<ScintillaNext> created;
        if(method=="nnp.files.create") {
            QString name;if(params.contains("languageId")){name=languageName(app,params["languageId"].toString());if(name.isEmpty()){*error=rpcError(-32602,"Unknown language ID");return {};}}
            const auto text=params["text"].toString();
            for(qsizetype n=0;n<text.size();++n){if(text[n].isHighSurrogate()){if(++n==text.size()||!text[n].isLowSurrogate()){*error=rpcError(1009,"UnsupportedEncoding");return {};}}else if(text[n].isLowSurrogate()){*error=rpcError(1009,"UnsupportedEncoding");return {};}}
            const auto bytes=text.toUtf8();window->newFile();created=window->currentEditor();created->setText("");created->addText(bytes.size(),bytes.constData());
            if(!name.isEmpty())app->setEditorLanguage(created,name);
        } else {
            const auto path=filePath(params["uri"]);if(path.isEmpty()){*error=rpcError(-32602,"Expected absolute file URI");return {};}
            const QFileInfo info(path);if(!info.isFile()||!info.isReadable()){*error=rpcError(1012,"FileIOError");return {};}
            if(info.size()>PluginDocuments::DocumentBridge::MaxSnapshotBytes){*error=rpcError(1008,"Plugin file open exceeds native document budget");return {};}
            created=app->getEditorManager()->getEditorByFilePath(path);if(!created){window->openFile(path);created=app->getEditorManager()->getEditorByFilePath(path);}
            if(!created){*error=rpcError(1012,"FileIOError");return {};}
        }
        if(!window||!allowed()){*error=rpcError(1004,"PluginStopping");return {};}
        if(params["activate"].toBool(true))window->getDockedEditor()->switchToEditor(created);
        else if(previous){window->getDockedEditor()->switchToEditor(previous);if(focus)focus->setFocus();}
        return QJsonObject{{"status","completed"},{"document",documents->nativeInfo(documents->idFor(created))}};
    }
    QString id=params["documentId"].toString();
    if(method=="nnp.editors.activate") {
        for(const auto &item:documents->editorList())if(item.toObject()["editorId"]==params["editorId"])id=item.toObject()["documentId"].toString();
    }
    if(method=="nnp.decorations.set") {
        const auto handle=params["resourceId"].toString();if(!decorations.contains(handle)){*error=rpcError(1004,"Decoration disposed");return {};}
        if(decorations[handle].owner!=owner){*error=rpcError(1003,"Resource belongs to another session");return {};}id=decorations[handle].documentId;
    }
    QPointer<ScintillaNext> editor=qobject_cast<ScintillaNext*>(documents->editorForDocument(id));
    if(!editor||!window->editors().contains(editor)){*error=rpcError(1002,"DocumentClosed");return {};}
    const bool guarded=params.contains("expectedVersion")||params.contains("expectedDocumentVersion");
    const auto version=params["expectedVersion"].toInteger(params["expectedDocumentVersion"].toInteger());
    if(guarded&&!documents->checkVersion(id,version,error))return {};
    if(!allowed()){*error=rpcError(1004,"PluginStopping");return {};}
    if(method=="nnp.editors.activate") {
        QPointer<QWidget> focus=QApplication::focusWidget();window->getDockedEditor()->switchToEditor(editor);
        if(params["preserveFocus"].toBool()){if(focus)focus->setFocus();}else editor->grabFocus();return documents->editorInfo(id);
    }
    if(method=="nnp.languages.set") {
        const auto name=languageName(app,params["languageId"].toString());if(name.isEmpty()){*error=rpcError(-32602,"Unknown language ID");return {};}
        app->setEditorLanguage(editor,name);documents->updateMetadata(id,editor->isFile()?QUrl::fromLocalFile(editor->getFilePath()):QUrl(),languageId(name));return documents->nativeInfo(id);
    }
    if(method.startsWith("nnp.bookmarks.")) {
        const auto bookmarks=editor->findChild<BookMarkDecorator*>();if(!bookmarks){*error=rpcError(1005,"Bookmarks unavailable");return {};}
        if(method.endsWith(".set")) {
            QSet<int> requested;for(const auto &line:params["lines"].toArray()){const auto number=line.toInteger(-1);if(number<0||number>=editor->lineCount()){*error=rpcError(-32602,"Bookmark line out of bounds");return {};}requested.insert(int(number));}
            QSet<int> final;for(const auto line:bookmarks->bookMarkedLines())final.insert(line);for(const auto line:requested){if(params["marked"].toBool())final.insert(line);else final.remove(line);}
            if(final.size()>4096){*error=rpcError(1008,"Bookmark list exceeds budget");return {};}
            for(const auto line:requested){if(params["marked"].toBool())bookmarks->addBookmark(line);else bookmarks->removeBookmark(line);}
            documents->lifecycle("bookmarksChanged",id);
        }
        const auto result=lines(bookmarks);if(result.size()>4096){*error=rpcError(1008,"Bookmark list exceeds budget");return {};}
        return QJsonObject{{"documentId",id},{"version",documents->info(id)["version"]},{"lines",result}};
    }
    if(method=="nnp.decorations.create") {
        if(resourceCount(owner)>=8){*error=rpcError(1007,"Decoration style quota exceeded");return {};}
        const auto handle=QUuid::createUuid().toString(QUuid::WithoutBraces),name="plugin-decoration:"+owner+":"+handle;
        const auto slot=editor->allocateIndicator(name);if(slot<0){*error=rpcError(1007,"Native indicators exhausted");return {};}
        editor->indicSetStyle(slot,params["style"]=="underline"?INDIC_SQUIGGLE:INDIC_ROUNDBOX);
        const auto hex=params["color"].toString().mid(1).toUInt(nullptr,16);const auto color=((hex>>16)&0xff)|(hex&0x00ff00)|((hex&0xff)<<16);
        editor->indicSetFore(slot,color);editor->indicSetAlpha(slot,70);editor->indicSetOutlineAlpha(slot,130);
        decorations.insert(handle,{owner,id,name,editor,slot});return QJsonObject{{"resourceId",handle}};
    }
    if(method=="nnp.decorations.set") {
        QVector<QPair<sptr_t,sptr_t>> ranges;qsizetype work=0;
        for(const auto &item:params["ranges"].toArray()) {
            const auto range=item.toObject();for(const auto &key:QStringList{"start","end"}){const auto line=range[key].toObject()["line"].toInteger(-1);if(line>=0&&line<editor->lineCount())work+=editor->lineEndPosition(line)-editor->positionFromLine(line);}
            if(work>PluginDocuments::DocumentBridge::MaxSnapshotBytes){*error=rpcError(1008,"Decoration mapping exceeds aggregate budget");return {};}
            sptr_t start,end;if(!documents->boundedBytePosition(id,range["start"].toObject(),&start,error)||!documents->boundedBytePosition(id,range["end"].toObject(),&end,error))return {};
            if(end<start){*error=rpcError(-32602,"Reversed decoration range");return {};}ranges.append({start,end});
        }
        auto &style=decorations[params["resourceId"].toString()];style.version=version;style.ranges=std::move(ranges);
        if(publishedOwners.contains(owner))draw(style);
        return QJsonValue(QJsonValue::Null);
    }
    if(method.startsWith("nnp.files.")) {
        QPointer<ScintillaNext> target=editor;
        const auto guard=[&,target](const QString &path){
            if(!target||!window||!allowed()){*error=rpcError(1004,"PluginStopping");return false;}
            if(!documents->checkVersion(id,version,error))return false;
            if(!path.isEmpty()) {
                QFileInfo chosen(path);const auto canonical=chosen.exists()?chosen.canonicalFilePath():chosen.absoluteFilePath();
                for(const auto &other:window->editors())if(other!=target&&other->isFile()) {
                    QFileInfo existing(other->getFilePath());if(existing.canonicalFilePath()==canonical){*error=rpcError(1001,"Save target already belongs to another document");return false;}
                }
            }
            return true;
        };
        if(method=="nnp.files.close") {
            const auto result=FileOperations::approveClose(editor,window,params["policy"]=="ifClean",guard);
            if(!error->isEmpty())return {};if(result.status=="failed"){*error=rpcError(1012,"FileIOError");return {};}
            if(result.status!="completed")return QJsonObject{{"status",result.status},{"document",documents->nativeInfo(id)}};
            if(!target||!window||!allowed()){*error=rpcError(1004,"PluginStopping");return {};}
            target->close();if(window&&window->editorCount()==0)window->newFile();
            return QJsonObject{{"status","completed"},{"document",QJsonValue(QJsonValue::Null)}};
        }
        const auto path=params.contains("uri")?filePath(params["uri"]):QString();
        if(params.contains("uri")&&path.isEmpty()){*error=rpcError(-32602,"Expected absolute file URI");return {};}
        const auto result=FileOperations::save(editor,window,method=="nnp.files.saveAs",path,guard);
        if(!error->isEmpty())return {};if(result.status=="failed"){*error=rpcError(1012,"FileIOError",QJsonObject{{"fileError",int(result.error)}});return {};}
        return QJsonObject{{"status",result.status},{"document",documents->nativeInfo(id)}};
    }
    *error=rpcError(1005,"UnsupportedCapability");return {};
}
