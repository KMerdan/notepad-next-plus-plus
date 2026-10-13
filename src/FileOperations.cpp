// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileOperations.h"
#include "ScintillaNext.h"
#include "FileDialogHelpers.h"
#include <QMessageBox>
#include <QPointer>
#include <QFileDialog>
#include <QTimer>
namespace FileOperations {
Result save(ScintillaNext *value,QWidget *parent,bool saveAs,const QString &suppliedPath,Guard canCommit) {
    QPointer<ScintillaNext> editor=value;QPointer<QWidget> window=parent;
    if(!editor)return {"cancelled",{},QFileDevice::NoError};
    if(!saveAs&&editor->isSavedToDisk())return {"skipped",editor->getFilePath(),QFileDevice::NoError};
    QString path=suppliedPath;
    if(saveAs||!editor->isFile()) {
        if(path.isEmpty()) {
            QFileDialog dialog(parent,QString(),editor->isFile()?editor->getFilePath():editor->getName(),QString());
            dialog.setAcceptMode(QFileDialog::AcceptSave);dialog.setSupportedSchemes({"file"});
            dialog.setFilter(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::AllDirs|QDir::Files|QDir::Hidden);
            QTimer validity;if(canCommit){QObject::connect(&validity,&QTimer::timeout,&dialog,[&]{if(!editor||!window||!canCommit({}))dialog.reject();});validity.start(25);}
            if(dialog.exec()==QDialog::Accepted)path=dialog.selectedFiles().value(0);
        }
        if(path.isEmpty())return {"cancelled",{},QFileDevice::NoError};
    } else path=editor->getFilePath();
    if(!editor||!window||(canCommit&&!canCommit(path)))return {"cancelled",path,QFileDevice::NoError};
    const auto error=(saveAs||!editor->isFile())?editor->saveAs(path):editor->save();
    return {error==QFileDevice::NoError?"completed":"failed",path,error};
}
Result approveClose(ScintillaNext *value,QWidget *parent,bool ifClean,Guard canCommit) {
    QPointer<ScintillaNext> editor=value;QPointer<QWidget> window=parent;
    if(!editor)return {"cancelled",{},QFileDevice::NoError};
    if(editor->isSavedToDisk()||!editor->modify()) {
        if(canCommit&&!canCommit({}))return {"cancelled",{},QFileDevice::NoError};
        return {};
    }
    if(ifClean)return {"skipped",{},QFileDevice::NoError};
    QMessageBox dialog(QMessageBox::Question,QObject::tr("Save Changes"),QObject::tr("Save changes to %1 before closing?").arg(editor->getName()),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,parent);
    dialog.setTextFormat(Qt::PlainText);dialog.setDefaultButton(QMessageBox::Save);
    QTimer validity;if(canCommit){QObject::connect(&validity,&QTimer::timeout,&dialog,[&]{if(!editor||!window||!canCommit({}))dialog.reject();});validity.start(25);}
    const auto choice=dialog.exec();
    if((choice!=QMessageBox::Save&&choice!=QMessageBox::Discard)||!editor||!window||(canCommit&&!canCommit({})))return {"cancelled",{},QFileDevice::NoError};
    if(choice==QMessageBox::Save){const auto result=save(editor,parent,false,{},canCommit);if(result.status!="completed"&&result.status!="skipped")return result;}
    return {};
}
}
