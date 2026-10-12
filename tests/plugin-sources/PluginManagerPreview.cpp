// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginManagerDialog.h"
#include "RegisteredResourceRegistry.h"
#include "InstalledPluginRegistry.h"
#include "InstallationService.h"

#include <QApplication>
#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QPalette>
#include <QTabWidget>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const bool dark=qEnvironmentVariable("NNP_PREVIEW_THEME")=="dark";
    auto palette=application.palette();palette.setColor(QPalette::Window,dark?QColor("#29292c"):QColor("#f1f1f1"));palette.setColor(QPalette::Base,dark?QColor("#202023"):QColor("#ffffff"));palette.setColor(QPalette::Text,dark?QColor("#eeeeef"):QColor("#242424"));palette.setColor(QPalette::WindowText,palette.color(QPalette::Text));palette.setColor(QPalette::PlaceholderText,dark?QColor("#b6b6bd"):QColor("#626267"));application.setPalette(palette);
    QTemporaryDir directory;
    PluginSources::RegisteredResourceRegistry registry(directory.filePath(QStringLiteral("sources.json")));
    QString error;
    if (!registry.load(&error)) qFatal("%s", qPrintable(error));
    QFile fixture(QStringLiteral(PLUGIN_FIXTURES_DIR "/marketplace.manifest.json"));
    if (!fixture.open(QIODevice::ReadOnly)) qFatal("Cannot open fixture");
    PluginSources::SourceSnapshot snapshot;
    if (!PluginSources::PluginMetadata::parse(fixture.readAll(), QStringLiteral("https://github.com/example/notepadnext-marketplace"), {}, &snapshot, &error)) qFatal("%s", qPrintable(error));
    snapshot.commit = QString(40, QLatin1Char('a'));
    if (!registry.registerSource(snapshot, &error)) qFatal("%s", qPrintable(error));
    PluginPackages::InstalledPluginRegistry installed(directory.filePath("installed.json"),directory.filePath("store"));if(!installed.load(&error))qFatal("%s",qPrintable(error));
    PluginPackages::InstallationService installer(&installed,{});
    PluginManagerDialog dialog(&registry,nullptr,nullptr,&installed,&installer);
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("uninstalledResources"));
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == QStringLiteral("notepadnext.markdown-preview")) list->setCurrentRow(i);
    dialog.show();
    if(qEnvironmentVariable("NNP_PREVIEW_TAB")=="sources")dialog.findChild<QTabWidget*>("pluginManagerTabs")->setCurrentIndex(2);
    if(qEnvironmentVariableIsSet("NNP_PREVIEW_SMALL"))dialog.resize(dialog.minimumSize());
    if (argc == 2) {
        QTimer::singleShot(300, &dialog, [&] {
            if (!dialog.grab().save(QString::fromLocal8Bit(argv[1]))) qFatal("Cannot save preview");
            application.quit();
        });
    }
    return application.exec();
}
