// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JSONIDMASK_H
#define JSONIDMASK_H
#include <QByteArray>
#include <QString>

namespace PluginRpc {
// Validate JSON before scanning IDs, and replace only numeric envelope IDs
// for QJson's tree parser. Responses still use the original byte tokens.
bool maskNumericIds(const QByteArray &input, QByteArray *treeInput, QString *fault);
}
#endif
