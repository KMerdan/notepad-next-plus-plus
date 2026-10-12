// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINSEMVER_H
#define PLUGINSEMVER_H
#include <QStringList>
namespace PluginPackages {
struct SemVer {
    QStringList core, prerelease;
    static bool parse(const QString &text, SemVer *result);
    int compare(const SemVer &other) const;
    // Strict profile: full versions, = > >= < <= ^ ~, whitespace AND,
    // || OR and *. Unsupported shorthand is rejected, never guessed.
    static bool satisfies(const QString &version, const QString &range, QString *error);
};
}
#endif
