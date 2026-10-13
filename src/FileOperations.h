// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFileDevice>
#include <QString>
#include <functional>
class ScintillaNext;
class QWidget;
namespace FileOperations {
struct Result {
    QString status="completed",path;
    QFileDevice::FileError error=QFileDevice::NoError;
};
using Guard=std::function<bool(const QString &)>;
Result save(ScintillaNext *editor,QWidget *parent,bool saveAs,const QString &path={},Guard canCommit={});
Result approveClose(ScintillaNext *editor,QWidget *parent,bool ifClean,Guard canCommit={});
}
