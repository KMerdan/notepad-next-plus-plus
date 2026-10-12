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


#include "MacroManager.h"
#include "MacroRunDialog.h"

#include "ui_MacroRunDialog.h"
#include "NativeAppearance.h"
#include <QDialogButtonBox>


MacroRunDialog::MacroRunDialog(QWidget *parent, MacroManager *mm) :
    QDialog(parent, Qt::Tool),
    ui(new Ui::MacroRunDialog),
    macroManager(mm)
{
    ui->setupUi(this);

    connect(ui->buttonRun, &QPushButton::clicked, this, [=, this]() {
        Macro *selectedMacro = ui->comboBox->currentData().value<Macro*>();
        int times = -1; // for end of file

        if (ui->radioExecute->isChecked()) {
            times = ui->spinTimes->value();
        }

        emit execute(selectedMacro, times);
    });
#ifdef Q_OS_MACOS
    ui->verticalLayout->removeItem(ui->horizontalLayout);delete ui->horizontalLayout;
    auto *buttons=new QDialogButtonBox(this);buttons->addButton(ui->buttonCancel,QDialogButtonBox::RejectRole);buttons->addButton(ui->buttonRun,QDialogButtonBox::ActionRole);ui->verticalLayout->addWidget(buttons);
    NativeAppearance::polishDialog(this,tr("Run Macro"),tr("Repeat a macro a set number of times or until the end of the file."));
    NativeAppearance::setDialogPrimary(this,ui->buttonRun);
    ui->spinTimes->setAccessibleName(tr("Number of repetitions"));
    ui->spinTimes->setEnabled(ui->radioExecute->isChecked());
    setMinimumSize(440,300);resize(520,340);
#endif
}

MacroRunDialog::~MacroRunDialog()
{
    delete ui;
}

void MacroRunDialog::showEvent(QShowEvent *event)
{
    ui->comboBox->clear();

    if (macroManager->hasCurrentUnsavedMacro()) {
        ui->comboBox->addItem(macroManager->getCurrentMacro()->getName(), QVariant::fromValue(macroManager->getCurrentMacro()));
    }

    for (Macro *macro : macroManager->availableMacros()) {
        ui->comboBox->addItem(macro->getName(), QVariant::fromValue(macro));
    }
#ifdef Q_OS_MACOS
    const bool available=ui->comboBox->count()>0;
    ui->buttonRun->setEnabled(available);
    ui->comboBox->setPlaceholderText(available?QString():tr("Record or save a macro first"));
#endif

    QDialog::showEvent(event);
}
