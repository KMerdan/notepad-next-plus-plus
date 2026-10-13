// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
namespace TextEncoding {
QStringList supported();
QString detected(const QByteArray &bytes);
bool decode(const QByteArray &bytes, const QString &encoding, QString *text, QString *error);
bool encode(const QString &text, const QString &encoding, QByteArray *bytes, QString *error);
QString label(const QString &encoding);
}
