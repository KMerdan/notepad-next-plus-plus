// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include "../../SearchService.h"
#include "../../EditorBufferOperations.h"
#include <QElapsedTimer>
#include <QStringDecoder>
#include <QTimer>
#include <memory>
#include <utility>
using PluginRpc::rpcError;
namespace PluginDocuments {
QJsonObject DocumentBridge::nativeLimits() {
    return {{"maxRangeUtf8Bytes",MaxRangeBytes},{"maxPositionLineUtf8Bytes",MaxRangeBytes},
        {"maxSearchUtf8Bytes",MaxSnapshotBytes},{"maxSearchResults",1000},{"maxSubscriptions",64},
        {"maxDecorationStylesPerOwner",8},{"maxDecorationRanges",1024}};
}
ScintillaEdit *DocumentBridge::editorForDocument(const QString &id) const { return buffers.value(id).editor; }
QJsonObject DocumentBridge::editorInfo(const QString &id) const {
    const auto buffer=buffers.value(id); if(!buffer.editor)return {};
    return {{"editorId",buffer.editorId},{"documentId",id},{"windowId",windowId}};
}
QJsonArray DocumentBridge::editorList() const {
    QJsonArray result;for(auto i=buffers.begin();i!=buffers.end();++i)if(i->editor)result.append(editorInfo(i.key()));return result;
}
bool DocumentBridge::checkVersion(const QString &id,qint64 version,QJsonObject *error) const {
    *error={};if(!editorForDocument(id)){*error=rpcError(1002,"DocumentClosed");return false;}
    if(buffers[id].version!=version){*error=rpcError(1001,"VersionConflict",QJsonObject{{"expectedVersion",version},{"actualVersion",buffers[id].version}});return false;}return true;
}
bool DocumentBridge::boundedBytePosition(const QString &id,const QJsonObject &position,sptr_t *byte,QJsonObject *error) const {
    const auto editor=editorForDocument(id);if(!editor){*error=rpcError(1002,"DocumentClosed");return false;}
    const auto line=position["line"].toInteger(-1);
    if(line>=0&&line<editor->lineCount()&&editor->lineEndPosition(line)-editor->positionFromLine(line)>MaxRangeBytes) {
        *error=rpcError(1008,"Position line exceeds native mapping budget");return false;
    }
    return bytePosition(id,position,byte,error);
}
QJsonObject DocumentBridge::readRange(const QJsonObject &request,QJsonObject *error) const {
    const auto id=request["documentId"].toString();if(!checkVersion(id,request["expectedVersion"].toInteger(),error))return {};
    sptr_t start,end;const auto range=request["range"].toObject();
    if(!boundedBytePosition(id,range["start"].toObject(),&start,error)||!boundedBytePosition(id,range["end"].toObject(),&end,error))return {};
    const auto budget=request.contains("maxBytes")?request["maxBytes"].toInteger():MaxRangeBytes;
    if(end<start){*error=rpcError(-32602,"Reversed text range");return {};}
    if(budget<1||budget>MaxRangeBytes||end-start>budget){*error=rpcError(1008,"Range exceeds native read budget");return {};}
    const auto editor=editorForDocument(id);if(editor->codePage()!=SC_CP_UTF8){*error=rpcError(1009,"UnsupportedEncoding");return {};}
    const auto bytes=editor->get_text_range(start,end);QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);const QString text=decoder.decode(bytes);
    if(decoder.hasError()){*error=rpcError(1009,"UnsupportedEncoding");return {};}
    return {{"documentId",id},{"version",buffers[id].version},{"range",range},{"text",text},{"positionEncoding","utf-16"}};
}
QJsonObject DocumentBridge::revealRange(const QJsonObject &request,QJsonObject *error) {
    const auto id=documentForEditor(request["editorId"].toString());if(!checkVersion(id,request["expectedDocumentVersion"].toInteger(),error))return {};
    sptr_t start,end;const auto range=request["range"].toObject();
    if(!boundedBytePosition(id,range["start"].toObject(),&start,error)||!boundedBytePosition(id,range["end"].toObject(),&end,error))return {};
    if(end<start){*error=rpcError(-32602,"Reversed text range");return {};}
    const auto editor=editorForDocument(id);editor->ensureVisible(editor->lineFromPosition(start));editor->ensureVisible(editor->lineFromPosition(end));editor->scrollRange(end,start);
    return editorInfo(id);
}
QJsonObject DocumentBridge::editorOptions(const QString &editorId,QJsonObject *error) const {
    *error={};const auto id=documentForEditor(editorId);const auto editor=editorForDocument(id);
    if(!editor){*error=rpcError(1002,"DocumentClosed");return {};}
    return {{"editorId",editorId},{"tabWidth",qint64(editor->tabWidth())},{"indentWidth",qint64(editor->indent())},{"useTabs",editor->useTabs()}};
}
QJsonObject DocumentBridge::convertLineEndings(const QJsonObject &request,QJsonObject *error) {
    const auto id=request["documentId"].toString();if(!checkVersion(id,request["expectedVersion"].toInteger(),error))return {};
    const auto editor=editorForDocument(id);if(editor->readOnly()){*error=rpcError(1003,"ReadOnlyDocument");return {};}
    snapshot(id,error);if(!error->isEmpty())return {};
    const auto mode=request["lineEnding"].toString();if(mode!="lf"&&mode!="crlf"&&mode!="cr"){*error=rpcError(-32602,"Invalid line ending");return {};}
    const auto eol=mode=="crlf"?SC_EOL_CRLF:mode=="cr"?SC_EOL_CR:SC_EOL_LF;
    EditorBufferOperations::convertLineEndings(editor,eol);
    emitEvent("metadataChanged",info(id));return nativeInfo(id);
}
void DocumentBridge::queueNativeEvent(QJsonObject value,bool immediate) {
    if(!nativeObservers)return;
    if(immediate){flushNativeEvents();emit nativeEvent(value,++nativeEventSequence);return;}
    const auto id=value["document"].toObject()["documentId"].toString();
    const auto key=value["type"].toString()+":"+id;
    if(pendingNativeEvents.size()>=128&&!pendingNativeEvents.contains(key)) {
        flushNativeEvents();value={{"type","reset"},{"document",value["document"]},{"reason","notificationOverflow"}};
    }
    pendingNativeEvents.insert(key,value);
    if(!nativeFlushScheduled){nativeFlushScheduled=true;QTimer::singleShot(0,this,[this]{flushNativeEvents();});}
}
void DocumentBridge::flushNativeEvents() {
    nativeFlushScheduled=false;const auto pending=std::exchange(pendingNativeEvents,{});
    auto keys=pending.keys();keys.sort();for(const auto &key:keys)emit nativeEvent(pending[key],++nativeEventSequence);
}
void DocumentBridge::nativeDocumentEvent(const QString &type,const QJsonValue &document) {
    if(!nativeObservers)return;
    const auto id=document.toObject()["documentId"].toString();QJsonValue metadata(QJsonValue::Null);
    QString editorId;
    if(!id.isEmpty()) {
        if(type=="closed") {
            editorId=nativeEditorIds.take(id);
            auto closed=nativeMetadataCache.take(id);const auto current=document.toObject();
            for(auto i=current.begin();i!=current.end();++i)closed[i.key()]=i.value();
            metadata=closed.isEmpty()?document:QJsonValue(closed);
        }
        else {const auto value=nativeInfo(id);if(!value.isEmpty()){metadata=value;nativeMetadataCache[id]=value;editorId=editorInfo(id)["editorId"].toString();nativeEditorIds[id]=editorId;}}
    }
    QJsonObject value{{"type",type},{"document",metadata}};
    if(!editorId.isEmpty())value["editorId"]=editorId;
    if(type=="reset")value["reason"]="bufferReset";
    queueNativeEvent(value,type=="opened"||type=="closed"||type=="activeChanged"||type=="reset");
}
void DocumentBridge::lifecycle(const QString &type,const QString &id) {
    queueNativeEvent({{"type",type},{"document",nativeInfo(id)}},true);
}
void DocumentBridge::retainNativeStream() {
    if(++nativeObservers!=1)return;
    for(const auto &item:nativeList()){const auto value=item.toObject();const auto id=value["documentId"].toString();nativeMetadataCache[id]=value;nativeEditorIds[id]=editorInfo(id)["editorId"].toString();}
}
void DocumentBridge::releaseNativeStream() {
    if(nativeObservers>0)--nativeObservers;
    if(!nativeObservers){pendingNativeEvents.clear();nativeMetadataCache.clear();nativeEditorIds.clear();}
}
QJsonObject DocumentBridge::boundedSelections(const QString &editorId,QJsonObject *error) {
    *error={};const auto id=documentForEditor(editorId);const auto editor=editorForDocument(id);
    if(!editor){*error=rpcError(1002,"DocumentClosed");return {};}
    qsizetype work=0;
    if(editor->selections()>256){*error=rpcError(1008,"Too many selections");return {};}
    for(sptr_t n=0;n<editor->selections();++n)for(const auto byte:{editor->selectionNAnchor(n),editor->selectionNCaret(n)}) {
        const auto line=editor->lineFromPosition(byte),length=editor->lineEndPosition(line)-editor->positionFromLine(line);work+=length;
        if(length>MaxRangeBytes||work>MaxSnapshotBytes){*error=rpcError(1008,"Selection mapping exceeds native budget");return {};}
    }
    return selections(editorId,error);
}
QJsonValue DocumentBridge::extension(const QString &method,const QJsonObject &params,QMainWindow *window,const QString &owner,std::function<bool()> allowed,QJsonObject *error) {
    *error={};if(!extensionHandler){*error=rpcError(1005,"UnsupportedCapability");return {};}
    return extensionHandler(method,params,window,owner,std::move(allowed),error);
}
void DocumentBridge::releaseOwner(const QString &owner){if(releaseResources)releaseResources(owner);}
int DocumentBridge::ownedResourceCount(const QString &owner)const{return countResources?countResources(owner):0;}

void DocumentBridge::search(const QJsonObject &request,bool countOnly,std::function<bool()> allowed,std::function<void(const QJsonValue &,const QJsonObject &)> completed) {
    const auto id=request["documentId"].toString();QJsonObject error;
    if(!checkVersion(id,request["expectedVersion"].toInteger(),&error)){completed({},error);return;}
    const auto editor=editorForDocument(id);
    if(editor->length()>MaxSnapshotBytes){completed({},rpcError(1008,"Search exceeds native document budget"));return;}
    if(editor->codePage()!=SC_CP_UTF8){completed({},rpcError(1009,"UnsupportedEncoding"));return;}
    // Validate encoding before returning ranges, without changing target/search state.
    snapshot(id,&error);if(!error.isEmpty()){completed({},error);return;}
    sptr_t start=0,end=editor->length();
    if(request.contains("range")) {const auto range=request["range"].toObject();if(!boundedBytePosition(id,range["start"].toObject(),&start,&error)||!boundedBytePosition(id,range["end"].toObject(),&end,&error)){completed({},error);return;}}
    if(request.contains("cursor")) {sptr_t cursor;if(!boundedBytePosition(id,request["cursor"].toObject(),&cursor,&error)){completed({},error);return;}if(cursor<start||cursor>end){completed({},rpcError(-32602,"Search cursor outside range"));return;}start=cursor;}
    if(end<start||request["text"].toString().isEmpty()){completed({},rpcError(-32602,"Invalid search range or text"));return;}
    struct Job {sptr_t cursor,end;QJsonArray matches;int count=0;};
    const auto job=std::make_shared<Job>();job->cursor=start;job->end=end;
    const auto text=request["text"].toString().toUtf8();const auto version=request["expectedVersion"].toInteger();const auto limit=int(request["maxResults"].toInteger(1000));
    const auto flags=(request["matchCase"].toBool()?SCFIND_MATCHCASE:0)|(request["wholeWord"].toBool()?SCFIND_WHOLEWORD:0);
    auto step=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weak=step;
    *step=[this,id,version,text,flags,limit,countOnly,allowed,completed,job,weak] {
        QJsonObject error;
        if(!allowed()){completed({},rpcError(1006,"Cancelled"));return;}
        if(!checkVersion(id,version,&error)){completed({},error);return;}
        QElapsedTimer elapsed;elapsed.start();int scanned=0;bool complete=false;
        while(job->cursor<job->end&&job->count<limit&&scanned++<32&&elapsed.elapsed()<4) {
            const auto editor=editorForDocument(id);
            const auto boundary=qMin<sptr_t>(job->end,job->cursor+64*1024);
            const auto scanEnd=qMin<sptr_t>(job->end,boundary+text.size()*4+4);
            const auto match=SearchService::findLiteral(editor,text,flags,job->cursor,scanEnd,true);
            if(match.cpMin==INVALID_POSITION){
                if(scanEnd>=job->end){job->cursor=job->end;complete=true;break;}
                job->cursor=editor->positionAfter(editor->positionBefore(boundary));continue;
            }
            QJsonObject begin,end;
            if(editor->lineEndPosition(editor->lineFromPosition(match.cpMin))-editor->positionFromLine(editor->lineFromPosition(match.cpMin))>MaxRangeBytes||
               !deltaPosition(editor,match.cpMin,&begin)||!deltaPosition(editor,match.cpMax,&end)) {completed({},rpcError(1008,"Search match exceeds native position mapping budget"));return;}
            if(!countOnly)job->matches.append(QJsonObject{{"start",begin},{"end",end}});
            ++job->count;job->cursor=match.cpMax;
        }
        if(job->cursor>=job->end)complete=true;
        if(complete||job->count>=limit) {
            QJsonValue next(QJsonValue::Null);if(!complete){QJsonObject value;if(!deltaPosition(editorForDocument(id),job->cursor,&value)){completed({},rpcError(1009,"UnsupportedEncoding"));return;}next=value;}
            completed(QJsonObject{{"documentId",id},{"version",version},{"matches",job->matches},{"count",job->count},{"complete",complete},{"next",next}},{});return;
        }
        if(const auto next=weak.lock())QTimer::singleShot(0,this,[next]{(*next)();});
    };
    QTimer::singleShot(0,this,[step]{(*step)();});
}
}
