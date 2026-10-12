// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINPROCESSIDENTITY_H
#define PLUGINPROCESSIDENTITY_H
#include <QJsonObject>
namespace PluginPackages {
// PID plus native creation time, not an executable name or a reused PID.
// Unknown is deliberately distinct from Exited: uncertain probes retain code.
struct ProcessIdentity {
    enum class State {Alive,Exited,Unknown};
    qint64 pid=0;
    QString platform,start;
    QJsonObject json() const;
    static bool parse(const QJsonObject &json,ProcessIdentity *result);
    static bool capture(qint64 pid,ProcessIdentity *result,QString *error);
    State state() const;
};
}
#endif
