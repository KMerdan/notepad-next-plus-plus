// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageLease.h"
#include "RpcProcess.h"
#include "ProtocolProfile.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc!=5&&argc!=6)return 2;
    const QString store=QString::fromLocal8Bit(argv[1]),digest=QString::fromLocal8Bit(argv[2]),node=QString::fromLocal8Bit(argv[3]),host=QString::fromLocal8Bit(argv[4]);
    QString error;auto lease=PluginPackages::PackageLease::acquire(store,digest,&error);if(!lease)return 3;
    const auto root=store+'/'+digest;QFile file(root+"/notepadnext-plugin.json");if(!file.open(QIODevice::ReadOnly))return 4;
    const auto manifest=QJsonDocument::fromJson(file.readAll()).object();
    PluginRpc::RpcProcess peer({"nnp.commands.register"});peer.holdLifetime(lease);
    QObject::connect(&peer,&PluginRpc::RpcProcess::requestReceived,&app,[&](const QString &method,const QJsonValue &,const QString &token){
        if(method=="nnp.commands.register")peer.respond(token,QJsonObject{{"registrationId","test-command"}},{});
        else peer.respond(token,{},QJsonObject{{"code",-32601},{"message","Unknown fixture method"}});
    });
    QObject::connect(&peer,&PluginRpc::RpcProcess::started,&app,[&]{
        if(!lease->pinRuntimeProcess(peer.processId(),&error)){app.exit(5);return;}
        const QJsonObject identity{{"sessionId","parent-crash-test"},{"generation",1}};
        auto initialize=identity;initialize["pluginId"]=manifest["id"];initialize["protocolVersions"]=QJsonArray{"0.1"};initialize["hostVersion"]="0.15.0";initialize["apiVersion"]="0.1.0";initialize["grantedCapabilities"]=QJsonArray{"commands@1"};initialize["limits"]=PluginRpc::profileLimits();
        peer.call("nnp.initialize",initialize,[&,identity](const QJsonValue &,const QJsonObject &error){
            if(!error.isEmpty()){app.exit(6);return;}
            auto activate=identity;activate["reason"]="command";
            peer.call("nnp.plugin.activate",activate,[&](const QJsonValue &,const QJsonObject &error){
                if(!error.isEmpty()){app.exit(7);return;}
                std::printf("ready %lld\n",static_cast<long long>(peer.processId()));std::fflush(stdout);
            });
        });
    });
    QObject::connect(&peer,&PluginRpc::RpcProcess::finished,&app,[&]{app.exit(8);});
    QStringList arguments{host,root};if(argc==6)arguments.append(QString::fromLocal8Bit(argv[5]));
    QTimer::singleShot(20000,&app,[&]{app.exit(9);});peer.start(node,arguments,root);return app.exec();
}
