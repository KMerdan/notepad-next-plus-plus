// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RPCFRAMER_H
#define RPCFRAMER_H
#include <QByteArray>
#include <QString>
#include <functional>

namespace PluginRpc {
class RpcFramer
{
public:
    static constexpr qsizetype MaxHeaderBytes = 8192;
    static constexpr qsizetype MaxFrameBytes = 16 * 1024 * 1024;
    bool feed(const QByteArray &bytes, const std::function<void(const QByteArray &)> &consumer, QString *error);
    bool finish(QString *error);
    static QByteArray frame(const QByteArray &json);
private:
    bool parse(const std::function<void(const QByteArray &)> &consumer, QString *error);
    bool fail(QString *error, const QString &message);
    QByteArray buffer;
    qsizetype bodyLength = -1;
    bool broken = false;
};
}
#endif
