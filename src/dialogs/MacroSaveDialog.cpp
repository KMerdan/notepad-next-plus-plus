/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "MacroSaveDialog.h"
#include "ui_MacroSaveDialog.h"
#include "NativeAppearance.h"
#include <QDialogButtonBox>

MacroSaveDialog::MacroSaveDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MacroSaveDialog)
{
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    ui->setupUi(this);

    connect(ui->editName, &QLineEdit::textChanged, [=, this](const QString &text) {
        ui->buttonOk->setEnabled(text.trimmed().size() > 0);
    });
#ifdef Q_OS_MACOS
    ui->verticalLayout->removeItem(ui->horizontalLayout);delete ui->horizontalLayout;
    auto *buttons=new QDialogButtonBox(this);buttons->addButton(ui->buttonCancel,QDialogButtonBox::RejectRole);buttons->addButton(ui->buttonOk,QDialogButtonBox::AcceptRole);ui->verticalLayout->addWidget(buttons);
    ui->label->setBuddy(ui->editName);ui->label_2->setBuddy(ui->keySequenceEdit);
    NativeAppearance::polishDialog(this,tr("Save Macro"),tr("Choose a name and an optional keyboard shortcut."));
    ui->buttonOk->setText(tr("Save"));NativeAppearance::setDialogPrimary(this,ui->buttonOk);
    setMinimumSize(440,250);resize(480,280);ui->editName->setFocus();
#endif
}

MacroSaveDialog::~MacroSaveDialog()
{
    delete ui;
}

QString MacroSaveDialog::getName() const
{
    return ui->editName->text();
}

QKeySequence MacroSaveDialog::getShortcut() const
{
    return ui->keySequenceEdit->keySequence();
}
