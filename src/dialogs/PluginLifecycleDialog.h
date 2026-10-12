// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../NativeAppearance.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>

// One review layout for source registration, packages and lifecycle actions.
// Call finish() after adding content so all controls share the form skin.
class PluginLifecycleDialog:public QDialog {
public:
    QVBoxLayout *content;
    QDialogButtonBox *buttons;
    QPushButton *confirm;
    explicit PluginLifecycleDialog(QWidget *parent,const QString &id,const QString &title,const QString &description,const QString &action,bool destructive=false):QDialog(parent),title(title),description(description),destructive(destructive) {
        setObjectName(id);setWindowTitle(title);setMinimumWidth(520);resize(640,400);
        auto *root=new QVBoxLayout(this);
        auto *scroll=new QScrollArea(this);scroll->setObjectName("lifecycleReviewScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body=new QWidget(scroll);content=new QVBoxLayout(body);content->setContentsMargins(0,0,8,0);content->setSpacing(16);scroll->setWidget(body);root->addWidget(scroll,1);
        buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        confirm=buttons->button(QDialogButtonBox::Ok);confirm->setText(action);
        confirm->setProperty("dialogDestructive",destructive);
        connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    }
    QLabel *paragraph(const QString &text,bool caption=false) {
        auto *label=new QLabel(text,this);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);
        if(caption)label->setProperty("dialogRole","description");content->addWidget(label);return label;
    }
    void finish() {
        content->addStretch();layout()->addWidget(buttons);
        NativeAppearance::polishDialog(this,title,description);NativeAppearance::setDialogPrimary(this,confirm);
        if(destructive){confirm->setDefault(false);buttons->button(QDialogButtonBox::Cancel)->setDefault(true);buttons->button(QDialogButtonBox::Cancel)->setFocus();}
    }
private:
    QString title,description;
    bool destructive;
};
