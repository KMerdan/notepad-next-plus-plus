// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINMANAGERDIALOG_H
#define PLUGINMANAGERDIALOG_H

#include <QDialog>
#include "PluginMetadata.h"
#include "GitHubReleaseService.h"

class QProgressBar;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QNetworkAccessManager;
class QComboBox;
class ManagerDetailPane;

namespace PluginSources {
class RegisteredResourceRegistry;
class SourceEntryResolver;
}
namespace PluginPackages {class InstalledPluginRegistry;class InstallationService;class PluginController;}

class PluginManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluginManagerDialog(PluginSources::RegisteredResourceRegistry *registry, QWidget *parent = nullptr,
                                 QNetworkAccessManager *network = nullptr,
                                 PluginPackages::InstalledPluginRegistry *installed = nullptr,
                                 PluginPackages::InstallationService *installer = nullptr,
                                 PluginPackages::PluginController *controller = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    bool refreshingAppearance=false;
    bool ready=false;
    void reload();
    void updateResourceDetails();
    void updateInstalledDetails();
    void inspectInstalledMetadata();
    void installLocalPackage();
    void installGitHubPackage();
    void updatePlugin();
    void reviewReleases(const QString &pluginId,const QVector<PluginPackages::ReleaseAsset> &assets);
    void updateSourceDetails();
    void registerResources();
    void refreshSource();
    void refreshCatalog();
    void advanceCatalogRefresh();
    void removeSource();
    void inspect(const QJsonObject &metadata);
    void finishLookup(const PluginSources::SourceSnapshot &snapshot);
    void updateBusyState();
    void updateViewContext();

    PluginSources::RegisteredResourceRegistry *registry;
    PluginSources::SourceEntryResolver *resolver;
    PluginPackages::InstalledPluginRegistry *installed;
    PluginPackages::InstallationService *installer;
    PluginPackages::PluginController *controller;
    QVector<PluginSources::RegisteredResource> resources;
    QVector<PluginSources::SourceSnapshot> sources;
    QTabWidget *tabs=nullptr;
    QProgressBar *operationProgress;
    QPushButton *manageResourceButton;
    QLineEdit *search;
    QListWidget *resourceList;
    QListWidget *sourceList;
    QListWidget *installedList;
    QTextBrowser *installedDetails;
    QTextBrowser *resourceDetails;
    QTextBrowser *sourceDetails;
    QLabel *status;
    QPushButton *registerButton;
    QPushButton *refreshButton;
    QPushButton *removeButton;
    QPushButton *inspectResourceButton;
    QPushButton *inspectSourceButton;
    QPushButton *cancelButton;
    QPushButton *installButton;
    QPushButton *localInstallButton;
    QPushButton *enableButton;
    QPushButton *reloadButton;
    QPushButton *uninstallButton;
    QPushButton *logsButton;
    QPushButton *inspectInstalledButton;
    QPushButton *overviewButton;
    QPushButton *moreButton;
    QComboBox *commandChoice;
    QPushButton *runCommandButton;
    QPushButton *updateButton;
    QPushButton *rollbackButton;
    QPushButton *refreshCatalogButton;
    QLabel *viewContext;
    QLabel *listContexts[3];
    ManagerDetailPane *installedPane;
    ManagerDetailPane *resourcePane;
    ManagerDetailPane *sourcePane;
    bool refreshing = false;
    bool catalogRefreshing = false;
    QVector<PluginSources::SourceSnapshot> catalogQueue;
    int catalogFailures = 0;
    int catalogTotal = 0;
    QString checkingUpdateId;
    QString pendingReleaseId;
    QJsonObject pendingReleaseMetadata;
    bool pendingReleaseUpdate=false;
};

#endif
