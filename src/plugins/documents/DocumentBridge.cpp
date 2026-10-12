// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentBridge.h"
#include "RpcEndpoint.h"
#include <QFontDatabase>
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
