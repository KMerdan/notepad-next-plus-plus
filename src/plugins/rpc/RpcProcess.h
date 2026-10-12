// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RPCPROCESS_H
#define RPCPROCESS_H
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

namespace PluginRpc {
struct WireCounters;
class RpcProcess : public QObject
{
    Q_OBJECT
public:
    enum class FailureKind {None,RuntimeUnavailable,SupervisionFailure,ProtocolFault,ProcessCrash,TransportFailure};
    using Callback = std::function<void(const QJsonValue &, const QJsonObject &)>;
    explicit RpcProcess(const QStringList &hostMethods, QObject *parent = nullptr);
    ~RpcProcess() override;
    void start(const QString &program, const QStringList &arguments, const QString &workingDirectory);
    QString call(const QString &method, const QJsonValue &params, Callback callback, int timeoutMs = 5000);
    void notify(const QString &method, const QJsonValue &params);
    void respond(const QString &token, const QJsonValue &result, const QJsonObject &error = {});
    void cancel(const QString &id);
    void setDeadlinePaused(const QString &id, bool paused);
    void terminate();
    // The worker retains this owner even if its GUI facade is destroyed.
    // Used for package lifetime pins until the process has actually exited.
    void holdLifetime(std::shared_ptr<void> owner) { lifetime=std::move(owner); }
    bool isRunning() const { return worker != nullptr; }
    bool isClosing() const { return closing; }
    qint64 processId() const { return pid; }
    FailureKind failureKind() const {return lastFailure;}
    struct WireStatistics {quint64 queuedBytes=0,receivedBytes=0,sentFrames=0,receivedFrames=0;};
    // Per-start counters, sampled without scheduling work on either event loop.
    // Bytes queued are not a claim that the peer consumed them. Each direction
    // is monotonic; the four relaxed loads are not a transactional snapshot.
    WireStatistics wireStatistics() const;

signals:
    void started();
    void finished(int exitCode, const QString &reason);
    void requestReceived(const QString &method, const QJsonValue &params, const QString &replyToken);
    void requestCancelled(const QString &replyToken);
    void diagnostic(const QString &message);
    void stderrOutput(const QByteArray &bytes);
    void backpressure(bool active);

private:
    QStringList hostMethods;
    QPointer<QObject> worker;
    QHash<QString, Callback> callbacks;
    qint64 pid = 0;
    bool closing = false;
    FailureKind lastFailure=FailureKind::None;
    std::shared_ptr<void> lifetime;
    std::shared_ptr<WireCounters> wire;
};
}
#endif
