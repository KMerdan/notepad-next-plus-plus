// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeAppearance.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
namespace PluginIcons {
inline bool isMarkdown(const QJsonObject &metadata) {
    if(metadata.value("displayIcon")=="markdown")return true;
    // Compatible with SDK packages published before displayIcon existed.
    // This selects a bundled glyph; it never opens the uninstalled file.
    for(const auto &value:metadata["contributes"].toObject()["commands"].toArray())
        if(QFileInfo(value.toObject()["icon"].toString()).fileName()=="markdown-preview.png")return true;
    return false;
}
inline QIcon hostIcon(const QJsonObject &metadata,const QPalette &palette) {
    return NativeAppearance::brandIcon(isMarkdown(metadata)?":/icons/markdown-preview.svg":":/icons/plugins.svg",palette);
}
}
