// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
class ScintillaNext;
// Shared by native menus and the plugin bridge. Public IDs are independent
// of QAction names, translations and menu placement.
namespace EditorOperations {
QJsonArray catalog();
bool execute(ScintillaNext *editor, const QString &id, const QJsonObject &options = {});
}
