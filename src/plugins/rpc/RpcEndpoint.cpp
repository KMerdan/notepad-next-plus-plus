// SPDX-License-Identifier: GPL-3.0-or-later
#include "RpcEndpoint.h"
#include "RpcFramer.h"
#include "JsonIdMask.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QUuid>
#include <cmath>

namespace PluginRpc {
namespace {
constexpr int MaxPending = 64;
constexpr qsizetype MaxBatchResult = RpcFramer::MaxFrameBytes - 64 * 1024;

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// The JSON tree is validated before this scanner runs. Keep the original id
// token: QJson/JS numeric conversion must never alter a caller's numeric ID.
void whitespace(const QByteArray &raw, qsizetype &pos)
{
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\r' || raw[pos] == '\n' || raw[pos] == '\t')) ++pos;
}
qsizetype valueEnd(const QByteArray &raw, qsizetype pos)
{
    whitespace(raw, pos);
    if (raw[pos] == '"') {
        for (++pos; pos < raw.size(); ++pos) {
            if (raw[pos] == '\\') ++pos;
            else if (raw[pos] == '"') return pos + 1;
        }
    } else if (raw[pos] == '{' || raw[pos] == '[') {
        const char closing = raw[pos] == '{' ? '}' : ']';
        for (++pos; pos < raw.size();) {
            whitespace(raw, pos);
            if (raw[pos] == closing) return pos + 1;
            if (raw[pos] == ':' || raw[pos] == ',') ++pos;
            else pos = valueEnd(raw, pos);
        }
    } else {
        while (pos < raw.size() && raw[pos] != ',' && raw[pos] != ']' && raw[pos] != '}'
               && raw[pos] != ' ' && raw[pos] != '\r' && raw[pos] != '\n' && raw[pos] != '\t') ++pos;
        return pos;
    }
    return raw.size();
}
QVector<QByteArray> arrayValues(const QByteArray &raw)
{
    QVector<QByteArray> values;
    qsizetype pos = 0;
    whitespace(raw, pos); ++pos;
    while (pos < raw.size()) {
        whitespace(raw, pos);
        if (raw[pos] == ']') break;
        if (raw[pos] == ',') { ++pos; continue; }
        const auto end = valueEnd(raw, pos);
        values.append(raw.mid(pos, end - pos));
        pos = end;
    }
    return values;
}
QByteArray rawId(const QByteArray &raw)
{
    QByteArray id("null");
    qsizetype pos = 0;
    whitespace(raw, pos); ++pos;
    while (pos < raw.size()) {
        whitespace(raw, pos);
        if (raw[pos] == '}') break;
        if (raw[pos] == ',') { ++pos; continue; }
        const auto keyEnd = valueEnd(raw, pos);
        const auto key = QJsonDocument::fromJson('[' + raw.mid(pos, keyEnd - pos) + ']').array().first().toString();
        pos = keyEnd;
        whitespace(raw, pos); ++pos; whitespace(raw, pos);
        const auto end = valueEnd(raw, pos);
        if (key == QStringLiteral("id")) id = raw.mid(pos, end - pos);
        pos = end;
    }
    return id;
}
bool validId(const QJsonValue &id) { return id.isNull() || id.isString() || id.isDouble(); }
QByteArray response(const QByteArray &id, const QJsonValue &result, const QJsonObject &error)
{
    QJsonObject object{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}};
    if (error.isEmpty()) object.insert(QStringLiteral("result"), result.isUndefined() ? QJsonValue(QJsonValue::Null) : result);
    else object.insert(QStringLiteral("error"), error);
    auto bytes = encode(object);
    bytes.chop(1);
    return bytes + ",\"id\":" + id + '}';
}
}

QJsonObject rpcError(int code, const QString &message, const QJsonValue &data)
{
    QJsonObject error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!data.isUndefined()) error.insert(QStringLiteral("data"), data);
    return error;
}

RpcEndpoint::RpcEndpoint(const QStringList &methods, QObject *parent)
    : QObject(parent), methods(methods.begin(), methods.end())
{
}

void RpcEndpoint::receive(const QByteArray &json)
{
    if (closed) return;
    QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(json);
    Q_UNUSED(decoded);
    QByteArray treeInput;
    QString fault;
    const bool lexed = maskNumericIds(json, &treeInput, &fault);
    if (!fault.isEmpty()) { emit protocolFault(fault); return; }
    QJsonParseError error;
    const auto wrapped = QJsonDocument::fromJson('[' + treeInput + ']', &error);
    if (decoder.hasError() || !lexed || error.error != QJsonParseError::NoError || wrapped.array().size() != 1) {
        emit send(response("null", {}, rpcError(-32700, QStringLiteral("Parse error"))));
        return;
    }
    const auto value = wrapped.array().first();
    if (!value.isArray()) { handle(json, value, {}); return; }
    const auto values = value.toArray();
    if (values.isEmpty()) { emit send(response("null", {}, rpcError(-32600, QStringLiteral("Invalid Request")))); return; }
    // Batch length is a negotiated transport-profile limit. Reject before
    // dispatch: an unbounded array of tiny invalid items could otherwise force
    // an error response much larger than the permitted frame.
    if (values.size() > 64) { emit protocolFault(QStringLiteral("RPC batch exceeds 64 entries.")); return; }
    auto batch = QSharedPointer<Batch>::create();
    const auto raw = arrayValues(json);
    for (qsizetype i = 0; i < values.size(); ++i) handle(raw[i], values[i], batch);
    batch->dispatching = false;
    flush(batch);
}

void RpcEndpoint::handle(const QByteArray &raw, const QJsonValue &value, const QSharedPointer<Batch> &batch)
{
    if (batch) ++batch->waiting;
    const auto object = value.toObject();
    const auto id = object.value(QStringLiteral("id"));
    Incoming call{value.isObject() && object.contains(QStringLiteral("id")) && validId(id) ? rawId(raw) : QByteArray("null"), id, batch};
    const bool looksLikeResponse = value.isObject() && !object.contains(QStringLiteral("method"))
        && (object.contains(QStringLiteral("result")) || object.contains(QStringLiteral("error")));
    if (looksLikeResponse) {
        if (batch) --batch->waiting;
        const auto failure = object.value(QStringLiteral("error")).toObject();
        const auto code = failure.value(QStringLiteral("code"));
        const bool valid = object.value(QStringLiteral("jsonrpc")) == QJsonValue(QStringLiteral("2.0"))
            && object.contains(QStringLiteral("id")) && validId(id)
            && (object.contains(QStringLiteral("result")) != object.contains(QStringLiteral("error")))
            && (!object.contains(QStringLiteral("error")) || (object.value(QStringLiteral("error")).isObject()
                && code.isDouble() && code.toDouble() == std::floor(code.toDouble()) && failure.value(QStringLiteral("message")).isString()));
        if (!valid || !id.isString() || !pending.contains(id.toString())) { emit diagnostic(QStringLiteral("Ignored invalid, unknown or late RPC response.")); return; }
        auto *timer = pending.take(id.toString()); timer->stop(); timer->deleteLater();
        emit responseReceived(id.toString(), object.value(QStringLiteral("result")), failure);
        return;
    }
    const auto params = object.value(QStringLiteral("params"));
    const bool valid = value.isObject() && object.value(QStringLiteral("jsonrpc")) == QJsonValue(QStringLiteral("2.0"))
        && object.value(QStringLiteral("method")).isString()
        && (!object.contains(QStringLiteral("id")) || validId(id))
        && (!object.contains(QStringLiteral("params")) || params.isObject() || params.isArray());
    if (!valid) { reply(call, {}, rpcError(-32600, QStringLiteral("Invalid Request"))); return; }
    const QString method = object.value(QStringLiteral("method")).toString();
    const bool notification = !object.contains(QStringLiteral("id"));
    if (method == QStringLiteral("nnp.request.cancel") && notification) {
        const auto cancelledId = params.toObject().value(QStringLiteral("requestId"));
        const auto tokens = incoming.keys();
        for (const auto &token : tokens)
            if (cancelledId.isString() && incoming.contains(token) && incoming.value(token).parsedId == cancelledId) emit requestCancelled(token);
        if (batch) --batch->waiting;
        return;
    }
    if (!methods.contains(method)) {
        if (notification) { if (batch) --batch->waiting; }
        else reply(call, {}, rpcError(-32601, QStringLiteral("Method not found")));
        return;
    }
    if (incoming.size() >= MaxPending) {
        if (notification) { if (batch) --batch->waiting; emit diagnostic(QStringLiteral("RPC notification rejected: overloaded.")); }
        else reply(call, {}, rpcError(1007, QStringLiteral("Overloaded")));
        return;
    }
    if (notification) {
        if (batch) --batch->waiting;
        emit requestReceived(method, params, QString());
    } else {
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        incoming.insert(token, call);
        emit requestReceived(method, params, token);
    }
}

void RpcEndpoint::reply(const Incoming &call, const QJsonValue &result, const QJsonObject &error)
{
    QByteArray bytes = response(call.id, result, error);
    if (bytes.size() > MaxBatchResult || (call.batch && call.batch->bytes + bytes.size() > MaxBatchResult))
        bytes = response(call.id, {}, rpcError(1008, QStringLiteral("ContentTooLarge")));
    if (!call.batch) { emit send(bytes); return; }
    call.batch->bytes += bytes.size();
    call.batch->replies.append(bytes);
    --call.batch->waiting;
    flush(call.batch);
}

void RpcEndpoint::flush(const QSharedPointer<Batch> &batch)
{
    if (!batch || batch->dispatching || batch->waiting || batch->replies.isEmpty()) return;
    QByteArray combined("[");
    for (const auto &reply : batch->replies) { if (combined.size() > 1) combined += ','; combined += reply; }
    combined += ']';
    batch->replies.clear();
    emit send(combined);
}

void RpcEndpoint::respond(const QString &token, const QJsonValue &result, const QJsonObject &error)
{
    if (!incoming.contains(token) || closed) return;
    reply(incoming.take(token), result, error);
}

void RpcEndpoint::sendRequest(const QString &id, const QString &method, const QJsonValue &params, int timeoutMs)
{
    if (closed || pending.size() >= MaxPending || pending.contains(id)) {
        emit responseReceived(id, {}, rpcError(closed ? 1004 : 1007, QStringLiteral("RPC connection is closed, overloaded or ID is duplicate.")));
        return;
    }
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    pending.insert(id, timer);
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto *expired = pending.take(id);
        if (!expired) return;
        expired->deleteLater();
        emit responseReceived(id, {}, rpcError(1006, QStringLiteral("RPC request timed out.")));
    });
    timer->start(qMax(timeoutMs, 1));
    QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("method"), method}};
    if (!params.isUndefined()) request.insert(QStringLiteral("params"), params);
    emit send(encode(request));
}

void RpcEndpoint::notify(const QString &method, const QJsonValue &params)
{
    if (closed) return;
    QJsonObject notification{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}};
    if (!params.isUndefined()) notification.insert(QStringLiteral("params"), params);
    emit send(encode(notification));
}

void RpcEndpoint::cancelRequest(const QString &id)
{
    if (!pending.contains(id)) return;
    auto *timer = pending.take(id); timer->stop(); timer->deleteLater();
    notify(QStringLiteral("nnp.request.cancel"), QJsonObject{{QStringLiteral("requestId"), id}});
    emit responseReceived(id, {}, rpcError(1006, QStringLiteral("Cancelled")));
}

void RpcEndpoint::setDeadlinePaused(const QString &id, bool paused)
{
    auto *timer = pending.value(id, nullptr);
    if (!timer || timer->property("paused").toBool() == paused) return;
    timer->setProperty("paused", paused);
    if (paused) {
        timer->setProperty("remaining", qMax(timer->remainingTime(), 1));
        timer->stop();
    } else {
        timer->start(qMax(timer->property("remaining").toInt(), 1));
    }
}

void RpcEndpoint::close()
{
    if (closed) return;
    closed = true;
    const auto ids = pending.keys();
    for (const auto &id : ids) {
        auto *timer = pending.take(id); timer->stop(); timer->deleteLater();
        emit responseReceived(id, {}, rpcError(1004, QStringLiteral("Plugin connection closed.")));
    }
    incoming.clear();
}
}
