// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINHTMLPOLICY_H
#define PLUGINHTMLPOLICY_H
#include <QHash>
#include <QByteArray>
#include <QUrl>
namespace PluginWebviews {
class HtmlPolicy {
public:
    static constexpr qsizetype MaxHtmlBytes = 6 * 1024 * 1024;
    static bool sanitize(const QString &fragment, const QHash<QString, QUrl> &images, QString *safe, QString *error);
    static bool readImage(const QString &root, const QString &relative, QByteArray *bytes, QByteArray *mime, QString *error);
};
}
#endif
