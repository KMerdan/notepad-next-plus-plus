// SPDX-License-Identifier: GPL-3.0-or-later
#include "RpcProcess.h"
#include "RpcEndpoint.h"
#include "RpcFramer.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QUuid>
#include <atomic>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace PluginRpc {
struct WireCounters {
    std::atomic<quint64> queuedBytes{0},receivedBytes{0},sentFrames{0},receivedFrames{0};
};
namespace {
constexpr qint64 HighWater = 8 * 1024 * 1024;
constexpr qint64 HardLimit = 32 * 1024 * 1024;
using FailureKind=RpcProcess::FailureKind;
}

class RpcWorker : public QObject
{
    Q_OBJECT
public:
    RpcWorker(const QStringList &methods, const QString &program, const QStringList &arguments, const QString &directory,std::shared_ptr<void> lifetime,std::shared_ptr<WireCounters> wire)
        : methods(methods), program(program), arguments(arguments), directory(directory),lifetime(std::move(lifetime)),wire(std::move(wire)) {}
    ~RpcWorker() override {closeParentWatch();}
    void initialize() {
        logWindow.start();
        process = new QProcess(this);
        endpoint = new RpcEndpoint(methods, this);
        process->setProgram(program);
        process->setArguments(arguments);
        process->setWorkingDirectory(directory);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const QString &key : {QStringLiteral("NODE_OPTIONS"), QStringLiteral("NODE_PATH"), QStringLiteral("LD_PRELOAD"),
            QStringLiteral("DYLD_INSERT_LIBRARIES"), QStringLiteral("NODE_EXTRA_CA_CERTS"),QStringLiteral("NNP_PARENT_WATCH_FD"),QStringLiteral("NNP_PARENT_WATCH_GROUP")}) environment.remove(key);
#ifdef Q_OS_UNIX
        int descriptors[2];
        if(::pipe(descriptors)<0){failure=FailureKind::SupervisionFailure;end(-1,QStringLiteral("Cannot create the plugin parent-lifetime pipe."));return;}
        watchRead=descriptors[0];watchWrite=descriptors[1];
        if(::fcntl(watchRead,F_SETFD,FD_CLOEXEC)<0||::fcntl(watchWrite,F_SETFD,FD_CLOEXEC)<0){failure=FailureKind::SupervisionFailure;end(-1,QStringLiteral("Cannot protect plugin lifetime descriptors."));return;}
        // Only the application retains the write end. EOF is an OS lifetime
        // fact even if its PID is reused or the plugin's JS loop is blocked.
        const auto read=watchRead,write=watchWrite;
        process->setChildProcessModifier([read,write] {
            if(::setsid()<0||::dup2(read,3)<0||::fcntl(3,F_SETFD,0)<0)::_exit(127);
            if(read!=3)::close(read);if(write!=3)::close(write);
        });
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
        QProcess::UnixProcessParameters parameters;parameters.flags=QProcess::UnixProcessFlag::CloseFileDescriptors;parameters.lowestFileDescriptorToClose=4;
        process->setUnixProcessParameters(parameters);
#endif
        environment.insert("NNP_PARENT_WATCH_FD","3");environment.insert("NNP_PARENT_WATCH_GROUP","1");
#endif
        process->setProcessEnvironment(environment);
#ifdef Q_OS_WIN
        job = CreateJobObjectW(nullptr, nullptr);
        if (!job) { failure=FailureKind::SupervisionFailure;end(-1, QStringLiteral("Cannot create plugin process job.")); return; }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { failure=FailureKind::SupervisionFailure;end(-1, QStringLiteral("Cannot configure plugin process job.")); return; }
#endif
        connect(endpoint, &RpcEndpoint::send, this, &RpcWorker::send);
        connect(endpoint, &RpcEndpoint::requestReceived, this, &RpcWorker::requestReceived);
        connect(endpoint, &RpcEndpoint::requestCancelled, this, &RpcWorker::requestCancelled);
        connect(endpoint, &RpcEndpoint::responseReceived, this, &RpcWorker::responseReceived);
        connect(endpoint, &RpcEndpoint::diagnostic, this, [this](const QString &message) { if(logAllowance(message.toUtf8().size())) emit diagnostic(message); });
        connect(endpoint, &RpcEndpoint::protocolFault, this, [this](const QString &message) { stop(message,FailureKind::ProtocolFault); });
        connect(process, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
            HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()));
            const bool assigned = child && AssignProcessToJobObject(job, child);
            if (child) CloseHandle(child);
            if (!assigned) { stop(QStringLiteral("Cannot supervise plugin process tree."),FailureKind::SupervisionFailure); return; }
#endif
            pid = process->processId();
#ifdef Q_OS_UNIX
            if(watchRead>=0){::close(watchRead);watchRead=-1;}
#endif
            if (stopping) { killTree(true); process->kill(); return; }
            emit started(pid);
        });
        connect(process, &QProcess::readyReadStandardOutput, this, [this] {
            process->setReadChannel(QProcess::StandardOutput);
            while (process->bytesAvailable()) {
                const auto bytes=process->read(64*1024);
                wire->receivedBytes.fetch_add(quint64(bytes.size()),std::memory_order_relaxed);
                if(stopping) continue;
                QString error;
                if (!framer.feed(bytes, [this](const QByteArray &json) { wire->receivedFrames.fetch_add(1,std::memory_order_relaxed);endpoint->receive(json); }, &error)) { stop(error,FailureKind::ProtocolFault); return; }
            }
        });
        connect(process, &QProcess::readyReadStandardError, this, [this] {
            process->setReadChannel(QProcess::StandardError);
            // Drain all stderr, but bound delivery to the UI/log writer.
            QByteArray retained;
            while (process->bytesAvailable()) {
                const auto bytes = process->read(64 * 1024);
                const auto part=bytes.left(qMin<qsizetype>(16*1024-retained.size(),logRemaining()));
                retained+=part; logDelivered+=part.size();
            }
            process->setReadChannel(QProcess::StandardOutput);
            if (!retained.isEmpty()) emit stderrOutput(retained);
        });
        connect(process, &QProcess::bytesWritten, this, [this] { pressure(); });
        connect(process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            QString truncated;
            if (!stopping && !framer.finish(&truncated)) {reason = truncated;failure=FailureKind::ProtocolFault;}
            if (!stopping&&reason.isEmpty() && status == QProcess::CrashExit) {reason = QStringLiteral("Plugin process crashed.");failure=FailureKind::ProcessCrash;}
            end(code, reason);
        });
        connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {failure=FailureKind::RuntimeUnavailable;end(-1, process->errorString());}
        });
        process->start();
    }
    void call(const QString &id, const QString &method, const QJsonValue &params, int timeout) {
        if (!endpoint || stopping || process->state() != QProcess::Running) { emit responseReceived(id, {}, rpcError(1004, QStringLiteral("Plugin is stopping or unavailable."))); return; }
        if (process->bytesToWrite() >= HighWater && method != QStringLiteral("nnp.shutdown") && method != QStringLiteral("nnp.plugin.deactivate")) {
            emit responseReceived(id, {}, rpcError(1007, QStringLiteral("RPC send queue is overloaded."))); return;
        }
        endpoint->sendRequest(id, method, params, timeout);
    }
    void notify(const QString &method, const QJsonValue &params) { if (endpoint && !stopping) endpoint->notify(method, params); }
    void respond(const QString &token, const QJsonValue &result, const QJsonObject &error) { if (endpoint && !stopping) endpoint->respond(token, result, error); }
    void cancel(const QString &id) { if (endpoint) endpoint->cancelRequest(id); }
    void setDeadlinePaused(const QString &id, bool paused) { if (endpoint) endpoint->setDeadlinePaused(id, paused); }
    void stop(const QString &message = QString(),FailureKind kind=FailureKind::TransportFailure) {
        if (stopping) return;
        stopping = true; reason = message;if(!message.isEmpty())failure=kind;
        if(failure!=FailureKind::None)emit failureClassified(int(failure));
        if (endpoint) endpoint->close();
        if (!process || process->state() == QProcess::NotRunning) { end(-1, reason); return; }
        if(!message.isEmpty()) { killTree(true); process->kill(); return; }
        killTree(false);
        process->terminate();
        QTimer::singleShot(500, this, [this] { if (!ended) { killTree(true); process->kill(); } });
    }
signals:
    void started(qint64 pid);
    void exited(int exitCode, const QString &reason,int failureKind);
    void failureClassified(int failureKind);
    void responseReceived(const QString &id, const QJsonValue &result, const QJsonObject &error);
    void requestReceived(const QString &method, const QJsonValue &params, const QString &token);
    void requestCancelled(const QString &token);
    void diagnostic(const QString &message);
    void stderrOutput(const QByteArray &bytes);
    void backpressure(bool active);
private:
    void closeParentWatch() {
#ifdef Q_OS_UNIX
        if(watchRead>=0){::close(watchRead);watchRead=-1;}
        if(watchWrite>=0){::close(watchWrite);watchWrite=-1;}
#endif
    }
    qsizetype logRemaining() {
        if(logWindow.elapsed()>=1000) { logWindow.restart(); logDelivered=0; }
        return qMax<qsizetype>(0,16*1024-logDelivered);
    }
    bool logAllowance(qsizetype bytes) { if(bytes>logRemaining()) return false; logDelivered+=bytes; return true; }
    void send(const QByteArray &json) {
        if (stopping || !process || process->state() != QProcess::Running) return;
        const auto bytes = RpcFramer::frame(json);
        if (json.size() > RpcFramer::MaxFrameBytes || process->bytesToWrite() + bytes.size() > HardLimit) { stop(QStringLiteral("RPC send queue or frame exceeds its hard limit.")); return; }
        const auto queued=process->write(bytes);
        if(queued>0)wire->queuedBytes.fetch_add(quint64(queued),std::memory_order_relaxed);
        if (queued != bytes.size()) { stop(QStringLiteral("Cannot write RPC frame.")); return; }
        wire->sentFrames.fetch_add(1,std::memory_order_relaxed);
        pressure();
    }
    void pressure() {
        const bool active = process && process->bytesToWrite() >= HighWater;
        if (active != pressured) { pressured = active; emit backpressure(active); }
    }
    void killTree(bool force) {
#ifdef Q_OS_UNIX
        if (pid > 0) ::kill(-pid, force ? SIGKILL : SIGTERM);
#endif
#ifdef Q_OS_WIN
        if (job && force) TerminateJobObject(job, 1);
#endif
        Q_UNUSED(force);
    }
    void end(int code, const QString &message) {
        if (ended) return; ended = true;
        if(failure!=FailureKind::None)emit failureClassified(int(failure));
        if (endpoint) endpoint->close();
        killTree(true);
        closeParentWatch();
#ifdef Q_OS_WIN
        if (job) { CloseHandle(job); job = nullptr; }
#endif
        emit exited(code, message,int(failure));
        QThread::currentThread()->quit();
    }
    QStringList methods;
    QString program;
    QStringList arguments;
    QString directory;
    QProcess *process = nullptr;
    RpcEndpoint *endpoint = nullptr;
    RpcFramer framer;
    qint64 pid = 0;
    std::shared_ptr<void> lifetime;
    std::shared_ptr<WireCounters> wire;
#ifdef Q_OS_UNIX
    int watchRead=-1,watchWrite=-1;
#endif
    bool stopping = false, ended = false, pressured = false;
    QString reason;
    FailureKind failure=FailureKind::None;
    QElapsedTimer logWindow;
    qsizetype logDelivered=0;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
#endif
};

RpcProcess::RpcProcess(const QStringList &methods, QObject *parent) : QObject(parent), hostMethods(methods),wire(std::make_shared<WireCounters>()) {}
RpcProcess::~RpcProcess() { terminate(); }
RpcProcess::WireStatistics RpcProcess::wireStatistics() const {
    return {wire->queuedBytes.load(std::memory_order_relaxed),wire->receivedBytes.load(std::memory_order_relaxed),wire->sentFrames.load(std::memory_order_relaxed),wire->receivedFrames.load(std::memory_order_relaxed)};
}

void RpcProcess::start(const QString &program, const QStringList &arguments, const QString &workingDirectory)
{
    if (worker) return;
    wire=std::make_shared<WireCounters>();
    lastFailure=FailureKind::None;
    if (!QFileInfo(program).isAbsolute() || !QFileInfo(program).isExecutable()) { lastFailure=FailureKind::RuntimeUnavailable;emit finished(-1, QStringLiteral("Plugin runtime must be an absolute executable path.")); return; }
    auto *thread = new QThread;
    auto *task = new RpcWorker(hostMethods, program, arguments, workingDirectory,lifetime,wire);
    worker = task;
    task->moveToThread(thread);
    struct Exit {int code=-1;QString reason;FailureKind failure=FailureKind::None;};
    auto exit = QSharedPointer<Exit>::create();
    connect(thread, &QThread::started, task, &RpcWorker::initialize);
    connect(thread, &QThread::finished, task, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(task, &RpcWorker::started, this, [this](qint64 processId) { pid=processId; emit started(); });
    connect(task, &RpcWorker::requestReceived, this, &RpcProcess::requestReceived);
    connect(task, &RpcWorker::requestCancelled, this, &RpcProcess::requestCancelled);
    connect(task, &RpcWorker::diagnostic, this, &RpcProcess::diagnostic);
    connect(task, &RpcWorker::stderrOutput, this, &RpcProcess::stderrOutput);
    connect(task, &RpcWorker::backpressure, this, &RpcProcess::backpressure);
    connect(task,&RpcWorker::failureClassified,this,[this](int kind){lastFailure=FailureKind(kind);});
    connect(task, &RpcWorker::responseReceived, this, [this](const QString &id, const QJsonValue &result, const QJsonObject &error) {
        auto callback = callbacks.take(id);
        if (callback) callback(result, error);
    });
    connect(task, &RpcWorker::exited, this, [exit](int code, const QString &reason,int kind) { *exit = {code, reason,FailureKind(kind)}; });
    connect(thread, &QThread::finished, this, [this, exit] {
        worker.clear();
        pid=0;
        lastFailure=exit->failure;
        const auto pending = callbacks; callbacks.clear();
        for (const auto &callback : pending) callback({}, rpcError(1004, QStringLiteral("Plugin connection closed.")));
        emit finished(exit->code, exit->reason);
    });
    thread->start();
}

QString RpcProcess::call(const QString &method, const QJsonValue &params, Callback callback, int timeoutMs)
{
    const QString id = QStringLiteral("h:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!worker) { callback({}, rpcError(1004, QStringLiteral("Plugin is unavailable."))); return id; }
    callbacks.insert(id, std::move(callback));
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task, id, method, params, timeoutMs] { task->call(id, method, params, timeoutMs); }, Qt::QueuedConnection);
    return id;
}

void RpcProcess::notify(const QString &method, const QJsonValue &params)
{
    if (!worker) return;
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task, method, params] { task->notify(method, params); }, Qt::QueuedConnection);
}
void RpcProcess::respond(const QString &token, const QJsonValue &result, const QJsonObject &error)
{
    if (!worker) return;
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task, token, result, error] { task->respond(token, result, error); }, Qt::QueuedConnection);
}
void RpcProcess::cancel(const QString &id)
{
    if (!worker) return;
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task, id] { task->cancel(id); }, Qt::QueuedConnection);
}
void RpcProcess::setDeadlinePaused(const QString &id, bool paused)
{
    if (!worker || id.isEmpty()) return;
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task, id, paused] { task->setDeadlinePaused(id, paused); }, Qt::QueuedConnection);
}
void RpcProcess::terminate()
{
    if (!worker) return;
    auto *task = static_cast<RpcWorker *>(worker.data());
    QMetaObject::invokeMethod(task, [task] { task->stop(); }, Qt::QueuedConnection);
}
}
#include "RpcProcess.moc"
