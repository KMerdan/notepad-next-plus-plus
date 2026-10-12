// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentBridge.h"
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QTest>
#include <QElapsedTimer>
using namespace PluginDocuments;
namespace {
QJsonObject position(int line, int character) { return {{"line",line},{"character",character}}; }
QJsonObject edit(int line, int start, int end, const QString &text) { return {{"range",QJsonObject{{"start",position(line,start)},{"end",position(line,end)}}},{"text",text}}; }
QList<QJsonObject> textEvents(const QSignalSpy &events) {QList<QJsonObject> result;for(const auto &entry:events){const auto event=entry[0].toJsonObject();if(event["type"]!="metadataChanged")result.append(event);}return result;}
}
class PluginDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void nativeCaptureBoundsEscapedResponse() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);const QByteArray text(1100*1024,'\x01');editor.setText(text.constData());editor.selectAll();
        DocumentBridge bridge;bridge.setWindowId("window");bridge.setActive(bridge.attach(&editor,{},"text"));QJsonObject error;
        QVERIFY(!bridge.snapshot(bridge.active().toObject()["documentId"].toString(),&error).isEmpty());QVERIFY(error.isEmpty());
        QVERIFY(bridge.capture(bridge.invocation("test.tools.read"),&error).isEmpty());QCOMPARE(error["code"].toInt(),1008);
        // Refusing an inflated response does not invalidate its buffer/session.
        editor.setText("normal");QVERIFY(!bridge.capture(bridge.invocation("test.tools.read"),&error).isEmpty());QVERIFY(error.isEmpty());
    }
    void nativeCaptureGuardAndUnicodeSelections() {
        ScintillaEdit first,second;first.setCodePage(SC_CP_UTF8);second.setCodePage(SC_CP_UTF8);
        first.setText(QStringLiteral("ab😀中\r\ncd\n").toUtf8().constData());second.setText("other");
        first.setMultipleSelection(true);first.setSelection(9,0);first.addSelection(13,11);first.setMainSelection(1);first.emptyUndoBuffer();
        DocumentBridge bridge;bridge.setWindowId("window");const auto a=bridge.attach(&first,{},"text"),b=bridge.attach(&second,{},"text");bridge.setActive(a);
        const auto invocation=bridge.invocation("test.tools.uppercase");QVERIFY(invocation["editorId"]!=a);
        // Switching tabs does not retarget the original invocation.
        bridge.setActive(b);QJsonObject error;auto captured=bridge.capture(invocation,&error);QVERIFY2(error.isEmpty(),qPrintable(error["message"].toString()));
        QCOMPARE(captured["document"].toObject()["documentId"].toString(),a);QCOMPARE(captured["selectedText"].toArray(),QJsonArray({QStringLiteral("ab😀中"),"cd"}));
        const auto selection=captured["selection"].toObject();QCOMPARE(selection["mainIndex"].toInteger(),1);
        QCOMPARE(selection["selections"].toArray()[0].toObject()["range"].toObject()["end"].toObject(),position(0,5));
        const QJsonObject guard{{"editorId",invocation["editorId"]},{"revision",selection["revision"]}};
        first.setSelection(1,0);QVERIFY(!bridge.checkSelection(a,guard,&error));QCOMPARE(error["code"].toInt(),1011);
        captured=bridge.capture(invocation,&error);QVERIFY(error.isEmpty());const auto current=captured["selection"].toObject();
        QVERIFY(bridge.checkSelection(a,{{"editorId",invocation["editorId"]},{"revision",current["revision"]}},&error));
        const auto version=bridge.info(a)["version"].toInteger();bridge.applyEdits(a,version,{edit(0,0,2,"AB"),edit(1,0,2,"CD")},&error);QVERIFY(error.isEmpty());
        QCOMPARE(bridge.snapshot(a,&error)["text"].toString(),QStringLiteral("AB😀中\r\nCD\n"));first.undo();QCOMPARE(bridge.snapshot(a,&error)["text"].toString(),QStringLiteral("ab😀中\r\ncd\n"));
        QCOMPARE(bridge.snapshot(b,&error)["text"].toString(),"other");
        auto state=bridge.selections(invocation["editorId"].toString(),&error);const auto old=state;
        QJsonObject invalid{{"editorId",invocation["editorId"]},{"expectedDocumentVersion",bridge.info(a)["version"]},{"expectedRevision",state["revision"]},{"mainIndex",0},{"selections",state["selections"]}};
        auto items=invalid["selections"].toArray();auto item=items[0].toObject();item["caret"]=position(0,3);invalid["selections"]=QJsonArray{item};
        bridge.setSelections(invalid,&error);QCOMPARE(error["code"].toInt(),-32602);QCOMPARE(bridge.selections(invocation["editorId"].toString(),&error),old);
        bridge.detach(a);bridge.capture(invocation,&error);QCOMPARE(error["code"].toInt(),1002);
    }
    void deletionDeltaUsesOldUnicodeCoordinates() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText(QStringLiteral("a😀中\r\né\n").toUtf8().constData());DocumentBridge bridge;const auto id=bridge.attach(&editor,{},"text");bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);const auto before=bridge.info(id)["version"].toInteger();
        editor.setTargetStart(1);editor.setTargetEnd(8);editor.replaceTarget(0,"");
        QCOMPARE(events.size(),1);const auto event=events[0][0].toJsonObject();QCOMPARE(event["type"].toString(),"changed");QCOMPARE(event["baseVersion"].toInteger(),before);QCOMPARE(event["document"].toObject()["version"].toInteger(),before+1);const auto change=event["changes"].toArray()[0].toObject();QCOMPARE(change["rangeOffset"].toInteger(),1);QCOMPARE(change["rangeLength"].toInteger(),3);QCOMPARE(change["range"].toObject()["end"].toObject(),position(0,4));QCOMPARE(change["text"].toString(),QString());
        QJsonObject error;QCOMPARE(bridge.snapshot(id,&error)["text"].toString(),QStringLiteral("a\r\né\n"));bridge.releaseDeltaStream();events.clear();editor.addText(1,"x");QVERIFY(events.isEmpty());
    }
    void multilineDeltaAndUndoRedo() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("a\r\nb\n");editor.emptyUndoBuffer();DocumentBridge bridge;const auto id=bridge.attach(&editor,{},"text");bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);
        editor.setTargetStart(1);editor.setTargetEnd(4);editor.replaceTarget(0,"");QCOMPARE(textEvents(events).size(),1);auto change=textEvents(events)[0]["changes"].toArray()[0].toObject();QCOMPARE(change["range"].toObject()["start"].toObject(),position(0,1));QCOMPARE(change["range"].toObject()["end"].toObject(),position(1,1));QCOMPARE(change["rangeLength"].toInteger(),3);
        events.clear();editor.undo();QVERIFY(!textEvents(events).isEmpty());for(const auto &event:textEvents(events))QCOMPARE(event["reason"].toString(),"undo");events.clear();editor.redo();QVERIFY(!textEvents(events).isEmpty());for(const auto &event:textEvents(events))QCOMPARE(event["reason"].toString(),"redo");bridge.releaseDeltaStream();bridge.detach(id);
    }
    void atomicEditDeltaAndExplicitReset() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText(QStringLiteral("a😀中\r\né").toUtf8().constData());editor.emptyUndoBuffer();DocumentBridge bridge;const auto id=bridge.attach(&editor,{},"text");bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);QJsonObject error;const auto before=bridge.info(id)["version"].toInteger();
        const auto result=bridge.applyEdits(id,before,{edit(0,0,1,"B"),edit(1,0,2,"new")},&error);QVERIFY(error.isEmpty());QCOMPARE(textEvents(events).size(),1);const auto event=textEvents(events)[0];QCOMPARE(event["baseVersion"].toInteger(),before);QCOMPARE(event["document"].toObject()["version"].toInteger(),result["version"].toInteger());const auto changes=event["changes"].toArray();QCOMPARE(changes.size(),2);QCOMPARE(changes[0].toObject()["rangeOffset"].toInteger(),6);QCOMPARE(changes[0].toObject()["rangeLength"].toInteger(),2);for(const auto &entry:events)if(entry[0].toJsonObject()["type"]=="metadataChanged")QCOMPARE(entry[0].toJsonObject()["document"].toObject()["version"].toInteger(),result["version"].toInteger());
        events.clear();const auto same=bridge.info(id)["version"].toInteger();bridge.applyEdits(id,same,{edit(0,0,0,"")},&error);QVERIFY(error.isEmpty());QCOMPARE(bridge.info(id)["version"].toInteger(),same);QVERIFY(events.isEmpty());
        {const QSignalBlocker blocked(&editor);editor.setText("reloaded");}bridge.reset(id);QCOMPARE(events.size(),1);QCOMPARE(events[0][0].toJsonObject()["type"].toString(),"reset");QCOMPARE(events[0][0].toJsonObject()["reason"].toString(),"bufferReset");bridge.releaseDeltaStream();
    }
    void unsupportedDeltaIsAnExplicitReset() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText(QStringLiteral("a😀中").toUtf8().constData());DocumentBridge bridge;const auto id=bridge.attach(&editor,{},"text");bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);
        editor.setTargetStart(2);editor.setTargetEnd(3);editor.replaceTarget(0,"");QCOMPARE(events.size(),1);QCOMPARE(events[0][0].toJsonObject()["type"].toString(),"reset");QCOMPARE(events[0][0].toJsonObject()["reason"].toString(),"unmappableChange");QJsonObject error;bridge.snapshot(id,&error);QCOMPARE(error["code"].toInt(),1009);bridge.releaseDeltaStream();
    }
    void oversizedDeltaResetsAndCanRecover() {
        ScintillaEdit editor;editor.setCodePage(SC_CP_UTF8);editor.setText("small");DocumentBridge bridge;const auto id=bridge.attach(&editor,{},"text");bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);
        const QByteArray large(DocumentBridge::MaxSnapshotBytes+1,'x');editor.insertText(0,large.constData());QCOMPARE(textEvents(events).size(),1);QCOMPARE(textEvents(events)[0]["type"].toString(),"reset");QVERIFY(!textEvents(events)[0].contains("changes"));QJsonObject error;bridge.snapshot(id,&error);QCOMPARE(error["code"].toInt(),1008);
        events.clear();editor.setTargetStart(0);editor.setTargetEnd(large.size());editor.replaceTarget(0,"");QCOMPARE(textEvents(events).size(),1);QCOMPARE(textEvents(events)[0]["type"].toString(),"reset");QCOMPARE(bridge.snapshot(id,&error)["text"].toString(),"small");QVERIFY(error.isEmpty());bridge.releaseDeltaStream();
    }
    void orderedDeltaTopologyAndMetadata() {
        DocumentBridge bridge;ScintillaEdit a,b;bridge.retainDeltaStream();QSignalSpy events(&bridge,&DocumentBridge::deltaEvent);const auto first=bridge.attach(&a,{},"text"),second=bridge.attach(&b,{},"text");bridge.setActive(second);bridge.updateMetadata(first,QUrl::fromLocalFile("/tmp/renamed.md"),"markdown");bridge.detach(first);bridge.detach(second);qint64 seq=0;for(const auto &event:events)QCOMPARE(event[1].toLongLong(),++seq);QCOMPARE(seq,bridge.deltaSequence());QCOMPARE(events.last()[0].toJsonObject()["type"].toString(),"activeChanged");QVERIFY(events.last()[0].toJsonObject()["document"].isNull());bridge.releaseDeltaStream();
    }
    void snapshotAndIdentity() {
        DocumentBridge bridge; ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8);
        const auto id = bridge.attach(&editor, {}, "markdown"); bridge.setActive(id);
        editor.setText(QStringLiteral("a😀中\r\né\n\n").toUtf8().constData()); QJsonObject error;
        auto snap = bridge.snapshot(id, &error); QVERIFY(error.isEmpty()); QCOMPARE(snap["text"].toString(), QStringLiteral("a😀中\r\né\n\n"));
        QCOMPARE(snap["positionEncoding"].toString(), "utf-16"); QVERIFY(snap["isDirty"].toBool());
        const auto version = snap["version"].toInteger(); editor.setSavePoint(); QCOMPARE(bridge.info(id)["version"].toInteger(), version);
        bridge.updateMetadata(id, QUrl::fromLocalFile("/tmp/a 中文.md"), "markdown"); QCOMPARE(bridge.idFor(&editor), id);
        QVERIFY(bridge.info(id)["uri"].toString().contains("%E4%B8%AD")); QCOMPARE(bridge.info(id)["version"].toInteger(),version);
        { const QSignalBlocker blocker(&editor); editor.setText("reloaded"); } bridge.reset(id);
        QCOMPARE(bridge.info(id)["version"].toInteger(),version+1); QCOMPARE(bridge.snapshot(id,&error)["text"].toString(), "reloaded");
        bool invalidatedBeforeClose = false;
        connect(&bridge,&DocumentBridge::event,&bridge,[&](const auto &type,const auto &,qint64) { if(type=="closed") { bridge.snapshot(id,&error); invalidatedBeforeClose = error["code"].toInt()==1002; } });
        bridge.detach(id); QVERIFY(invalidatedBeforeClose); QVERIFY(bridge.active().isNull());
        const auto newId = bridge.attach(&editor, {},"markdown"); QVERIFY(newId!=id);
        bridge.detach(newId); // disconnect widget callbacks before captured locals leave scope
    }
    void unicodePositions() {
        ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); DocumentBridge bridge; const auto id=bridge.attach(&editor,{},"markdown");
        editor.setText(QStringLiteral("a😀中\r\né\n\n").toUtf8().constData()); QJsonObject error; sptr_t byte=0;
        QVERIFY(bridge.bytePosition(id,position(0,4),&byte,&error)); QCOMPARE(byte,8);
        QVERIFY(bridge.bytePosition(id,position(0,1),&byte,&error)); QCOMPARE(byte,1);
        QVERIFY(!bridge.bytePosition(id,position(0,2),&byte,&error)); QCOMPARE(error["code"].toInt(),-32602);
        QVERIFY(!bridge.bytePosition(id,position(0,5),&byte,&error));
        QVERIFY(bridge.bytePosition(id,position(1,2),&byte,&error)); QCOMPARE(byte,13);
        QVERIFY(bridge.bytePosition(id,position(3,0),&byte,&error)); QCOMPARE(byte,15);
        QVERIFY(!bridge.bytePosition(id,position(4,0),&byte,&error));
        QVERIFY(!bridge.bytePosition(id,QJsonObject{{"line",0},{"character",0.5}},&byte,&error));
    }
    void invalidEncodingAndBudget() {
        ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); DocumentBridge bridge; const auto id=bridge.attach(&editor,{},"text"); QJsonObject error;
        editor.addText(1,"\xff"); bridge.snapshot(id,&error); QCOMPARE(error["code"].toInt(),1009); QCOMPARE(editor.length(),1);
        editor.setText(""); editor.addText(3,"a\xf0\x9f"); bridge.snapshot(id,&error); QCOMPARE(error["code"].toInt(),1009); QCOMPARE(editor.length(),3);
        editor.setText(QByteArray(DocumentBridge::MaxSnapshotBytes+1,'x').constData()); bridge.snapshot(id,&error); QCOMPARE(error["code"].toInt(),1008);
    }
    void editsAreAtomicAndUndoable() {
        ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); DocumentBridge bridge; const auto id=bridge.attach(&editor,{},"text");
        const auto original=QStringLiteral("a😀中\r\né"); editor.setText(original.toUtf8().constData()); editor.emptyUndoBuffer(); QJsonObject error;
        const auto version=bridge.info(id)["version"].toInteger();
        bridge.applyEdits(id,version-1,{edit(0,0,1,"b")},&error); QCOMPARE(error["code"].toInt(),1001);
        bridge.applyEdits(id,version,{edit(0,0,1,"b"),edit(0,2,3,"x")},&error); QCOMPARE(error["code"].toInt(),-32602); QCOMPARE(bridge.snapshot(id,&error)["text"].toString(),original);
        editor.setReadOnly(true); bridge.applyEdits(id,version,{edit(0,0,1,"b")},&error); QCOMPARE(error["code"].toInt(),1003); editor.setReadOnly(false);
        const auto result=bridge.applyEdits(id,version,{edit(0,0,1,"B"),edit(1,0,2,QStringLiteral("中文"))},&error); QVERIFY(error.isEmpty()); QVERIFY(result["version"].toInteger()>version);
        QCOMPARE(bridge.snapshot(id,&error)["text"].toString(),QStringLiteral("B😀中\r\n中文")); QVERIFY(editor.canUndo()); editor.undo();
        QCOMPARE(bridge.snapshot(id,&error)["text"].toString(),original); QVERIFY(!editor.canUndo());
    }
    void sequenceAndSwitching() {
        DocumentBridge bridge; ScintillaEdit a,b; QSignalSpy events(&bridge,&DocumentBridge::event);
        const auto first=bridge.attach(&a,{},"text"), second=bridge.attach(&b,{},"markdown"); bridge.setActive(first); a.setText("changed"); bridge.setActive(second); bridge.detach(first);
        QCOMPARE(bridge.active().toObject()["documentId"].toString(),second);
        qint64 last=0; for(const auto &event:events) { QVERIFY(event[2].toLongLong()>last); last=event[2].toLongLong(); }
        QCOMPARE(last,bridge.eventSequence());
    }
    void snapshotPerformance_data() { QTest::addColumn<int>("size"); QTest::newRow("10KiB")<<10*1024; QTest::newRow("100KiB")<<100*1024; QTest::newRow("1MiB")<<1024*1024; }
    void snapshotPerformance() {
        QFETCH(int,size); ScintillaEdit editor; editor.setCodePage(SC_CP_UTF8); editor.setText(QByteArray(size,'x').constData()); DocumentBridge bridge; const auto id=bridge.attach(&editor,{},"markdown"); QJsonObject error;
        QElapsedTimer timer; timer.start();
        for(int iteration=0;iteration<100;++iteration) { const auto result=bridge.snapshot(id,&error); QVERIFY(error.isEmpty()); QCOMPARE(result["text"].toString().size(),size); }
        qInfo("Snapshot bytes=%d, average=%.3fms",size,timer.nsecsElapsed()/100.0/1000000.0);
    }
};
QTEST_MAIN(PluginDocumentTests)
#include "PluginDocumentTests.moc"
