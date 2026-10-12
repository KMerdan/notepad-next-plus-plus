// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINDATASTORE_H
#define PLUGINDATASTORE_H
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
namespace PluginData {
struct Result { QJsonValue value=QJsonValue(QJsonValue::Null); QJsonObject error; };
// Bounded, plugin-owned JSON records. Call on a worker; never expose paths to
// plugin code. Missing update values delete a key; JSON null remains a value.
class Store {
public:
    static constexpr qint64 MaxFileBytes=1024*1024;
    static constexpr qint64 MaxValueBytes=64*1024;
    static constexpr int MaxKeys=256;
    explicit Store(QString root,QString repository={}):root(std::move(root)),repository(std::move(repository)) {}
    static bool validPluginId(const QString &id);
    static bool validKey(const QString &key);
    static bool validValue(const QJsonValue &value);
    Result access(const QString &id,const QString &space,const QString &key,bool update=false,const QJsonValue &value=QJsonValue::Undefined,const QJsonValue &expectedValue=QJsonValue::Undefined) const;
    Result erase(const QString &id) const;
    QString filePath(const QString &id) const;
private:
    QString root,repository;
};
}
#endif
