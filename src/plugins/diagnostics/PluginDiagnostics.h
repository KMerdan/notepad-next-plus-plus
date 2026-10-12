// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINDIAGNOSTICS_H
#define PLUGINDIAGNOSTICS_H
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
namespace PluginDiagnostics {
enum class Failure {None,PackageInvalid,CapabilityMissing,RuntimeUnavailable,InitializationFailed,ActivationFailed,ProtocolFault,ProcessCrash,HealthTimeout,CommandFailed,UpdateFailed,OwnershipFailure};
QString category(Failure failure);
QString label(Failure failure);
QString displayMessage(const QString &message);
struct Snapshot {QJsonArray events,failures;QString error;};
class Store {
public:
    static constexpr int MaxFileBytes=512*1024,MaxEvents=256,MaxFailures=64,MaxEventBytes=24*1024;
    explicit Store(QString directory):root(std::move(directory)) {}
    Snapshot read() const;
    Snapshot append(const QJsonArray &events) const;
    static QJsonObject event(const QString &id,const QString &repository,const QString &session,const QString &kind,const QByteArray &message,Failure failure=Failure::None,const QJsonObject &details={});
    static bool validEvent(const QJsonObject &event);
    static QString format(const QJsonObject &event);
private:
    QString root;
};
// GUI-owned bounded queue; all directory/read/rotation/write work runs outside
// the GUI thread. Shutdown flushes accepted records without blocking the UI.
class Service:public QObject {
    Q_OBJECT
public:
    explicit Service(QString root,QObject *parent=nullptr);
    void append(const QJsonObject &event);
    void flush();
    bool isIdle() const {return restoredOnce&&!running&&pending.isEmpty()&&!timer.isActive();}
signals:
    void restored(const QJsonArray &events,const QJsonArray &failures);
    void unavailable(const QString &message);
    void available();
    void idle();
private:
    void pump();
    QString root,lastError;
    QJsonArray pending;
    QTimer timer;
    bool restoredOnce=false,running=false,flushing=false;
    int dropped=0,retries=0;
};
}
#endif
