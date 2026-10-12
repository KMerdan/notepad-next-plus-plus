// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RPCENDPOINT_H
#define RPCENDPOINT_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSharedPointer>
#include <QSet>
#include <QTimer>
#include <QVector>

namespace PluginRpc {
QJsonObject rpcError(int code, const QString &message, const QJsonValue &data = QJsonValue());

// All endpoint methods execute on its owning thread. The process worker owns
// it; GUI services receive opaque reply tokens over queued connections.
class RpcEndpoint : public QObject
{
    Q_OBJECT
public:
    explicit RpcEndpoint(const QStringList &methods, QObject *parent = nullptr);
    int pendingCount() const { return pending.size(); }
    int incomingCount() const { return incoming.size(); }

public slots:
    void receive(const QByteArray &json);
    void sendRequest(const QString &id, const QString &method, const QJsonValue &params, int timeoutMs = 5000);
    void notify(const QString &method, const QJsonValue &params);
    void respond(const QString &token, const QJsonValue &result, const QJsonObject &error = QJsonObject());
    void cancelRequest(const QString &id);
    void setDeadlinePaused(const QString &id, bool paused);
    void close();

signals:
    void send(const QByteArray &json);
    void requestReceived(const QString &method, const QJsonValue &params, const QString &replyToken);
    void requestCancelled(const QString &replyToken);
    void responseReceived(const QString &id, const QJsonValue &result, const QJsonObject &error);
    void diagnostic(const QString &message);
    void protocolFault(const QString &message);

private:
    struct Batch { int waiting = 0; bool dispatching = true; QVector<QByteArray> replies; qsizetype bytes = 0; };
    struct Incoming { QByteArray id; QJsonValue parsedId; QSharedPointer<Batch> batch; };
    void handle(const QByteArray &raw, const QJsonValue &value, const QSharedPointer<Batch> &batch);
    void reply(const Incoming &call, const QJsonValue &result, const QJsonObject &error);
    void flush(const QSharedPointer<Batch> &batch);
    QSet<QString> methods;
    QHash<QString, QTimer *> pending;
    QHash<QString, Incoming> incoming;
    bool closed = false;
};
}
#endif
