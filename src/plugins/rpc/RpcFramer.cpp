// SPDX-License-Identifier: GPL-3.0-or-later
#include "RpcFramer.h"
#include <QRegularExpression>

namespace PluginRpc {
bool RpcFramer::fail(QString *error, const QString &message)
{
    broken = true;
    buffer.clear();
    if (error) *error = message;
    return false;
}

bool RpcFramer::feed(const QByteArray &bytes, const std::function<void(const QByteArray &)> &consumer, QString *error)
{
    if (broken) return fail(error, QStringLiteral("The RPC stream is already invalid."));
    // Bound temporary accumulation even when the caller hands us multiple
    // maximum-size frames in one read.
    for (qsizetype offset = 0; offset < bytes.size(); offset += 64 * 1024) {
        buffer += bytes.mid(offset, 64 * 1024);
        if (!parse(consumer, error)) return false;
    }
    return true;
}

bool RpcFramer::parse(const std::function<void(const QByteArray &)> &consumer, QString *error)
{
    while (!broken) {
        if (bodyLength < 0) {
            const qsizetype end = buffer.indexOf("\r\n\r\n");
            if (end < 0) {
                if (buffer.size() > MaxHeaderBytes) return fail(error, QStringLiteral("RPC header exceeds 8 KiB."));
                return true;
            }
            if (end + 4 > MaxHeaderBytes) return fail(error, QStringLiteral("RPC header exceeds 8 KiB."));
            const auto header = buffer.left(end);
            auto stripped = header;
            stripped.replace("\r\n", "");
            if (stripped.contains('\r') || stripped.contains('\n')) return fail(error, QStringLiteral("RPC headers require CRLF line endings."));
            const auto lines = header.split('\n');
            bool hasLength = false;
            for (QByteArray line : lines) {
                if (line.endsWith('\r')) line.chop(1);
                const qsizetype colon = line.indexOf(':');
                if (colon <= 0) return fail(error, QStringLiteral("Malformed RPC header."));
                const auto name = line.left(colon);
                for (char c : name) if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && c != '-')
                    return fail(error, QStringLiteral("Malformed RPC header name."));
                const auto value = line.mid(colon + 1).trimmed();
                for (char c : value) if ((c < 32 && c != '\t') || c > 126) return fail(error, QStringLiteral("RPC headers must be ASCII."));
                if (name.toLower() == "content-length") {
                    if (hasLength || value.isEmpty()) return fail(error, QStringLiteral("Duplicate or empty Content-Length."));
                    for (char c : value) if (c < '0' || c > '9') return fail(error, QStringLiteral("Invalid Content-Length."));
                    bool ok = false;
                    const auto length = value.toULongLong(&ok);
                    if (!ok || length > quint64(MaxFrameBytes)) return fail(error, QStringLiteral("RPC frame exceeds 16 MiB or has an invalid length."));
                    bodyLength = qsizetype(length);
                    hasLength = true;
                } else if (name.toLower() == "content-type") {
                    const QString contentType = QString::fromLatin1(value);
                    const auto charset = QRegularExpression(QStringLiteral("charset\\s*=\\s*\"?([^;\"\\s]+)"), QRegularExpression::CaseInsensitiveOption).match(contentType);
                    if ((contentType.contains(QStringLiteral("charset"), Qt::CaseInsensitive) && !charset.hasMatch())
                        || (charset.hasMatch() && charset.captured(1).compare(QStringLiteral("utf-8"), Qt::CaseInsensitive) != 0))
                        return fail(error, QStringLiteral("Only UTF-8 RPC content is supported."));
                }
            }
            if (!hasLength) return fail(error, QStringLiteral("Missing Content-Length."));
            buffer.remove(0, end + 4);
        }
        if (buffer.size() < bodyLength) return true;
        const QByteArray json = buffer.left(bodyLength);
        buffer.remove(0, bodyLength);
        bodyLength = -1;
        consumer(json);
    }
    return false;
}

bool RpcFramer::finish(QString *error)
{
    if (broken || bodyLength >= 0 || !buffer.isEmpty()) return fail(error, QStringLiteral("RPC stream ended in a truncated frame."));
    return true;
}

QByteArray RpcFramer::frame(const QByteArray &json)
{
    return "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
}
}
