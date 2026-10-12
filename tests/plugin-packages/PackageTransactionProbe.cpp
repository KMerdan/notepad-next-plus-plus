// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstalledPluginRegistry.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
using namespace PluginPackages;
namespace {
void output(const QJsonObject &value) {
    const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);
    std::fputc('\n',stdout);std::fflush(stdout);
}
[[noreturn]] void park(const QString &phase,const QString &digest={},qint64 copied=0) {
    output({{"phase",phase},{"digest",digest},{"copiedBytes",copied}});
    // The test parent must terminate this live process. The watchdog bounds
    // an abandoned fixture without running stack destructors or releasing leases.
    QTimer::singleShot(15000,[]{std::_Exit(9);});
    QCoreApplication::exec();std::_Exit(10);
}
int fail(const QString &error) {
    output({{"error",error}});
    return 3;
}
QJsonObject revision(const PreparedPackage &package) {
    return {{"version",package.metadata["version"]},{"digest",package.digest},{"root",package.root}};
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=6)return 2;
    const auto phase=app.arguments()[1],storePath=app.arguments()[2],indexPath=app.arguments()[3];
    const auto input=app.arguments()[4],repository=app.arguments()[5];
    if(!QStringList{"inspect","copying","published","installed","update-pending","update-deferred",
        "validation-started","update-confirmed","rollback-pending","recovery-busy"}.contains(phase))
        return fail("Unknown fixture phase.");
    PackageStore store(storePath);InstalledPluginRegistry index(indexPath,storePath);QString error;
    if(phase=="inspect") {
        if(!index.load(&error))return fail(error);
        QJsonArray plugins;
        for(const auto &p:index.plugins()) {
            if(!store.verify(p.package,&error)
                ||(p.previous&&!store.verify(p.previous->package,&error))
                ||(p.pendingPrevious&&!store.verify(p.pendingPrevious->package,&error)))return fail(error);
            QJsonObject value{{"id",p.id()},{"package",revision(p.package)},{"enabled",p.enabled},
                {"updatePending",p.updatePending},{"validationDeferred",p.validationDeferred},
                {"sourceIds",QJsonArray::fromStringList(p.sourceIds)},{"origin",p.origin}};
            if(p.previous)value["previous"]=revision(p.previous->package);
            if(p.pendingPrevious)value["pendingPrevious"]=revision(p.pendingPrevious->package);
            plugins.append(value);
        }
        output({{"verified",true},{"plugins",plugins}});return 0;
    }
    if(phase=="rollback-pending") {
        if(!index.load(&error))return fail(error);
        const auto installed=index.plugins();
        if(installed.size()!=1)return fail("Expected one installed plugin.");
        if(!index.beginRollback(installed[0].id(),installed[0].package.digest,&error))return fail(error);
        park(phase,index.plugin(installed[0].id()).package.digest);
    }
    QFile metadata(input+"/notepadnext-plugin.json");
    if(!metadata.open(QIODevice::ReadOnly))return fail("Cannot read fixture metadata.");
    PluginSources::SourceSnapshot source;
    if(!PluginSources::PluginMetadata::parse(metadata.readAll(),repository,{},&source,&error))return fail(error);
    if(source.resources.size()!=1)return fail("Expected one resource.");
    PackagePolicy policy;policy.capabilities={"commands@1"};
    PreparedPackage prepared;
    const auto progress=[&](qint64 bytes){if(phase=="copying"&&bytes>=64*1024)park(phase,{},bytes);};
    if(!store.prepareDirectory(input,source.resources[0],policy,&prepared,&error,nullptr,progress))return fail(error);
    if(phase=="published")park(phase,prepared.digest);
    if(!index.load(&error))return fail(error);
    if(phase=="installed") {
        if(!index.add(prepared,{"probe-source"},{{"kind","local"}},&error))return fail(error);
        park(phase,prepared.digest);
    }
    const auto current=index.plugin(source.resources[0].id);
    const bool deferred=phase=="update-deferred"||phase=="validation-started";
    if(!index.beginUpdate(prepared,{"candidate-source"},{{"kind","local"}},current.package.digest,&error,deferred))return fail(error);
    if(phase=="validation-started"&&!index.startValidation(current.id(),&error))return fail(error);
    if(phase=="update-confirmed"&&!index.confirmUpdate(current.id(),&error))return fail(error);
    park(phase,prepared.digest);
}
