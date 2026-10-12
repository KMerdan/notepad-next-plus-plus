// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINZIPEXTRACTOR_H
#define PLUGINZIPEXTRACTOR_H
#include <QString>
#include <atomic>
#include <functional>
namespace PluginPackages {
// Bounded ZIP release profile: one disk, stored/deflate, regular files/dirs,
// ASCII or strict UTF-8 paths; no ZIP64, encryption, links or executable hooks.
class ZipExtractor {
public:
    static constexpr qint64 MaxArchiveBytes=128*1024*1024;
    static bool extract(const QString &archive,const QString &emptyDirectory,QString *error,
                        const std::atomic_bool *cancel=nullptr,const std::function<void(qint64)> &progress={});
};
}
#endif
