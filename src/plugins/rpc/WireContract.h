// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINWIRECONTRACT_H
#define PLUGINWIRECONTRACT_H
#include <QJsonValue>
#include <QStringList>
namespace PluginRpc {
// A validator for the closed, bundled wire schema vocabulary, not an API for
// evaluating untrusted or arbitrary JSON Schema documents.
class WireContract {
public:
    static bool validate(const QString &method,const QString &part,const QJsonValue &value,QString *error=nullptr,int *failureCode=nullptr);
    static QStringList methods();
};
}
#endif
