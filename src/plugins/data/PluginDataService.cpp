// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginDataService.h"
#include <QThread>
#include <memory>
namespace PluginData {
void Service::access(const QString &id,const QString &space,const QString &key,bool update,const QJsonValue &value,Callback callback,const QString &repository,const QJsonValue &expectedValue) {
    if(!Store::validPluginId(id)||!Store::validKey(key)||(space!="storage"&&space!="configuration")) {callback({{},{{"code",-32602},{"message","Invalid plugin data namespace or key"}}});return;}
    if(!Store::validValue(value)||!Store::validValue(expectedValue)){callback({{},{{"code",1008},{"message","Plugin data value exceeds 64 KiB or 32 levels"}}});return;}
    if(!update&&!expectedValue.isUndefined()){callback({{},{{"code",-32602},{"message","Expected value requires an update"}}});return;}
    enqueue(id+'\n'+repository,{space,key,update,false,value,std::move(callback),id,repository,expectedValue});
}
void Service::erase(const QString &id,Callback callback,const QString &repository) {enqueue(id+'\n'+repository,{{},{},false,true,{},std::move(callback),id,repository});}
void Service::enqueue(const QString &id,Request request) {
    if(pending>=128||queues.value(id).size()>=64){request.callback({{},{{"code",1007},{"message","Plugin data queue is full"}}});return;}
    if(queues.value(id).isEmpty()&&!running.contains(id))ready.enqueue(id);
    queues[id].enqueue(std::move(request));++pending;pump();
}
void Service::pump() {
    while(running.size()<4&&!ready.isEmpty()) {
        const auto id=ready.dequeue();
        auto request=queues[id].dequeue();running.insert(id);
        auto result=std::make_shared<Result>();const auto dataRoot=root;
        auto *worker=QThread::create([id,dataRoot,request,result] {
            const Store store(dataRoot,request.repository);*result=request.erase?store.erase(request.id):store.access(request.id,request.space,request.key,request.update,request.value,request.expectedValue);
        });
        connect(worker,&QThread::finished,this,[this,id,result,callback=request.callback] {
            running.remove(id);--pending;if(queues[id].isEmpty())queues.remove(id);else ready.enqueue(id);
            callback(*result);pump();if(isIdle())emit idle();
        });
        connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
    }
}
}
