// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PluginLifecycleDialog.h"
#include "../PluginIcons.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
namespace PluginReview {
inline void identity(PluginLifecycleDialog &dialog,const QJsonObject &metadata) {
    auto *row=new QHBoxLayout;auto *icon=new QLabel(&dialog);icon->setFixedSize(32,32);icon->setProperty("hostBrandResource",PluginIcons::isMarkdown(metadata)?":/icons/markdown-preview.svg":":/icons/plugins.svg");icon->setPixmap(PluginIcons::hostIcon(metadata,dialog.palette()).pixmap(QSize(32,32)));row->addWidget(icon);
    auto *label=new QLabel(metadata["name"].toString()+" · "+metadata["version"].toString(),&dialog);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setProperty("dialogRole","section");row->addWidget(label,1);dialog.content->addLayout(row);
}
inline void fact(PluginLifecycleDialog &dialog,const QString &name,const QString &value) {
    auto *row=new QHBoxLayout;row->setSpacing(16);
    auto *caption=new QLabel(name,&dialog);caption->setFixedWidth(112);caption->setWordWrap(true);row->addWidget(caption,0,Qt::AlignTop);
    auto *label=new QLabel(value,&dialog);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setMinimumWidth(0);label->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);label->setTextInteractionFlags(Qt::TextSelectableByMouse);row->addWidget(label,1);dialog.content->addLayout(row);
}
inline void access(PluginLifecycleDialog &dialog,const QJsonObject &metadata) {
    auto *group=new QGroupBox(QObject::tr("Host access"),&dialog);auto *layout=new QVBoxLayout(group);
    const QHash<QString,QString> labels{{"documents.read",QObject::tr("Read open documents")},{"documents.events",QObject::tr("Follow document changes")},{"documents.write",QObject::tr("Edit documents")},{"commands",QObject::tr("Add plugin commands")},{"webviews",QObject::tr("Open preview panels")},{"storage",QObject::tr("Store plugin data")},{"configuration",QObject::tr("Save plugin settings")}};
    for(const auto &v:metadata["capabilities"].toObject()["required"].toArray()){
        const auto key=v.toString().section('@',0,0);auto *label=new QLabel(labels.value(key,key),group);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);layout->addWidget(label);
    }
    dialog.content->addWidget(group);dialog.paragraph(QObject::tr("Plugin code runs with your account’s local permissions."),true);
}
}
