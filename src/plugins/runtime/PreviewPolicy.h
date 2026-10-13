// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include "DocumentBridge.h"

namespace PluginRuntime {
// File type eligibility is a host policy, independent of process activation.
inline bool previewMatches(const QJsonObject &policy, const QJsonObject &document) {
    if (document.isEmpty()) return false;
    if (policy["languages"].toArray().contains(document["languageId"])) return true;
    const auto path=QUrl(document["uri"].toString()).path();
    for (const auto &suffix:policy["extensions"].toArray())
        if(path.endsWith(suffix.toString(),Qt::CaseInsensitive))return true;
    return false;
}
inline QJsonValue previewSource(const QJsonObject &policy, PluginDocuments::DocumentBridge *documents) {
    const auto active=documents ? documents->active() : QJsonValue(QJsonValue::Null);
    return previewMatches(policy,active.toObject()) ? active : QJsonValue(QJsonValue::Null);
}
inline bool hasPreviewDocuments(const QJsonObject &policy, PluginDocuments::DocumentBridge *documents) {
    if(documents)for(const auto &document:documents->list())
        if(previewMatches(policy,document.toObject()))return true;
    return false;
}
}
