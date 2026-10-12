// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include <QFontDatabase>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
using PluginRpc::rpcError;
namespace PluginDocuments {
namespace {
bool nonnegativeInteger(const QJsonValue &value) {
    return value.isDouble() && value.toDouble() >= 0 && value.toDouble() <= 9007199254740991.0 && value.toDouble() == std::floor(value.toDouble());
}
bool validUnicode(const QString &text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()) { if (++i == text.size() || !text[i].isLowSurrogate()) return false; }
        else if (text[i].isLowSurrogate()) return false;
    }
    return true;
}
}
DocumentBridge::DocumentBridge(QObject *parent) : QObject(parent) {}
QString DocumentBridge::attach(ScintillaEdit *editor, const QUrl &uri, const QString &language) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto previous = idFor(editor); if (!previous.isEmpty()) return previous;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    buffers.insert(id, {editor, uri.isEmpty() ? QUrl(QStringLiteral("untitled:") + id) : uri, language});
    buffers[id].editorId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    buffers[id].selectionBytes = rawSelections(editor);
    buffers[id].connections.append(connect(editor, &ScintillaEdit::updateUi, this, [this, id](Scintilla::Update) { refreshSelection(id); }));
    buffers[id].connections.append(connect(editor, &ScintillaEdit::modified, this, [this, id](Scintilla::ModificationFlags type,sptr_t position,sptr_t length,sptr_t,const QByteArray &bytes) {
        if(!buffers.contains(id))return;
        using Flag=Scintilla::ModificationFlags;
        const auto flags=static_cast<int>(type);auto &buffer=buffers[id];
        const bool beforeInsert=flags&static_cast<int>(Flag::BeforeInsert),beforeDelete=flags&static_cast<int>(Flag::BeforeDelete);
        if((beforeInsert||beforeDelete)&&deltaObservers&&!buffer.applying) {
            buffer.pending={};QJsonObject start,end;
            const auto last=beforeDelete?position+length:position;
            if(buffer.editor->length()<=MaxSnapshotBytes&&deltaPosition(buffer.editor,position,&start)&&deltaPosition(buffer.editor,last,&end)) {
                buffer.pending={{ {"start",start},{"end",end}},position,beforeDelete?length:0,buffer.editor->countCodeUnits(0,position),buffer.editor->countCodeUnits(position,last)};
            }
            return;
        }
        const bool inserted=flags&static_cast<int>(Flag::InsertText),deleted=flags&static_cast<int>(Flag::DeleteText);
        if(!inserted&&!deleted)return;
        const auto base=buffer.version;++buffer.version;
        // A selection guard must expire when its underlying text changes too.
        ++buffer.selectionRevision;
        const auto pending=buffer.pending;buffer.pending={};
        if(!buffer.applying) {
            if(deltaObservers) {
                if(buffer.editor->length()>MaxSnapshotBytes||bytes.size()>MaxSnapshotBytes) {
                    emitDelta({{"type","reset"},{"document",info(id)},{"reason","unmappableChange"}});
                    emitEvent("contentInvalidated",info(id));return;
                }
                QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
                const auto text=inserted?decoder.decode(bytes):QString();
                const bool valid=pending.position==position&&pending.length==(deleted?length:0)&&(!inserted||bytes.size()==length)&&!decoder.hasError()&&validUnicode(text);
                if(valid) {
                    const auto reason=(flags&static_cast<int>(Flag::Undo))?"undo":((flags&static_cast<int>(Flag::Redo))?"redo":"edit");
                    emitDelta({{"type","changed"},{"document",info(id)},{"baseVersion",base},{"changes",QJsonArray{QJsonObject{{"range",pending.range},{"rangeOffset",pending.offset},{"rangeLength",pending.units},{"text",text}}}},{"reason",reason}});
                } else emitDelta({{"type","reset"},{"document",info(id)},{"reason","unmappableChange"}});
            }
            emitEvent("contentInvalidated", info(id));
        }
    }));
    buffers[id].connections.append(connect(editor, &ScintillaEdit::savePointChanged, this, [this, id] {
        if (!buffers.contains(id)) return;
        // A batch can cross the save point while its text notifications are
        // suppressed. Publish metadata only after its final text/version.
        if (buffers[id].applying) buffers[id].metadataPending=true;
        else emitEvent("metadataChanged", info(id));
    }));
    buffers[id].connections.append(connect(editor, &QObject::destroyed, this, [this, id] { detach(id); }));
    emitEvent("opened", info(id)); return id;
}
QString DocumentBridge::idFor(ScintillaEdit *editor) const {
    for (auto it = buffers.cbegin(); it != buffers.cend(); ++it) if (it->editor == editor) return it.key();
    return {};
}
QJsonObject DocumentBridge::info(const QString &id) const {
    const auto it = buffers.constFind(id); if (it == buffers.cend()) return {};
    return {{"documentId", id}, {"uri", it->uri.toString(QUrl::FullyEncoded)}, {"languageId", it->language}, {"version", it->version},
        {"isDirty", it->editor && it->editor->modify()}, {"isReadonly", !it->editor || it->editor->readOnly()}};
}
QJsonValue DocumentBridge::active() const { return buffers.contains(activeId) && buffers[activeId].editor ? QJsonValue(info(activeId)) : QJsonValue(QJsonValue::Null); }
QFont DocumentBridge::activeEditorFont() const {
    Q_ASSERT(QThread::currentThread() == thread());
    auto font=QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto buffer=buffers.constFind(activeId);
    if(buffer!=buffers.cend()&&buffer->editor) {
        const auto family=QString::fromUtf8(buffer->editor->styleFont(STYLE_DEFAULT));
        if(!family.isEmpty())font.setFamily(family);
        const auto size=buffer->editor->styleSizeFractional(STYLE_DEFAULT)/100.0;
        if(size>0)font.setPointSizeF(size);
    }
    return font;
}
void DocumentBridge::refreshAppearance() {
    Q_ASSERT(QThread::currentThread() == thread());
    emit appearanceChanged();
}
QJsonArray DocumentBridge::list() const {
    QJsonArray result; for (auto it = buffers.cbegin(); it != buffers.cend(); ++it) if (it->editor) result.append(info(it.key())); return result;
}
void DocumentBridge::setNativeServices(const QJsonArray &catalog,
    std::function<QJsonObject(ScintillaEdit *, const QString &, const QJsonObject &, QJsonObject *)> execute,
    std::function<QJsonObject(ScintillaEdit *)> metadata) {
    nativeCatalog = catalog; nativeExecute = std::move(execute); nativeMetadata = std::move(metadata);
}
QJsonObject DocumentBridge::nativeInfo(const QString &id) const {
    auto result = info(id); const auto editor = buffers.value(id).editor;
    if (!editor) return {};
    result["byteLength"] = qint64(editor->length()); result["lineCount"] = qint64(editor->lineCount());
    result["lineEnding"] = editor->eOLMode() == SC_EOL_CRLF ? "crlf" : editor->eOLMode() == SC_EOL_CR ? "cr" : "lf";
    result["name"] = buffers.value(id).uri.fileName();
    result["encoding"] = editor->codePage() == SC_CP_UTF8 ? "utf-8" : "unknown"; result["bom"] = false;
    if (nativeMetadata) { const auto metadata = nativeMetadata(editor); for (auto it=metadata.begin();it!=metadata.end();++it) result[it.key()]=it.value(); }
    return result;
}
QJsonArray DocumentBridge::nativeList() const {
    QJsonArray result; for (auto it=buffers.cbegin();it!=buffers.cend();++it) if(it->editor) result.append(nativeInfo(it.key())); return result;
}
QString DocumentBridge::documentForEditor(const QString &id) const {
    for (auto it=buffers.cbegin();it!=buffers.cend();++it) if(it->editor && it->editorId==id) return it.key(); return {};
}
QJsonValue DocumentBridge::activeEditor() const {
    const auto buffer=buffers.value(activeId); if(!buffer.editor) return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"editorId",buffer.editorId},{"documentId",activeId},{"windowId",windowId}};
}
QJsonObject DocumentBridge::invocation(const QString &command,const QJsonArray &args) const {
    const auto editor=activeEditor().toObject();
    return {{"commandId",command},{"windowId",windowId},{"editorId",editor.isEmpty()?QJsonValue(QJsonValue::Null):editor["editorId"]},
        {"documentId",editor.isEmpty()?QJsonValue(QJsonValue::Null):editor["documentId"]},{"args",args}};
}
QJsonArray DocumentBridge::rawSelections(ScintillaEdit *editor) const {
    QJsonArray result{qint64(editor->selectionMode()),qint64(editor->mainSelection())};
    for(sptr_t n=0;n<editor->selections();++n) result.append(QJsonArray{qint64(editor->selectionNAnchor(n)),qint64(editor->selectionNCaret(n)),qint64(editor->selectionNAnchorVirtualSpace(n)),qint64(editor->selectionNCaretVirtualSpace(n))});
    return result;
}
void DocumentBridge::refreshSelection(const QString &id) {
    if(!buffers.contains(id)||!buffers[id].editor)return;
    auto &buffer=buffers[id];const auto next=rawSelections(buffer.editor);
    if(next!=buffer.selectionBytes){buffer.selectionBytes=next;++buffer.selectionRevision;}
}
QJsonObject DocumentBridge::selections(const QString &editorId,QJsonObject *error) {
    *error={};const auto id=documentForEditor(editorId);
    if(id.isEmpty()){*error=rpcError(1002,"DocumentClosed");return {};}
    refreshSelection(id);const auto buffer=buffers.value(id);const auto editor=buffer.editor;
    if(editor->selections()>256){*error=rpcError(1008,"Too many selections");return {};}
    QJsonArray list;
    for(sptr_t n=0;n<editor->selections();++n){
        QJsonObject anchor,caret,start,end;
        if(!deltaPosition(editor,editor->selectionNAnchor(n),&anchor)||!deltaPosition(editor,editor->selectionNCaret(n),&caret)||
           !deltaPosition(editor,editor->selectionNStart(n),&start)||!deltaPosition(editor,editor->selectionNEnd(n),&end)){
            *error=rpcError(1009,"Selection cannot be mapped to UTF-16");return {};
        }
        list.append(QJsonObject{{"range",QJsonObject{{"start",start},{"end",end}}},{"anchor",anchor},{"caret",caret},
            {"anchorVirtualSpace",qint64(editor->selectionNAnchorVirtualSpace(n))},{"caretVirtualSpace",qint64(editor->selectionNCaretVirtualSpace(n))}});
    }
    return {{"editorId",editorId},{"documentId",id},{"documentVersion",buffer.version},{"revision",buffer.selectionRevision},
        {"mode",editor->selectionMode()==SC_SEL_STREAM?"stream":editor->selectionMode()==SC_SEL_RECTANGLE?"rectangle":editor->selectionMode()==SC_SEL_THIN?"thin":"lines"},
        {"mainIndex",qint64(editor->mainSelection())},{"selections",list}};
}
QJsonObject DocumentBridge::capture(const QJsonObject &target,QJsonObject *error) {
    Q_ASSERT(QThread::currentThread()==thread());*error={};
    const auto id=documentForEditor(target["editorId"].toString());
    if(id.isEmpty()||id!=target["documentId"].toString()||target["windowId"]!=windowId){*error=rpcError(1002,"DocumentClosed");return {};}
    const auto text=snapshot(id,error);if(!error->isEmpty())return {};
    const auto selection=selections(target["editorId"].toString(),error);if(!error->isEmpty())return {};
    QJsonArray selected;const auto editor=buffers.value(id).editor;qint64 selectedBytes=0;
    for(sptr_t n=0;n<editor->selections();++n){
        const auto begin=editor->selectionNStart(n),end=editor->selectionNEnd(n);selectedBytes+=end-begin;
        if(selectedBytes>MaxSnapshotBytes){*error=rpcError(1008,"Selection capture exceeds its aggregate text budget");return {};}
        selected.append(QString::fromUtf8(reinterpret_cast<const char*>(editor->rangePointer(begin,end-begin)),end-begin));
    }
    auto richText=nativeInfo(id);richText["text"]=text["text"];richText["positionEncoding"]="utf-16";
    const QJsonObject result{{"editor",QJsonObject{{"editorId",target["editorId"]},{"documentId",id},{"windowId",windowId}}},
        {"document",nativeInfo(id)},{"snapshot",richText},{"selection",selection},{"selectedText",selected}};
    // JSON escaping can inflate valid control characters sixfold. Refuse a
    // capture that would overflow the transport rather than fault its session.
    if(QJsonDocument(result).toJson(QJsonDocument::Compact).size()>12*1024*1024){*error=rpcError(1008,"Encoded selection capture is too large");return {};}
    return result;
}
bool DocumentBridge::checkSelection(const QString &id,const QJsonObject &guard,QJsonObject *error) {
    *error={};if(documentForEditor(guard["editorId"].toString())!=id){*error=rpcError(1002,"DocumentClosed");return false;}
    refreshSelection(id);
    if(!nonnegativeInteger(guard["revision"])||buffers.value(id).selectionRevision!=guard["revision"].toInteger()){
        *error=rpcError(1011,"SelectionConflict");return false;
    }
    return true;
}
bool DocumentBridge::checkTarget(const QJsonObject &target,QJsonObject *error) {
    *error={};const auto id=target["documentId"].toString();
    if(id.isEmpty()||documentForEditor(target["editorId"].toString())!=id){*error=rpcError(1002,"DocumentClosed");return false;}
    if(!nonnegativeInteger(target["expectedVersion"])||buffers.value(id).version!=target["expectedVersion"].toInteger()){
        *error=rpcError(1001,"VersionConflict");return false;
    }
    if(target.contains("selectionRevision")&&!checkSelection(id,{{"editorId",target["editorId"]},{"revision",target["selectionRevision"]}},error))return false;
    return true;
}
QJsonObject DocumentBridge::setSelections(const QJsonObject &request,QJsonObject *error) {
    Q_ASSERT(QThread::currentThread()==thread());*error={};
    const auto id=documentForEditor(request["editorId"].toString());
    if(!checkTarget({{"editorId",request["editorId"]},{"documentId",id},{"expectedVersion",request["expectedDocumentVersion"]},{"selectionRevision",request["expectedRevision"]}},error))return {};
    snapshot(id,error);if(!error->isEmpty())return {};
    const auto values=request["selections"].toArray();const auto main=request["mainIndex"].toInteger(-1);
    if(values.isEmpty()||values.size()>256||main<0||main>=values.size()){*error=rpcError(-32602,"Invalid selections");return {};}
    QVector<QPair<sptr_t,sptr_t>> prepared;
    for(const auto &value:values){
        const auto selection=value.toObject();sptr_t anchor,caret;
        if(selection["anchorVirtualSpace"].toInteger()||selection["caretVirtualSpace"].toInteger()){*error=rpcError(-32602,"Setting virtual space is not supported");return {};}
        if(!bytePosition(id,selection["anchor"].toObject(),&anchor,error)||!bytePosition(id,selection["caret"].toObject(),&caret,error))return {};
        sptr_t start,end;if(!bytePosition(id,selection["range"].toObject()["start"].toObject(),&start,error)||!bytePosition(id,selection["range"].toObject()["end"].toObject(),&end,error))return {};
        if(start!=qMin(anchor,caret)||end!=qMax(anchor,caret)){*error=rpcError(-32602,"Selection range disagrees with anchor/caret");return {};}
        for(const auto &previous:prepared){const auto a=qMin(previous.first,previous.second),b=qMax(previous.first,previous.second);if((start<b&&end>a)||start==a){*error=rpcError(-32602,"Overlapping selections are not supported");return {};}}
        prepared.append({anchor,caret});
    }
    const auto editor=buffers.value(id).editor;
    editor->setSelectionMode(SC_SEL_STREAM);editor->clearSelections();
    editor->setSelection(prepared[0].second,prepared[0].first);
    for(qsizetype n=1;n<prepared.size();++n)editor->addSelection(prepared[n].second,prepared[n].first);
    editor->setMainSelection(main);return selections(request["editorId"].toString(),error);
}
QJsonObject DocumentBridge::executeCore(const QString &command,const QJsonObject &request,QJsonObject *error) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(!nativeExecute){*error=rpcError(1005,"UnsupportedCapability");return {};}
    if(!checkTarget(request["target"].toObject(),error))return {};
    const auto id=request["target"].toObject()["documentId"].toString();const auto editor=buffers.value(id).editor;
    if(command.startsWith("edit.")){
        if(editor->readOnly()){*error=rpcError(1003,"ReadOnlyDocument");return {};}
        snapshot(id,error);if(!error->isEmpty())return {};
        const auto selection=selections(buffers.value(id).editorId,error);if(!error->isEmpty())return {};
        if(selection["mode"]!="stream"){*error=rpcError(-32602,"Native edits require stream selections");return {};}
        for(const auto &value:selection["selections"].toArray()){const auto s=value.toObject();if(s["anchorVirtualSpace"].toInteger()||s["caretVirtualSpace"].toInteger()){*error=rpcError(-32602,"Native edits do not support virtual space");return {};}}
    }
    nativeExecute(editor,command,request,error);if(!error->isEmpty())return {};
    return {{"version",buffers.value(id).version}};
}
void DocumentBridge::emitEvent(const QString &type, const QJsonValue &document) {
    if(type!="contentInvalidated"&&type!="reset")emitDelta({{"type",type},{"document",document}});
    emit event(type, document, ++sequence);
}
void DocumentBridge::retainDeltaStream() {Q_ASSERT(QThread::currentThread()==thread());++deltaObservers;}
void DocumentBridge::releaseDeltaStream() {Q_ASSERT(QThread::currentThread()==thread());Q_ASSERT(deltaObservers>0);if(deltaObservers>0)--deltaObservers;if(!deltaObservers)for(auto &buffer:buffers)buffer.pending={};}
void DocumentBridge::emitDelta(const QJsonObject &value) {if(deltaObservers)emit deltaEvent(value,++mirrorSequence);}
bool DocumentBridge::deltaPosition(ScintillaEdit *editor,sptr_t byte,QJsonObject *position) const {
    if(!editor||byte<0||byte>editor->length())return false;
    const auto line=editor->lineFromPosition(byte),start=editor->positionFromLine(line);
    if(byte>editor->lineEndPosition(line))return false;
    const auto units=editor->countCodeUnits(start,byte);
    if(editor->positionRelativeCodeUnits(start,units)!=byte)return false;
    *position={{"line",qint64(line)},{"character",qint64(units)}};return true;
}
void DocumentBridge::detach(const QString &id) {
    if (!buffers.contains(id)) return;
    const auto previous = info(id);
    for (const auto &connection : buffers[id].connections) disconnect(connection);
    buffers.remove(id); // invalidate before delivering any callback
    emitEvent("closed", previous);
    if (activeId == id) { activeId.clear(); emitEvent("activeChanged", QJsonValue(QJsonValue::Null)); }
}
void DocumentBridge::setActive(const QString &id) {
    const auto next = buffers.contains(id) && buffers[id].editor ? id : QString();
    if (activeId == next) return;
    activeId = next; emitEvent("activeChanged", active());
}
void DocumentBridge::updateMetadata(const QString &id, const QUrl &uri, const QString &language) {
    if (!buffers.contains(id)) return;
    auto &buffer = buffers[id];
    if (!uri.isEmpty()) buffer.uri = uri;
    buffer.language = language; emitEvent("metadataChanged", info(id));
}
void DocumentBridge::reset(const QString &id) {
    if (!buffers.contains(id)) return;
    ++buffers[id].version;buffers[id].pending={};emitDelta({{"type","reset"},{"document",info(id)},{"reason","bufferReset"}});emitEvent("reset", info(id));
}
QJsonObject DocumentBridge::snapshot(const QString &id, QJsonObject *error) const {
    Q_ASSERT(QThread::currentThread() == thread()); *error = {};
    const auto buffer = buffers.value(id);
    if (!buffer.editor) { *error = rpcError(1002, "DocumentClosed", QJsonObject{{"documentId", id}}); return {}; }
    const auto size = buffer.editor->length();
    if (size > MaxSnapshotBytes) { *error = rpcError(1008, "ContentTooLarge", QJsonObject{{"limitBytes", MaxSnapshotBytes}}); return {}; }
    const QByteArray bytes(reinterpret_cast<const char *>(buffer.editor->characterPointer()), size);
    QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless); const QString text = decoder.decode(bytes);
    if (decoder.hasError() || !validUnicode(text)) { *error = rpcError(1009, "UnsupportedEncoding"); return {}; }
    auto result = info(id); result["text"] = text; result["positionEncoding"] = "utf-16"; return result;
}
bool DocumentBridge::bytePosition(const QString &id, const QJsonObject &position, sptr_t *result, QJsonObject *error) const {
    *error = {};
    const auto editor = buffers.value(id).editor;
    if (!editor) { *error = rpcError(1002, "DocumentClosed"); return false; }
    if (position.size() != 2 || !nonnegativeInteger(position["line"]) || !nonnegativeInteger(position["character"])) { *error = rpcError(-32602, "Invalid UTF-16 position"); return false; }
    const auto line = position["line"].toInteger(), character = position["character"].toInteger();
    if (line >= editor->lineCount()) { *error = rpcError(-32602, "Position line is out of bounds"); return false; }
    const auto start = editor->positionFromLine(line), end = editor->lineEndPosition(line);
    const auto units = editor->countCodeUnits(start, end);
    if (character > units) { *error = rpcError(-32602, "Position character is out of bounds"); return false; }
    const auto byte = editor->positionRelativeCodeUnits(start, character);
    // countCodeUnits verifies the round trip, rejecting half of a surrogate pair.
    if (byte < start || byte > end || editor->countCodeUnits(start, byte) != character) { *error = rpcError(-32602, "Position splits a Unicode character"); return false; }
    *result = byte; return true;
}
QJsonObject DocumentBridge::applyEdits(const QString &id, qint64 expectedVersion, const QJsonArray &edits, QJsonObject *error) {
    Q_ASSERT(QThread::currentThread() == thread()); *error = {};
    if (!buffers.contains(id) || !buffers[id].editor) { *error = rpcError(1002, "DocumentClosed"); return {}; }
    auto &buffer = buffers[id];
    if (buffer.version != expectedVersion) { *error = rpcError(1001, "VersionConflict", QJsonObject{{"expectedVersion", expectedVersion}, {"actualVersion", buffer.version}}); return {}; }
    if (buffer.editor->readOnly()) { *error = rpcError(1003, "Document is read-only"); return {}; }
    snapshot(id, error); if (!error->isEmpty()) return {};
    if (edits.size() > 256) { *error = rpcError(1008, "Too many text edits"); return {}; }
    struct Edit { sptr_t start, end; QByteArray text;QJsonObject change; };
    QVector<Edit> prepared; qint64 finalBytes = buffer.editor->length();
    for (const auto &value : edits) {
        const auto obj = value.toObject(), range = obj["range"].toObject();
        if (!value.isObject() || obj.size() != 2 || range.size() != 2 || !range["start"].isObject() || !range["end"].isObject() || !obj["text"].isString() || !validUnicode(obj["text"].toString())) { *error = rpcError(-32602, "Invalid text edit"); return {}; }
        Edit edit{};
        if (!bytePosition(id, range["start"].toObject(), &edit.start, error) || !bytePosition(id, range["end"].toObject(), &edit.end, error)) return {};
        if (edit.end < edit.start) { *error = rpcError(-32602, "Reversed text range"); return {}; }
        edit.text = obj["text"].toString().toUtf8(); finalBytes += edit.text.size() - (edit.end - edit.start);if(edit.start==edit.end&&edit.text.isEmpty())continue;prepared.append(edit);
        if(deltaObservers)prepared.last().change={{"range",range},{"rangeOffset",qint64(buffer.editor->countCodeUnits(0,edit.start))},{"rangeLength",qint64(buffer.editor->countCodeUnits(edit.start,edit.end))},{"text",obj["text"]}};
    }
    if (finalBytes > MaxSnapshotBytes) { *error = rpcError(1008, "ContentTooLarge"); return {}; }
    std::sort(prepared.begin(), prepared.end(), [](const Edit &a, const Edit &b) { return a.start < b.start; });
    for (qsizetype i = 1; i < prepared.size(); ++i) if (prepared[i].start < prepared[i-1].end || prepared[i].start == prepared[i-1].start) { *error = rpcError(-32602, "Overlapping text edits"); return {}; }
    buffer.applying = true; buffer.metadataPending=false; auto editor = buffer.editor;
    editor->beginUndoAction();
    for (auto it = prepared.crbegin(); it != prepared.crend(); ++it) {
        editor->setTargetStart(it->start); editor->setTargetEnd(it->end); editor->replaceTarget(it->text.size(), it->text.constData());
    }
    editor->endUndoAction(); buffer.applying = false;
    if (!prepared.isEmpty()) {
        QJsonArray changes;if(deltaObservers)for(auto it=prepared.crbegin();it!=prepared.crend();++it)changes.append(it->change);
        emitDelta({{"type","changed"},{"document",info(id)},{"baseVersion",expectedVersion},{"changes",changes},{"reason","edit"}});
        emitEvent("contentInvalidated", info(id));
    }
    if(buffer.metadataPending){buffer.metadataPending=false;emitEvent("metadataChanged",info(id));}
    return {{"version", buffer.version}};
}
}
