// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINDATASERVICE_H
#define PLUGINDATASERVICE_H
#include "PluginDataStore.h"
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <functional>
namespace PluginData {
class Service:public QObject {
    Q_OBJECT
public:
    using Callback=std::function<void(const Result &)>;
    explicit Service(QString root,QObject *parent=nullptr):QObject(parent),root(std::move(root)) {}
    void access(const QString &id,const QString &space,const QString &key,bool update,const QJsonValue &value,Callback callback,const QString &repository={},const QJsonValue &expectedValue=QJsonValue::Undefined);
    void erase(const QString &id,Callback callback,const QString &repository={});
    bool isIdle() const {return pending==0;}
signals:
    void idle();
private:
    struct Request {QString space,key;bool update=false,erase=false;QJsonValue value;Callback callback;QString id,repository;QJsonValue expectedValue=QJsonValue::Undefined;};
    void enqueue(const QString &id,Request request);
    void pump();
    QString root;
    QHash<QString,QQueue<Request>> queues;
    QQueue<QString> ready;
    QSet<QString> running;
    int pending=0;
};
}
#endif
