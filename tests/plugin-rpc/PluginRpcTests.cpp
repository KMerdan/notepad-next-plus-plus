// SPDX-License-Identifier: GPL-3.0-or-later
#include "RpcEndpoint.h"
#include "RpcFramer.h"
#include "RpcProcess.h"
#include "ProtocolProfile.h"
#include "WireContract.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QElapsedTimer>
#include <QProcess>
#include <algorithm>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <errno.h>
#endif
using namespace PluginRpc;
namespace {
QJsonValue normalized(QJsonValue value) {
    if (value.isObject()) {
        auto obj = value.toObject();
        if (obj.contains("error")) obj["error"] = QJsonObject{{"code", obj["error"].toObject()["code"]}};
        return obj;
    }
    if (value.isArray()) {
        QVector<QJsonValue> items;
        for (const auto &item : value.toArray()) items.append(normalized(item));
        std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
            return QJsonDocument(a.toObject()).toJson(QJsonDocument::Compact) < QJsonDocument(b.toObject()).toJson(QJsonDocument::Compact);
        });
        QJsonArray result; for (const auto &item : items) result.append(item); return result;
    }
    return value;
}
void handlers(RpcEndpoint &endpoint) {
    QObject::connect(&endpoint, &RpcEndpoint::requestReceived, &endpoint, [&endpoint](const QString &method, const QJsonValue &params, const QString &token) {
        if (method == "echo") endpoint.respond(token, params);
        else if (method == "fail") endpoint.respond(token, {}, rpcError(-32603, "Internal error"));
        else if (method == "sum") {
            if (!params.isArray()) { endpoint.respond(token, {}, rpcError(-32602, "Invalid params")); return; }
            double sum = 0; for (const auto &number : params.toArray()) sum += number.toDouble(); endpoint.respond(token, sum);
        }
    });
}
}
class PluginRpcTests : public QObject {
    Q_OBJECT
private slots:
    void wireFixtures_data() {
        QTest::addColumn<QJsonObject>("fixture");QFile file(QStringLiteral(RPC_FIXTURES_DIR "/wire-fixtures.json"));QVERIFY(file.open(QIODevice::ReadOnly));
        for(const auto &value:QJsonDocument::fromJson(file.readAll()).array())QTest::newRow(qPrintable(value.toObject()["name"].toString()))<<value.toObject();
    }
    void wireFixtures() {
        QFETCH(QJsonObject,fixture);QString error;
        QCOMPARE(WireContract::validate(fixture["method"].toString(),fixture["part"].toString(),fixture["value"],&error),fixture["valid"].toBool());
        QCOMPARE(error.isEmpty(),fixture["valid"].toBool());
    }
    void wireMethodCoverage() {
        QCOMPARE(WireContract::methods().size(),33);QFile file(QStringLiteral(RPC_FIXTURES_DIR "/wire-fixtures.json"));QVERIFY(file.open(QIODevice::ReadOnly));QSet<QString> covered;
        for(const auto &value:QJsonDocument::fromJson(file.readAll()).array())if(value.toObject()["valid"].toBool())covered.insert(value.toObject()["method"].toString());
        const auto methodList=WireContract::methods();QCOMPARE(covered,QSet<QString>(methodList.begin(),methodList.end()));
        QVERIFY(!WireContract::validate("nnp.future.method","params",QJsonObject{}));
    }
    void profileAgreement() {
        QFile file(QStringLiteral(RPC_FIXTURES_DIR "/../../packages/plugin-host/protocol-profile.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object(),profileLimits());
    }
    void conformance_data() {
        QTest::addColumn<QJsonObject>("fixture");
        QFile file(QStringLiteral(RPC_FIXTURES_DIR "/fixtures.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        for (const auto &item : QJsonDocument::fromJson(file.readAll()).array()) {
            const auto obj = item.toObject(); QTest::newRow(qPrintable(obj["name"].toString())) << obj;
        }
    }
    void conformance() {
        QFETCH(QJsonObject, fixture);
        RpcEndpoint endpoint({"echo", "sum", "fail"}); handlers(endpoint);
        QSignalSpy replies(&endpoint, &RpcEndpoint::send);
        endpoint.receive(fixture["input"].toString().toUtf8());
        if (fixture.contains("idRaw")) {
            QCOMPARE(replies.size(), 1);
            QVERIFY(replies.first().first().toByteArray().contains("\"id\":" + fixture["idRaw"].toString().toUtf8() + '}'));
        } else if (fixture["expected"].isNull()) QCOMPARE(replies.size(), 0);
        else {
            QCOMPARE(replies.size(), 1);
            const auto doc = QJsonDocument::fromJson(replies.first().first().toByteArray());
            const QJsonValue value = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
            QCOMPARE(normalized(value), normalized(fixture["expected"]));
        }
    }
    void framing() {
        const QByteArray body = QStringLiteral("{\"text\":\"a😀中\"}").toUtf8();
        const auto wire = RpcFramer::frame(body) + RpcFramer::frame("{}");
        for (qsizetype split = 0; split <= wire.size(); ++split) {
            RpcFramer framer; QVector<QByteArray> frames; QString error;
            const auto consume = [&frames](const QByteArray &json) { frames.append(json); };
            QVERIFY(framer.feed(wire.left(split), consume, &error)); QVERIFY(framer.feed(wire.mid(split), consume, &error));
            QVERIFY(framer.finish(&error)); QCOMPARE(frames, QVector<QByteArray>({body, "{}"}));
        }
        RpcFramer bytes; QVector<QByteArray> result; QString error;
        for (const char byte : wire) QVERIFY(bytes.feed(QByteArray(1, byte), [&result](auto json) { result.append(json); }, &error));
        QCOMPARE(result.size(), 2);
    }
    void invalidFraming_data() {
        QTest::addColumn<QByteArray>("wire");
        QTest::newRow("duplicate") << QByteArray("Content-Length: 1\r\nContent-Length: 1\r\n\r\nx");
        QTest::newRow("negative") << QByteArray("Content-Length: -1\r\n\r\n");
        QTest::newRow("oversize") << QByteArray("Content-Length: 16777217\r\n\r\n");
        QTest::newRow("overflow") << QByteArray("Content-Length: 999999999999999999999999999\r\n\r\n");
        QTest::newRow("missing") << QByteArray("X-Test: value\r\n\r\n");
        QTest::newRow("bare LF") << QByteArray("X-Test: value\nContent-Length: 0\r\n\r\n");
        QTest::newRow("charset") << QByteArray("Content-Length: 0\r\nContent-Type: application/json; charset=latin1\r\n\r\n");
        QTest::newRow("malformed charset") << QByteArray("Content-Length: 0\r\nContent-Type: application/json; charset=\r\n\r\n");
        QTest::newRow("header budget") << QByteArray(8193, 'x');
        QTest::newRow("non ASCII") << QByteArray("Content-Length: 0\r\nX-Test: \xff\r\n\r\n");
    }
    void invalidFraming() {
        QFETCH(QByteArray, wire); RpcFramer framer; QString error;
        QVERIFY(!framer.feed(wire, [](auto) {}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!framer.feed(RpcFramer::frame("{}"), [](auto) {}, &error));
    }
    void truncatedFrame() {
        RpcFramer framer; QString error; QVERIFY(framer.feed("Content-Length: 10\r\n\r\n{}", [](auto) {}, &error));
        QVERIFY(!framer.finish(&error));
    }
    void invalidUtf8() {
        RpcEndpoint endpoint({}); QSignalSpy replies(&endpoint, &RpcEndpoint::send);
        endpoint.receive(QByteArray("{\"x\":\"") + QByteArray(1, '\xff') + "\"}");
        QCOMPARE(replies.size(), 1);
        QCOMPARE(QJsonDocument::fromJson(replies[0][0].toByteArray()).object()["error"].toObject()["code"].toInt(), -32700);
    }
    void budgets() {
        RpcEndpoint endpoint({"echo"}); handlers(endpoint); QSignalSpy faults(&endpoint, &RpcEndpoint::protocolFault);
        QSignalSpy dispatch(&endpoint, &RpcEndpoint::requestReceived);
        endpoint.receive('[' + QByteArray("1,").repeated(64) + "1]"); QCOMPARE(faults.size(), 1); QCOMPARE(dispatch.size(), 0);
        endpoint.receive("{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"id\":\"" + QByteArray(1024, 'x') + "\"}"); QCOMPARE(faults.size(), 2);
        endpoint.receive(QByteArray(129, '[') + '0' + QByteArray(129, ']')); QCOMPARE(faults.size(), 3);
    }
    void responseTimeoutCancelAndClose() {
        RpcEndpoint endpoint({}); QSignalSpy result(&endpoint, &RpcEndpoint::responseReceived); QSignalSpy sends(&endpoint, &RpcEndpoint::send);
        endpoint.sendRequest("ok", "echo", QJsonObject{}, 1000);
        endpoint.receive("{\"jsonrpc\":\"2.0\",\"id\":\"ok\",\"result\":1,\"error\":{\"code\":1,\"message\":\"x\"}}");
        QCOMPARE(result.size(), 0); QCOMPARE(endpoint.pendingCount(), 1);
        endpoint.receive("{\"jsonrpc\":\"2.0\",\"id\":\"ok\",\"result\":3}");
        QCOMPARE(result.size(), 1); QCOMPARE(result[0][1].toJsonValue().toInt(), 3); QCOMPARE(endpoint.pendingCount(), 0);
        endpoint.sendRequest("timeout", "hang", QJsonObject{}, 10); QTRY_COMPARE(result.size(), 2);
        QCOMPARE(result[1][2].toJsonObject()["code"].toInt(), 1006);
        endpoint.sendRequest("cancel", "hang", QJsonObject{}); endpoint.cancelRequest("cancel"); QCOMPARE(result.size(), 3);
        QVERIFY(sends.last()[0].toByteArray().contains("nnp.request.cancel"));
        endpoint.sendRequest("close", "hang", QJsonObject{}); endpoint.close(); QCOMPARE(result.size(), 4); QCOMPARE(endpoint.pendingCount(), 0);
    }
    void pausedDeadlineRetainsBudgetAndCanCancelOrClose() {
        RpcEndpoint endpoint({}); QSignalSpy result(&endpoint,&RpcEndpoint::responseReceived);
        endpoint.sendRequest("ui","command",QJsonObject{},100);
        endpoint.setDeadlinePaused("ui",true);endpoint.setDeadlinePaused("ui",true);
        endpoint.sendRequest("health","ping",QJsonObject{},10);
        QTest::qWait(160);QCOMPARE(result.size(),1);QCOMPARE(result[0][0].toString(),QString("health"));QCOMPARE(endpoint.pendingCount(),1);
        endpoint.setDeadlinePaused("ui",false);endpoint.setDeadlinePaused("ui",false);
        QTRY_COMPARE(result.size(),2);QCOMPARE(result[1][0].toString(),QString("ui"));QCOMPARE(result[1][2].toJsonObject()["code"].toInt(),1006);
        endpoint.sendRequest("cancel","command",QJsonObject{},100);endpoint.setDeadlinePaused("cancel",true);endpoint.cancelRequest("cancel");QCOMPARE(result.size(),3);
        endpoint.sendRequest("close","command",QJsonObject{},100);endpoint.setDeadlinePaused("close",true);endpoint.close();QCOMPARE(result.size(),4);QCOMPARE(endpoint.pendingCount(),0);
        endpoint.setDeadlinePaused("late",false);
    }
    void asyncBatchAndIncomingCancel() {
        RpcEndpoint endpoint({"later"}); QSignalSpy incoming(&endpoint, &RpcEndpoint::requestReceived), replies(&endpoint, &RpcEndpoint::send), cancel(&endpoint, &RpcEndpoint::requestCancelled);
        endpoint.receive("[{\"jsonrpc\":\"2.0\",\"method\":\"later\",\"id\":\"a\"},{\"jsonrpc\":\"2.0\",\"method\":\"later\",\"id\":\"b\"}]");
        QCOMPARE(incoming.size(), 2); QCOMPARE(replies.size(), 0);
        endpoint.receive("{\"jsonrpc\":\"2.0\",\"method\":\"nnp.request.cancel\",\"params\":{\"requestId\":\"a\"}}"); QCOMPARE(cancel.size(), 1);
        endpoint.respond(incoming[1][2].toString(), 2); QCOMPARE(replies.size(), 0);
        endpoint.respond(incoming[0][2].toString(), 1); QCOMPARE(replies.size(), 1); QCOMPARE(endpoint.incomingCount(), 0);
        QVERIFY(QJsonDocument::fromJson(replies[0][0].toByteArray()).isArray());
    }
    void realNodeProcess() {
        QVERIFY2(QFile::exists(QStringLiteral(RPC_TEST_NODE)), "Set NOTEPADNEXT_TEST_NODE to an absolute Node 24 executable.");
        RpcProcess process({"host.echo"}); QSignalSpy started(&process, &RpcProcess::started), finished(&process, &RpcProcess::finished);
        connect(&process, &RpcProcess::requestReceived, &process, [&process](const QString &, const QJsonValue &params, const QString &token) { process.respond(token, params); });
        process.start(QStringLiteral(RPC_TEST_NODE), {QStringLiteral(RPC_FIXTURES_DIR "/peer.cjs")}, QStringLiteral(RPC_FIXTURES_DIR)); QTRY_COMPARE(started.size(), 1);
        int calls = 0; QJsonObject failure;
        process.call("peer.roundtrip", QJsonObject{{"text", QStringLiteral("a😀中")}}, [&calls, &failure](const auto &result, const auto &error) {
            ++calls; failure = error; QCOMPARE(result.toObject()["text"].toString(), QStringLiteral("a😀中"));
        }); QTRY_COMPARE(calls, 1); QVERIFY(failure.isEmpty());
        process.call("peer.hang", QJsonObject{}, [&calls](const auto &, const auto &error) { ++calls; QCOMPARE(error["code"].toInt(), 1006); }, 30); QTRY_COMPARE(calls, 2);
        qint64 child = 0;
        process.call("peer.child", QJsonObject{}, [&child](const auto &result, const auto &error) { QVERIFY(error.isEmpty()); child = qint64(result.toDouble()); }); QTRY_VERIFY(child > 0);
        process.terminate(); QTRY_COMPARE(finished.size(), 1); QVERIFY(!process.isRunning());
#ifdef Q_OS_UNIX
        QTRY_VERIFY_WITH_TIMEOUT(::kill(pid_t(child), 0) == -1 && errno == ESRCH, 5000);
#endif
    }
    void wireAccountingRoundtripAndRestart() {
        RpcProcess process({"host.echo"});QSignalSpy started(&process,&RpcProcess::started),finished(&process,&RpcProcess::finished);
        connect(&process,&RpcProcess::requestReceived,&process,[&process](const QString &,const QJsonValue &params,const QString &token){process.respond(token,params);});
        QCOMPARE(process.wireStatistics().queuedBytes,quint64(0));
        for(int run=0;run<2;++run) {
            process.start(QStringLiteral(RPC_TEST_NODE),{QStringLiteral(RPC_FIXTURES_DIR "/peer.cjs")},QStringLiteral(RPC_FIXTURES_DIR));QTRY_COMPARE(started.size(),run+1);
            QCOMPARE(process.wireStatistics().sentFrames,quint64(0));QCOMPARE(process.wireStatistics().receivedFrames,quint64(0));
            auto complete=std::make_shared<bool>(false);auto result=std::make_shared<QJsonValue>();auto error=std::make_shared<QJsonObject>();
            process.call("peer.roundtrip",QJsonObject{{"text",QStringLiteral("metrics 😀中")}},[complete,result,error](const auto &value,const auto &failure){*result=value;*error=failure;*complete=true;});QTRY_VERIFY(*complete);QVERIFY(error->isEmpty());QCOMPARE(result->toObject()["text"].toString(),QStringLiteral("metrics 😀中"));
            const auto stats=process.wireStatistics();QCOMPARE(stats.sentFrames,quint64(2));QCOMPARE(stats.receivedFrames,quint64(2));QVERIFY(stats.queuedBytes>100);QVERIFY(stats.receivedBytes>100);
            process.terminate();QTRY_COMPARE(finished.size(),run+1);QVERIFY(!process.isRunning());
            const auto stopped=process.wireStatistics();QCOMPARE(stopped.queuedBytes,stats.queuedBytes);QCOMPARE(stopped.receivedBytes,stats.receivedBytes);
        }
    }
    void repeatedProcesses() {
        for (int cycle = 0; cycle < 10; ++cycle) {
            RpcProcess process({}); QSignalSpy started(&process, &RpcProcess::started), finished(&process, &RpcProcess::finished);
            process.start(QStringLiteral(RPC_TEST_NODE), {QStringLiteral(RPC_FIXTURES_DIR "/peer.cjs")}, QStringLiteral(RPC_FIXTURES_DIR));
            QTRY_COMPARE(started.size(), 1); process.terminate(); QTRY_COMPARE(finished.size(), 1);
        }
    }
    void processBenchmarks_data() { QTest::addColumn<int>("count"); QTest::newRow("1 process")<<1; QTest::newRow("5 processes")<<5; QTest::newRow("10 processes")<<10; }
    void processBenchmarks() {
        QFETCH(int,count); QList<RpcProcess*> processes; QStringList pids;
        for(int index=0;index<count;++index) {
            auto *process=new RpcProcess({"host.echo"},this); processes.append(process); QSignalSpy started(process,&RpcProcess::started);
            connect(process,&RpcProcess::requestReceived,process,[process](const QString &,const QJsonValue &params,const QString &token) { process->respond(token,params); });
            process->start(QStringLiteral(RPC_TEST_NODE),{QStringLiteral(RPC_FIXTURES_DIR "/peer.cjs")},QStringLiteral(RPC_FIXTURES_DIR)); QTRY_COMPARE(started.size(),1); pids.append(QString::number(process->processId()));
        }
        QJsonArray measurements;
        for(int size:{10*1024,100*1024,1024*1024}) {
            const QJsonObject params{{"text",QString(size,'x')}}; QVector<double> times;
            for(int sample=0;sample<10;++sample) for(auto *process:processes) {
                bool completed=false; QElapsedTimer timer; timer.start();
                process->call("peer.roundtrip",params,[&](const QJsonValue &result,const QJsonObject &error) { QVERIFY(error.isEmpty()); QCOMPARE(result.toObject()["text"].toString().size(),size); times.append(timer.nsecsElapsed()/1000000.0); completed=true; });
                // The normal QTRY 50ms step obscures IPC latency. Pump the GUI
                // event loop at 1ms intervals only in this benchmark runner.
                while(!completed && timer.elapsed()<5000) QTest::qWait(1); QVERIFY(completed);
            }
            std::sort(times.begin(),times.end()); double total=0;for(auto value:times)total+=value;
            measurements.append(QJsonObject{{"bytes",size},{"samples",times.size()},{"meanMs",total/times.size()},{"p95Ms",times[qMin<qsizetype>(times.size()-1,qsizetype(times.size()*0.95))]}});
        }
        QJsonObject record{{"processes",count},{"roundtrips",measurements}};
#ifdef Q_OS_UNIX
        QProcess stats; stats.start("/bin/ps",{"-o","pid=,rss=,pcpu=","-p",pids.join(',')});
        if(stats.waitForFinished(5000) && stats.exitCode()==0) record["ownedProcessStatsKiB"]=QString::fromUtf8(stats.readAllStandardOutput()).trimmed();
#endif
        qInfo().noquote()<<"PLUGIN_BENCHMARK "<<QJsonDocument(record).toJson(QJsonDocument::Compact);
        for(auto *process:processes) { QSignalSpy finished(process,&RpcProcess::finished); process->terminate(); QTRY_COMPARE(finished.size(),1); delete process; }
    }
};
QTEST_GUILESS_MAIN(PluginRpcTests)
#include "PluginRpcTests.moc"
