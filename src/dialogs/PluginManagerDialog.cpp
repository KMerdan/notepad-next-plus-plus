// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginManagerDialog.h"
#include "../NativeAppearance.h"
#include "../PluginIcons.h"
#include "PluginLifecycleDialog.h"
#include "PluginReview.h"
#include <QProgressBar>
#include <QTimer>
#include <QEvent>
#include "RegisteredResourceRegistry.h"
#include "SourceEntryResolver.h"
#include "InstalledPluginRegistry.h"
#include "InstallationService.h"
#include "PluginController.h"
#include "SemVer.h"

#include <QDialogButtonBox>
#include <QScrollBar>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFileInfo>
#include <QtMath>
#include <QTabBar>
#include <QStyle>
#include <QButtonGroup>
#include <QMenu>
#include <QUrl>
#include <algorithm>

using namespace PluginSources;

// Native equivalents of the identity, notice and lifecycle components in
// design-draft.json. The scroll area keeps details usable at smaller sizes.
class ManagerDetailPane : public QWidget {
public:
    QLabel *icon,*title,*subtitle,*noticeTitle,*noticeBody;
    QWidget *identity,*notice,*states;
    QLabel *values[4];
    QVBoxLayout *layout;
    QHBoxLayout *actions;
    QTextBrowser *overview;
    explicit ManagerDetailPane(bool installed,QWidget *parent=nullptr):QWidget(parent) {
        setObjectName("managerDetailPane");
        layout=new QVBoxLayout(this);layout->setContentsMargins(24,24,24,24);layout->setSpacing(12);
        identity=new QWidget(this);auto *row=new QHBoxLayout(identity);row->setContentsMargins(0,0,0,0);row->setSpacing(12);
        icon=new QLabel(identity);icon->setFixedSize(40,40);icon->setAlignment(Qt::AlignCenter);row->addWidget(icon);
        auto *nameColumn=new QVBoxLayout;nameColumn->setSpacing(4);
        title=new QLabel(identity);title->setObjectName("detailTitle");title->setWordWrap(true);title->setTextFormat(Qt::PlainText);
        subtitle=new QLabel(identity);subtitle->setObjectName("detailSubtitle");subtitle->setWordWrap(true);subtitle->setTextFormat(Qt::PlainText);
        nameColumn->addWidget(title);nameColumn->addWidget(subtitle);row->addLayout(nameColumn,1);layout->addWidget(identity);
        states=new QWidget(this);auto *stateRow=new QHBoxLayout(states);stateRow->setContentsMargins(0,0,0,0);stateRow->setSpacing(8);
        const QStringList captions{tr("Installed version"),tr("Enablement"),tr("Process"),tr("Preview panels")};
        for(int i=0;i<4;++i){auto *card=new QWidget(states);card->setObjectName("stateCard");auto *column=new QVBoxLayout(card);column->setContentsMargins(12,6,12,6);column->setSpacing(2);auto *caption=new QLabel(captions[i],card);caption->setObjectName("managerCaption");values[i]=new QLabel(card);values[i]->setTextFormat(Qt::PlainText);auto font=values[i]->font();font.setWeight(QFont::DemiBold);values[i]->setFont(font);column->addWidget(caption);column->addWidget(values[i]);stateRow->addWidget(card,1);}
        if(installed)values[3]->setObjectName("pluginPanelCountState");
        states->setVisible(installed);layout->addWidget(states);
        notice=new QWidget(this);notice->setObjectName("managerNotice");auto *noticeLayout=new QVBoxLayout(notice);noticeLayout->setContentsMargins(12,12,12,12);noticeLayout->setSpacing(8);
        noticeTitle=new QLabel(notice);noticeTitle->setObjectName("noticeTitle");noticeBody=new QLabel(notice);noticeBody->setWordWrap(true);
        for(auto *label:{noticeTitle,noticeBody})label->setTextFormat(Qt::PlainText);
        noticeLayout->addWidget(noticeTitle);noticeLayout->addWidget(noticeBody);layout->addWidget(notice);notice->hide();
        actions=new QHBoxLayout;actions->setSpacing(8);if(installed)layout->addLayout(actions);
        overview=new QTextBrowser(this);overview->setFrameShape(QFrame::NoFrame);overview->setOpenLinks(false);overview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        overview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);overview->document()->setDocumentMargin(0);
        overview->document()->setDefaultStyleSheet("p { margin: 0 0 12px 0; line-height: 138%; } td { padding: 5px 0; } .caption { font-size:11px; }");
        connect(overview->document()->documentLayout(),&QAbstractTextDocumentLayout::documentSizeChanged,this,[this](const QSizeF &size){overview->setFixedHeight(qMax(36,qCeil(size.height())+4));});
        layout->addWidget(overview);if(!installed)layout->addLayout(actions);
        layout->addStretch();
    }
    void setIdentity(const QString &name,const QString &sub,const QIcon &image){identity->show();title->setText(name);subtitle->setText(sub);subtitle->setVisible(!sub.isEmpty());icon->setPixmap(image.pixmap(24,24));}
    void setNotice(const QString &heading,const QString &body){noticeTitle->setText(heading);noticeBody->setText(body);notice->setVisible(!heading.isEmpty());}
    void empty(const QString &message){identity->hide();states->hide();notice->hide();overview->setPlainText(message);}
};

namespace {
QString field(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString().toHtmlEscaped();
}

QString paragraph(const QString &label, const QString &value)
{
    return QStringLiteral("<tr><td width='144' style='color:%3'>%1</td><td>%2</td></tr>").arg(label.toHtmlEscaped(),value.toHtmlEscaped(),qApp->palette().color(QPalette::PlaceholderText).name());
}

QIcon resourceIcon(const QJsonObject &metadata,const QPalette &palette=QApplication::palette()) {
    return PluginIcons::hostIcon(metadata,palette);
}
QString displayOrigin(const QString &origin){const QUrl url(origin);return url.isLocalFile()?QFileInfo(url.toLocalFile()).dir().dirName():origin.startsWith("https://")?origin.mid(8):origin;}
void setPrimary(QPushButton *button,bool primary){if(button->property("primary").toBool()==primary)return;button->setProperty("primary",primary);button->style()->unpolish(button);button->style()->polish(button);}
QString processLabel(const QString &state) {
    if(state=="active")return PluginManagerDialog::tr("Running");
    if(state=="ready")return PluginManagerDialog::tr("Not running");
    if(state=="disabled"||state.isEmpty())return PluginManagerDialog::tr("Stopped");
    if(state=="failed")return PluginManagerDialog::tr("Failed");
    QString text=state;text[0]=text[0].toUpper();return text;
}
class PluginRowDelegate:public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override {return {260,94};}
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        painter->save();const bool selected=option.state&QStyle::State_Selected;
        const auto c=NativeAppearance::colors(option.palette);const QColor soft(c.selected);
        painter->fillRect(option.rect,selected?soft:option.palette.color(QPalette::Base));
        const auto icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));icon.paint(painter,QRect(option.rect.x()+16,option.rect.y()+35,24,24));
        const int x=option.rect.x()+56,width=option.rect.width()-72;
        auto font=option.font;font.setPixelSize(13);font.setWeight(QFont::DemiBold);painter->setFont(font);painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(QRect(x,option.rect.y()+14,width,22),Qt::AlignVCenter,QFontMetrics(font).elidedText(index.data(Qt::DisplayRole).toString().section('\n',0,0),Qt::ElideRight,width));
        font.setPixelSize(11);font.setWeight(QFont::Normal);painter->setFont(font);painter->setPen(QColor(c.secondary));
        painter->drawText(QRect(x,option.rect.y()+38,width,18),Qt::AlignVCenter,QFontMetrics(font).elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideRight,width));
        painter->setPen(QColor(c.secondary));
        painter->drawText(QRect(x,option.rect.y()+60,width,18),Qt::AlignVCenter,QFontMetrics(font).elidedText(index.data(Qt::UserRole+2).toString(),Qt::ElideRight,width));
        painter->setPen(QColor(c.border));painter->drawLine(option.rect.bottomLeft(),option.rect.bottomRight());
        if(option.state&QStyle::State_HasFocus){QPen pen(option.palette.color(QPalette::Highlight));pen.setStyle(Qt::DotLine);painter->setPen(pen);painter->drawRect(option.rect.adjusted(2,2,-3,-3));}painter->restore();
    }
};

QString managerStyle(const QPalette &palette) {
    const auto c=NativeAppearance::colors(palette);
    return QString(R"CSS(
        QDialog#pluginManager { background:%1; color:%4; }
        QWidget#managerDetailPane, QListWidget, QScrollArea { background:%2; color:%4; }
        QLabel { color:%4; background:transparent; }
        QLabel#managerHeading, QLabel#detailTitle { font-size:20px; font-weight:600; }
        QLabel#detailSubtitle { color:%5; }
        QLabel#managerCaption, QLabel#listHint { color:%5; font-size:11px; }
        QLabel#listContext { background:%3; padding-left:16px; font-weight:600; }
        QLabel#listHint { background:%3; padding:8px 16px; }
        QWidget#stateCard { background:%3; border-radius:4px; }
        QWidget#managerNotice { background:%3; border-radius:6px; }
        QLabel#noticeTitle { color:%4; font-weight:600; }
        QTextBrowser { background:transparent; color:%4; border:0; }
        QPushButton { background:%2; color:%4; border:1px solid %6; border-radius:6px; padding:4px 12px; min-height:16px; }
        QPushButton:hover { background:%9; border-color:%6; }
        QPushButton:checked { background:%7; color:%8; }
        QPushButton:focus, QComboBox:focus { border:1px solid %8; }
        QPushButton[primary="true"] { background:%8; color:%11; }
        QPushButton[primary="true"]:hover { background:%8; }
        QPushButton:disabled { color:%5; background:%3; }
        QPushButton[destructive="true"] { color:%10; }
        QLineEdit { background:%2; color:%4; border:1px solid %12; border-radius:6px; padding:4px 12px; }
        QLineEdit:focus { border-color:%8; }
        QComboBox { color:%4; background:%2; border:1px solid %12; border-radius:6px; padding:4px 8px; }
        QTabBar { background:%1; border:0; }
        QTabWidget::pane { border:0; border-top:1px solid %6; background:%2; }
        QTabWidget::tab-bar { alignment:left; left:16px; }
        QTabBar::tab { color:%4; background:%2; border-radius:4px; margin:7px 8px 7px 0; padding:4px 12px; }
        QTabBar::tab:selected { background:%7; color:%8; }
        QTabBar::tab:focus { border:1px solid %8; }
        QSplitter::handle { background:%6; }
        QProgressBar { background:%2; border:1px solid %6; border-radius:4px; min-height:6px; max-height:6px; } QProgressBar::chunk { background:%8; border-radius:3px; }
    )CSS").arg(c.chrome,c.surface,c.gutter,c.text,c.secondary,c.border,c.selected,c.accent,c.hover,c.destructive,c.onAccent,c.fieldBorder);
}

QWidget *listPage(QListWidget **list, QTextBrowser **details, QHBoxLayout **actions,ManagerDetailPane **pane,QLabel **context,int kind)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto *splitter = new QSplitter(page);
    splitter->setChildrenCollapsible(false);splitter->setHandleWidth(1);
    auto *listColumn=new QWidget(splitter);auto *listLayout=new QVBoxLayout(listColumn);listLayout->setContentsMargins(0,0,0,0);listLayout->setSpacing(0);
    *context=new QLabel(listColumn);(*context)->setObjectName("listContext");(*context)->setFixedHeight(40);listLayout->addWidget(*context);
    *list = new QListWidget(listColumn);(*list)->setMinimumWidth(250);(*list)->setFrameShape(QFrame::NoFrame);(*list)->setItemDelegate(new PluginRowDelegate(*list));listLayout->addWidget(*list,1);
    auto *hint=new QLabel(kind==0?PluginManagerDialog::tr("Select a plugin to inspect and manage it."):kind==1?PluginManagerDialog::tr("Browse registered plugins. Installed plugins stay in this catalog."):PluginManagerDialog::tr("Sources can register one or many resources."),listColumn);
    hint->setObjectName("listHint");hint->setWordWrap(true);hint->setMinimumHeight(40);listLayout->addWidget(hint);
    auto *scroll=new QScrollArea(splitter);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);
    *pane=new ManagerDetailPane(kind==0);scroll->setWidget(*pane);*details=(*pane)->overview;*actions=(*pane)->actions;
    splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({336,743});
    layout->addWidget(splitter);
    return page;
}
}

PluginManagerDialog::PluginManagerDialog(RegisteredResourceRegistry *registry, QWidget *parent, QNetworkAccessManager *network,
                                       PluginPackages::InstalledPluginRegistry *installedRegistry,PluginPackages::InstallationService *installService,PluginPackages::PluginController *pluginController)
    : QDialog(parent), registry(registry), resolver(new SourceEntryResolver(this, network)),installed(installedRegistry),installer(installService),controller(pluginController)
{
    setWindowTitle(tr("Plugin Manager"));
    setWindowIcon(NativeAppearance::brandIcon(":/icons/plugins.svg",palette()));
    setObjectName("pluginManager");resize(1080,664);setMinimumSize(820,560);
    auto interfaceFont=font();interfaceFont.setPixelSize(13);setFont(interfaceFont);
    setStyleSheet(managerStyle(palette()));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto *heading = new QHBoxLayout;heading->setContentsMargins(16,12,16,12);heading->setSpacing(12);
    auto *icon = new QLabel(this);icon->setObjectName("managerIdentityIcon");
    icon->setPixmap(windowIcon().pixmap(24, 24));
    heading->addWidget(icon);
    auto *title = new QLabel(tr("Plugin Manager"), this);
    title->setObjectName("managerHeading");
    heading->addWidget(title);
    heading->addStretch();
    auto *brand=new QLabel(QApplication::applicationDisplayName(),this);brand->setObjectName("managerCaption");heading->addWidget(brand);
    layout->addLayout(heading);
    auto *searchRow=new QHBoxLayout;searchRow->setContentsMargins(16,8,16,8);searchRow->setSpacing(8);
    search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("resourceSearch"));
    search->setPlaceholderText(tr("Search plugins"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(310);searchRow->addWidget(search);searchRow->addStretch();
    refreshCatalogButton=new QPushButton(tr("Refresh Catalog"),this);refreshCatalogButton->setObjectName("refreshPluginCatalog");searchRow->addWidget(refreshCatalogButton);
    registerButton = new QPushButton(tr("Add Source…"), this);registerButton->setObjectName("registerResources");searchRow->addWidget(registerButton);layout->addLayout(searchRow);
    tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("pluginManagerTabs"));
    tabs->setDocumentMode(true);tabs->tabBar()->setExpanding(false);
    viewContext=new QLabel(this);viewContext->setObjectName("managerCaption");viewContext->setContentsMargins(0,0,16,0);viewContext->setFixedHeight(38);viewContext->setAlignment(Qt::AlignRight|Qt::AlignVCenter);tabs->setCornerWidget(viewContext);
    QHBoxLayout *installedActions;
    tabs->addTab(listPage(&installedList,&installedDetails,&installedActions,&installedPane,&listContexts[0],0),tr("Installed (0)"));
    installedList->setObjectName("installedPlugins");installedDetails->setObjectName("installedPluginDetails");
    enableButton=new QPushButton(tr("Enable"),this);enableButton->setObjectName("togglePluginEnabled");
    reloadButton=new QPushButton(tr("Reload"),this);reloadButton->setObjectName("reloadPlugin");
    uninstallButton=new QPushButton(tr("Uninstall…"),this);uninstallButton->setObjectName("uninstallPlugin");
    logsButton=new QPushButton(tr("Logs"),this);logsButton->setObjectName("pluginLogs");
    inspectInstalledButton=new QPushButton(tr("Inspect"),this);
    inspectInstalledButton->setObjectName("inspectInstalledPlugin");inspectInstalledButton->setCheckable(true);
    overviewButton=new QPushButton(tr("Overview"),this);overviewButton->setObjectName("pluginOverview");overviewButton->setCheckable(true);overviewButton->setChecked(true);
    auto *detailViews=new QButtonGroup(this);detailViews->addButton(overviewButton);detailViews->addButton(inspectInstalledButton);
    commandChoice=new QComboBox(this);commandChoice->setObjectName("pluginCommandChoice");commandChoice->setAccessibleName(tr("Plugin command"));
    runCommandButton=new QPushButton(tr("Open Plugin"),this);runCommandButton->setObjectName("runPluginCommand");runCommandButton->setProperty("primary",true);
    updateButton=new QPushButton(tr("Update…"),this);updateButton->setObjectName("updatePlugin");
    rollbackButton=new QPushButton(tr("Roll Back…"),this);rollbackButton->setObjectName("rollbackPlugin");
    installedActions->addWidget(runCommandButton);installedActions->addWidget(enableButton);installedActions->addWidget(updateButton);installedActions->addStretch();installedActions->addWidget(uninstallButton);
    uninstallButton->setProperty("destructive",true);
    auto *inspectRow=new QHBoxLayout;inspectRow->setSpacing(8);inspectRow->addWidget(overviewButton);inspectRow->addWidget(inspectInstalledButton);inspectRow->addStretch();inspectRow->addWidget(logsButton);
    moreButton=new QPushButton(tr("More…"),this);auto *moreMenu=new QMenu(moreButton);moreButton->setMenu(moreMenu);inspectRow->addWidget(moreButton);
    const auto menuProxy=[&](QPushButton *button){auto *action=moreMenu->addAction(button->text());connect(action,&QAction::triggered,button,&QPushButton::click);connect(moreMenu,&QMenu::aboutToShow,this,[button,action]{action->setEnabled(button->isEnabled());});};
    menuProxy(reloadButton);menuProxy(rollbackButton);moreMenu->addSeparator();connect(moreMenu->addAction(tr("Raw Metadata…")),&QAction::triggered,this,&PluginManagerDialog::inspectInstalledMetadata);
    reloadButton->hide();rollbackButton->hide();
    installedPane->layout->insertLayout(installedPane->layout->indexOf(installedDetails),inspectRow);
    installedPane->layout->insertWidget(installedPane->layout->indexOf(installedDetails),commandChoice);
    QHBoxLayout *resourceActions;
    tabs->addTab(listPage(&resourceList, &resourceDetails, &resourceActions,&resourcePane,&listContexts[1],1), tr("Discover (0)"));
    resourceList->setObjectName(QStringLiteral("discoverPlugins"));
    installButton = new QPushButton(tr("Install…"), this);installButton->setProperty("primary",true);
    installButton->setObjectName("installPlugin");
    installButton->setMinimumWidth(172);
    resourceActions->addWidget(installButton);
    manageResourceButton=new QPushButton(tr("Manage Installed Plugin"),this);manageResourceButton->setObjectName("manageDiscoveredPlugin");manageResourceButton->setProperty("primary",true);resourceActions->addWidget(manageResourceButton);
    connect(manageResourceButton,&QPushButton::clicked,this,[this]{const auto id=resourceList->currentItem()?resourceList->currentItem()->data(Qt::UserRole).toString():QString();search->clear();for(int row=0;row<installedList->count();++row)if(installedList->item(row)->data(Qt::UserRole).toString()==id)installedList->setCurrentRow(row);tabs->setCurrentIndex(0);});
    localInstallButton=new QPushButton(tr("Install Local Package…"),this);
    localInstallButton->setObjectName("installLocalPlugin");resourceActions->addWidget(localInstallButton);
    inspectResourceButton = new QPushButton(tr("Inspect Source"), this);
    resourceActions->addWidget(inspectResourceButton);
    resourceActions->addStretch();
    QHBoxLayout *sourceActions;
    tabs->addTab(listPage(&sourceList, &sourceDetails, &sourceActions,&sourcePane,&listContexts[2],2), tr("Sources (0)"));
    sourceList->setObjectName(QStringLiteral("registeredSources"));
    sourcePane->icon->hide();sourcePane->layout->removeWidget(sourcePane->notice);sourcePane->layout->insertWidget(sourcePane->layout->count()-1,sourcePane->notice);
    auto *viewResources=new QPushButton(tr("View Discover"),this);viewResources->setProperty("primary",true);sourceActions->addWidget(viewResources);
    connect(viewResources,&QPushButton::clicked,this,[this]{search->clear();tabs->setCurrentIndex(1);});
    refreshButton = new QPushButton(tr("Refresh Metadata"), this);
    refreshButton->setObjectName(QStringLiteral("refreshSourceMetadata"));
    removeButton = new QPushButton(tr("Remove Source…"), this);
    removeButton->setObjectName("removeSource");removeButton->setProperty("destructive",true);
    inspectSourceButton = new QPushButton(tr("Inspect Metadata"), this);
    sourceActions->addWidget(refreshButton);
    sourceActions->addWidget(inspectSourceButton);
    sourceActions->addStretch();
    sourceActions->addWidget(removeButton);
    tabs->setCurrentIndex(1);tabs->tabBar()->setDrawBase(false);tabs->tabBar()->setAutoFillBackground(false);tabs->tabBar()->setAttribute(Qt::WA_StyledBackground,true);
    layout->addWidget(tabs,1);
    auto *footer=new QHBoxLayout;footer->setContentsMargins(16,10,16,10);footer->setSpacing(8);
    status = new QLabel(this);
    status->setObjectName(QStringLiteral("registrationStatus"));
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setObjectName("registrationStatus");status->setProperty("caption",true);auto statusFont=font();statusFont.setPixelSize(11);status->setFont(statusFont);footer->addWidget(status,1);
    operationProgress=new QProgressBar(this);operationProgress->setObjectName("pluginOperationProgress");operationProgress->setRange(0,0);operationProgress->setMaximumWidth(200);operationProgress->setTextVisible(false);operationProgress->hide();footer->addWidget(operationProgress);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    cancelButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::ActionRole);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Done"));footer->addWidget(buttons);layout->addLayout(footer);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this]{catalogQueue.clear();catalogRefreshing=false;resolver->cancel();});
    connect(cancelButton, &QPushButton::clicked,this,[this]{catalogQueue.clear();catalogRefreshing=false;resolver->cancel();if(installer)installer->cancel();updateBusyState();});
    connect(installButton,&QPushButton::clicked,this,&PluginManagerDialog::installGitHubPackage);
    connect(localInstallButton,&QPushButton::clicked,this,&PluginManagerDialog::installLocalPackage);
    const auto installedId=[this]{return installedList->currentItem()?installedList->currentItem()->data(Qt::UserRole).toString():QString();};
    connect(updateButton,&QPushButton::clicked,this,&PluginManagerDialog::updatePlugin);
    connect(rollbackButton,&QPushButton::clicked,this,[this,installedId] {
        if(!controller||!installed)return;const auto p=installed->plugin(installedId());if(!p.previous)return;
        PluginLifecycleDialog review(this,"rollbackPluginReview",tr("Restore Previous Version"),tr("Current commands and panels will close. Enablement and data are preserved. Open the preview again after restoring."),tr("Restore"));
        PluginReview::identity(review,p.previous->package.metadata);PluginReview::fact(review,tr("Version"),p.package.metadata["version"].toString()+" → "+p.previous->package.metadata["version"].toString());review.finish();
        if(review.exec()==QDialog::Accepted)controller->rollback(p.id());
    });
    connect(runCommandButton,&QPushButton::clicked,this,[this,installedId]{if(controller&&!commandChoice->currentData().toString().isEmpty()){const auto command=commandChoice->currentData().toString();hide();if(parentWidget()){parentWidget()->raise();parentWidget()->activateWindow();}controller->invoke(installedId(),command);}});
    connect(commandChoice,&QComboBox::currentIndexChanged,this,[this]{if(commandChoice->currentIndex()>=0)runCommandButton->setText(commandChoice->currentText());});
    connect(enableButton,&QPushButton::clicked,this,[this,installedId] {
        if(!controller||!installed)return;const auto id=installedId();QString error;
        if(!controller->setEnabled(id,!installed->plugin(id).enabled,&error))status->setText(error);
    });
    connect(reloadButton,&QPushButton::clicked,this,[this,installedId]{if(controller)controller->reload(installedId());});
    connect(uninstallButton,&QPushButton::clicked,this,[this,installedId] {
        if(!controller||!installed)return;const auto id=installedId();const auto plugin=installed->plugin(id);
        PluginLifecycleDialog confirm(this,"uninstallPluginReview",tr("Uninstall Plugin"),tr("Its commands and preview panels will close. The catalog entry remains available while its source is registered."),tr("Uninstall"),true);
        PluginReview::identity(confirm,plugin.package.metadata);
        auto *removeData=new QCheckBox(tr("Also delete this plugin’s data and settings"),&confirm);removeData->setObjectName("deletePluginData");confirm.content->addWidget(removeData);
        confirm.paragraph(tr("Data and settings are kept by default, so a later reinstall can use them."),true);confirm.finish();
        if(confirm.exec()==QDialog::Accepted)controller->uninstall(id,!removeData->isChecked());
    });
    connect(inspectInstalledButton,&QPushButton::clicked,this,&PluginManagerDialog::updateInstalledDetails);
    connect(overviewButton,&QPushButton::clicked,this,&PluginManagerDialog::updateInstalledDetails);
    connect(logsButton,&QPushButton::clicked,this,[this,installedId] {
        if(!controller)return;const auto id=installedId();QDialog dialog(this);dialog.setWindowTitle(tr("Plugin Logs"));dialog.resize(720,440);auto *layout=new QVBoxLayout(&dialog);auto *text=new QPlainTextEdit(&dialog);text->setObjectName("pluginLogText");text->setReadOnly(true);text->setPlainText(controller->pluginLog(id));layout->addWidget(text);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(controller,&PluginPackages::PluginController::logsChanged,&dialog,[this,text,id](const QString &changed){if(changed!=id)return;auto *scroll=text->verticalScrollBar();const int position=scroll->value();const bool atEnd=position==scroll->maximum();text->setPlainText(controller->pluginLog(id));scroll->setValue(atEnd?scroll->maximum():position);});NativeAppearance::polishDialog(&dialog,tr("Plugin Logs"));text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));dialog.exec();
    });
    connect(registerButton, &QPushButton::clicked, this, &PluginManagerDialog::registerResources);
    connect(refreshButton, &QPushButton::clicked, this, &PluginManagerDialog::refreshSource);
    refreshCatalogButton->setToolTip(tr("Refresh all registered sources."));
    connect(refreshCatalogButton,&QPushButton::clicked,this,&PluginManagerDialog::refreshCatalog);
    connect(removeButton, &QPushButton::clicked, this, &PluginManagerDialog::removeSource);
    connect(inspectResourceButton, &QPushButton::clicked, this, [this] {
        const QString id = resourceList->currentItem() ? resourceList->currentItem()->data(Qt::UserRole).toString() : QString();
        for (const auto &resource : resources) if (resource.id == id) { inspect(resource.metadata); break; }
    });
    connect(inspectSourceButton, &QPushButton::clicked, this, [this] {
        const QString key = sourceList->currentItem() ? sourceList->currentItem()->data(Qt::UserRole).toString() : QString();
        for (const auto &source : sources) if (source.key() == key) {auto metadata=source.metadata;metadata.insert("registration",QJsonObject{{"origin",source.repository},{"localPath",source.localPath},{"ref",source.ref},{"commit",source.commit},{"sha256",source.digest},{"checkedAt",source.checkedAt.toString(Qt::ISODate)}});inspect(metadata);break;}
    });
    connect(resourceList, &QListWidget::currentRowChanged, this, &PluginManagerDialog::updateResourceDetails);
    connect(installedList,&QListWidget::currentRowChanged,this,&PluginManagerDialog::updateInstalledDetails);
    connect(sourceList, &QListWidget::currentRowChanged, this, &PluginManagerDialog::updateSourceDetails);
    connect(search, &QLineEdit::textChanged, this, &PluginManagerDialog::reload);
    connect(tabs, &QTabWidget::currentChanged, this, &PluginManagerDialog::updateViewContext);
    connect(registry, &RegisteredResourceRegistry::changed, this, &PluginManagerDialog::reload);
    if(installed)connect(installed,&PluginPackages::InstalledPluginRegistry::changed,this,&PluginManagerDialog::reload);
    if(controller) {
        connect(controller,&PluginPackages::PluginController::statusChanged,this,[this]{reload();if(installed&&!resolver->isBusy()&&(!installer||!installer->isBusy())){int enabled=0,running=0;const auto plugins=installed->plugins();for(const auto &plugin:plugins){enabled+=plugin.enabled;running+=controller->pluginState(plugin.id())=="active";}status->setText(tr("%1 installed · %2 enabled · %3 running").arg(plugins.size()).arg(enabled).arg(running));}});
        connect(controller,&PluginPackages::PluginController::logsChanged,this,[this](const QString &id){if(inspectInstalledButton->isChecked()&&installedList->currentItem()&&installedList->currentItem()->data(Qt::UserRole).toString()==id)updateInstalledDetails();});
        connect(controller,&PluginPackages::PluginController::operationFinished,this,[this](const QString &id,const QString &error){status->setText(!error.isEmpty()?error:installed&&installed->plugin(id).id().isEmpty()?tr("Plugin uninstalled. Its registered catalog entry remains in Discover."):tr("Plugin operation completed. Enablement and data were preserved."));pendingReleaseUpdate=false;reload();});
    }
    if(installer) {
        connect(installer,&PluginPackages::InstallationService::busyChanged,this,[this]{operationProgress->setRange(0,0);updateBusyState();});
        connect(installer,&PluginPackages::InstallationService::progress,this,[this](qint64 bytes){status->setText(tr("Validating and staging package: %1 KiB").arg(bytes/1024));});
        connect(installer,&PluginPackages::InstallationService::downloadProgress,this,[this](qint64 received,qint64 total){operationProgress->setRange(0,total>0?100:0);if(total>0)operationProgress->setValue(int(100.0*received/total));status->setText(tr("Downloading plugin: %1 / %2 KiB").arg(received/1024).arg(total/1024));});
        connect(installer,&PluginPackages::InstallationService::releasesAvailable,this,&PluginManagerDialog::reviewReleases);
        connect(installer,&PluginPackages::InstallationService::finished,this,[this](const QString &,const QString &error){
            status->setText(error.isEmpty()?(pendingReleaseUpdate?tr("Update package prepared. Enablement is preserved while the update is validated."):tr("Plugin installed and disabled. Enable it when you are ready.")):error);reload();
            if(error.isEmpty())tabs->setCurrentIndex(0);
            if(isVisible()&&QApplication::applicationState()==Qt::ApplicationActive){raise();activateWindow();}
        });
    }
    connect(resolver, &SourceEntryResolver::resolved, this, &PluginManagerDialog::finishLookup);
    connect(resolver, &SourceEntryResolver::failed, this, [this](const QString &error) {
        if(catalogRefreshing){++catalogFailures;QTimer::singleShot(0,this,&PluginManagerDialog::advanceCatalogRefresh);return;}
        checkingUpdateId.clear();status->setText(tr("%1 Existing registered resources have been kept.").arg(error));
        updateBusyState();
    });
    connect(resolver, &SourceEntryResolver::cancelled, this, [this] {
        catalogQueue.clear();catalogRefreshing=false;
        checkingUpdateId.clear();status->setText(tr("Lookup cancelled. Existing registered resources have been kept."));
        updateBusyState();
    });
    ready=true;
    reload();
    if(installed&&!installed->plugins().isEmpty())tabs->setCurrentIndex(0);
    updateViewContext();
    if (!registry->loadError().isEmpty()) status->setText(registry->loadError());
}

void PluginManagerDialog::updateViewContext() {
    const auto index=tabs->currentIndex();
    search->setPlaceholderText(index==0?tr("Search installed plugins"):index==1?tr("Search available plugins"):tr("Search sources"));
    viewContext->setText(index==0?tr("Installed · manage plugins"):index==1?tr("Discover · registered resources"):tr("Sources · registered repositories"));
}

void PluginManagerDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if(event->type()!=QEvent::PaletteChange||refreshingAppearance)return;
    const auto requestedPalette=palette();const auto sheet=managerStyle(requestedPalette);
    // Repolishing emits palette changes too; only a changed appearance needs
    // another stylesheet, rather than repainting on every inherited event.
    if(styleSheet()==sheet)return;
    refreshingAppearance=true;
    setStyleSheet(sheet);setPalette(requestedPalette);
    refreshingAppearance=false;
    if(ready){findChild<QLabel*>("managerIdentityIcon")->setPixmap(NativeAppearance::brandIcon(":/icons/plugins.svg",palette()).pixmap(24,24));reload();}
}

void PluginManagerDialog::reload()
{
    const QString selectedId = resourceList->currentItem() ? resourceList->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString selectedSource = sourceList->currentItem() ? sourceList->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString selectedInstalled=installedList->currentItem()?installedList->currentItem()->data(Qt::UserRole).toString():QString();
    resources = registry->resources();
    sources = registry->sources();
    const QSignalBlocker resourceSignals(resourceList),sourceSignals(sourceList),installedSignals(installedList);
    resourceList->clear();
    sourceList->clear();
    installedList->clear();
    const QString query = search->text().trimmed();
    if(installed)for(const auto &plugin:installed->plugins()) {
        const auto metadata=plugin.package.metadata;const auto name=metadata["name"].toString();
        if(!query.isEmpty()&&!(name+plugin.id()+metadata["description"].toString()).contains(query,Qt::CaseInsensitive))continue;
        auto *item=new QListWidgetItem(resourceIcon(metadata,palette()),name,installedList);item->setData(Qt::UserRole,plugin.id());
        item->setData(Qt::UserRole+1,metadata["version"].toString()+" · "+(plugin.enabled?tr("Enabled"):tr("Disabled"))+" · "+processLabel(controller?controller->pluginState(plugin.id()):QString()));item->setData(Qt::UserRole+2,metadata["description"].toString());
        if(plugin.id()==selectedInstalled)installedList->setCurrentItem(item);
    }
    for (const auto &resource : resources) {
        const QString name = resource.metadata.value(QStringLiteral("name")).toString();
        const QString description = resource.metadata.value(QStringLiteral("description")).toString();
        if (!query.isEmpty() && !(name + resource.id + description).contains(query, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(resourceIcon(resource.metadata,palette()), name, resourceList);
        item->setData(Qt::UserRole, resource.id);
        const auto package=installed?installed->plugin(resource.id):PluginPackages::InstalledPlugin();
        item->setData(Qt::UserRole+1,package.id().isEmpty()?tr("Available · %1").arg(resource.metadata["version"].toString()):tr("Installed · %1").arg(package.package.metadata["version"].toString()));item->setData(Qt::UserRole+2,description);
        item->setToolTip(description);
        if (resource.id == selectedId) resourceList->setCurrentItem(item);
    }
    for (const auto &source : sources) {
        if(!query.isEmpty()&&!(source.metadata["name"].toString()+source.repository+source.localPath).contains(query,Qt::CaseInsensitive))continue;
        auto *item = new QListWidgetItem(QIcon(":/icons/plugins.svg"),source.metadata["name"].toString(),sourceList);
        item->setData(Qt::UserRole, source.key());
        item->setData(Qt::UserRole+1,(source.isMarketplace()?tr("Marketplace"):tr("Single plugin"))+" · "+tr("%1 resources").arg(source.resources.size()));item->setData(Qt::UserRole+2,tr("Metadata valid · %1").arg(source.checkedAt.toLocalTime().toString("MMM d, HH:mm")));
        if (source.key() == selectedSource) sourceList->setCurrentItem(item);
    }
    if (!resourceList->currentItem() && resourceList->count()) resourceList->setCurrentRow(0);
    if (!sourceList->currentItem() && sourceList->count()) sourceList->setCurrentRow(0);
    if(!installedList->currentItem()&&installedList->count())installedList->setCurrentRow(0);
    tabs->setTabText(0,tr("Installed (%1)").arg(installed?installed->plugins().size():0));
    tabs->setTabText(1, tr("Discover (%1)").arg(resources.size()));
    tabs->setTabText(2, tr("Sources (%1)").arg(sources.size()));
    listContexts[0]->setText(tr("%1 installed plugins").arg(installed?installed->plugins().size():0));listContexts[1]->setText(tr("%1 registered resources").arg(resources.size()));listContexts[2]->setText(tr("Registered resource sources"));
    if(status->text().isEmpty())status->setText(tr("%1 sources · %2 available · %3 installed").arg(sources.size()).arg(resources.size()).arg(installed?installed->plugins().size():0));
    updateResourceDetails();
    updateSourceDetails();
    updateInstalledDetails();
}

void PluginManagerDialog::updateResourceDetails()
{
    const QString id = resourceList->currentItem() ? resourceList->currentItem()->data(Qt::UserRole).toString() : QString();
    for (const auto &resource : resources) {
        if (resource.id != id) continue;
        QStringList names;
        for (const auto &source : sources) if (resource.sourceIds.contains(source.key())) names.append(source.metadata.value(QStringLiteral("name")).toString());
        resourcePane->setIdentity(resource.metadata["name"].toString(),tr("Version %1 · %2").arg(resource.metadata["version"].toString(),resource.repository.startsWith("file:")?tr("Local plugin"):tr("Registered plugin")),resourceIcon(resource.metadata,palette()));
        const auto package=installed?installed->plugin(id):PluginPackages::InstalledPlugin();
        resourcePane->setNotice(package.id().isEmpty()?tr("Available to install"):tr("Installed"),package.id().isEmpty()?tr("From %1. Install when you are ready.").arg(names.join(", ")):tr("Version %1 is installed. Manage its enablement, updates and removal in Installed.").arg(package.package.metadata["version"].toString()));
        resourceDetails->setHtml(QStringLiteral("<p>%1</p><table width='100%' cellspacing='0'>").arg(field(resource.metadata,"description"))
            +paragraph(tr("Registered source"),names.join(", "))+paragraph(tr("Plugin origin"),displayOrigin(resource.repository))
            +paragraph(tr("Plugin ID"),resource.id)+paragraph(tr("Version"),resource.metadata["version"].toString())+"</table><p class='caption'>"
            +(package.id().isEmpty()?tr("Installing adds this plugin to Installed. Enable it when you are ready."):tr("Catalog registration and installation are independent. Removing a source keeps this installed plugin.")).toHtmlEscaped()+"</p>");
        resourceDetails->setToolTip(resource.repository);
        updateBusyState();
        return;
    }
    resourcePane->empty(resources.isEmpty()
        ? (!registry->resources().isEmpty() ? tr("All registered plugins are installed. Open Installed to manage them.")
            : sources.isEmpty() ? tr("No resources are registered. Register a plugin or marketplace repository to begin.")
                             : tr("The registered sources contain no plugin resources. Refresh a source to check for new resources."))
        : tr("No resources match your search."));
    updateBusyState();
}

void PluginManagerDialog::updateSourceDetails()
{
    const QString key = sourceList->currentItem() ? sourceList->currentItem()->data(Qt::UserRole).toString() : QString();
    for (const auto &source : sources) {
        if (source.key() != key) continue;
        sourcePane->setIdentity(source.metadata["name"].toString(),source.metadata["description"].toString(),{});
        sourceDetails->setHtml(QStringLiteral("<table width='100%' cellspacing='0'>")
            +paragraph(source.isLocal()?tr("Local metadata"):tr("Repository"),source.isLocal()?QFileInfo(source.localPath).dir().dirName()+"/"+QFileInfo(source.localPath).fileName():displayOrigin(source.repository))
            +paragraph(tr("Type"),source.isMarketplace()?tr("Marketplace · metadata v1"):tr("Single plugin · metadata v1"))
            +paragraph(tr("Registered resources"),QString::number(source.resources.size()))
            +(source.isLocal()?QString():paragraph(tr("Tracking"),(source.ref.isEmpty()?tr("Default branch"):source.ref)+" · "+source.commit.left(7)))
            +paragraph(tr("Last refresh"),source.checkedAt.toLocalTime().toString("MMM d, yyyy · HH:mm"))+"</table>");
        sourceDetails->setToolTip(source.isLocal()?source.localPath:source.repository);
        sourcePane->setNotice(tr("Registration and installation are separate."),tr("Removing this source keeps installed plugins, settings and data. Their original release repositories stay the same."));
        updateBusyState();
        return;
    }
    sourcePane->empty(sources.isEmpty()?tr("No sources are registered. Register a plugin or marketplace to begin."):tr("No sources match your search."));
    updateBusyState();
}

void PluginManagerDialog::updateBusyState()
{
    const bool busy = catalogRefreshing||resolver->isBusy()||(installer&&installer->isBusy());
    const bool writable = registry->loadError().isEmpty();
    registerButton->setEnabled(!busy && writable);
    refreshButton->setEnabled(!busy && writable && sourceList->currentItem());
    refreshCatalogButton->setEnabled(!busy&&writable&&!sources.isEmpty());
    removeButton->setEnabled(!busy && writable && sourceList->currentItem());
    inspectResourceButton->setEnabled(resourceList->currentItem());
    inspectSourceButton->setEnabled(sourceList->currentItem());
    installButton->setEnabled(!busy&&installer&&installed&&installed->loadError().isEmpty()&&resourceList->currentItem());
    localInstallButton->setEnabled(installButton->isEnabled());
    if (resourceList->currentItem()) {
        const auto id = resourceList->currentItem()->data(Qt::UserRole).toString();
        for (const auto &resource : resources) if (resource.id == id) {const bool local=resource.repository.startsWith("file:");installButton->setVisible(!local);localInstallButton->setText(local?tr("Install Local Package…"):tr("Local Package…"));localInstallButton->setMinimumWidth(local?172:0);localInstallButton->setToolTip(tr("Install a built local plugin package."));setPrimary(localInstallButton,local);if(local)installButton->setEnabled(false);}
    }
    else {installButton->hide();localInstallButton->hide();}
    if(resourceList->currentItem())localInstallButton->show();
    const bool catalogInstalled=installed&&resourceList->currentItem()&&!installed->plugin(resourceList->currentItem()->data(Qt::UserRole).toString()).id().isEmpty();
    manageResourceButton->setVisible(catalogInstalled);manageResourceButton->setEnabled(catalogInstalled&&!busy);
    if(catalogInstalled){installButton->hide();localInstallButton->hide();}
    operationProgress->setVisible(busy);
    cancelButton->setVisible(busy);
    updateInstalledDetails();
}

void PluginManagerDialog::updateInstalledDetails()
{
    const auto id=installedList->currentItem()?installedList->currentItem()->data(Qt::UserRole).toString():QString();
    const auto plugin=installed?installed->plugin(id):PluginPackages::InstalledPlugin();
    const bool selected=!plugin.id().isEmpty();
    const auto state=controller?controller->pluginState(id):QString();
    const bool stopping=state=="stopping"||state=="updating"||catalogRefreshing||resolver->isBusy()||(installer&&installer->isBusy());
    const auto selectedCommand=commandChoice->currentData().toString();
    commandChoice->clear();
    for(const auto &item:plugin.package.metadata.value("contributes").toObject().value("commands").toArray()) {
        const auto command=item.toObject();commandChoice->addItem(command.value("title").toString(),command.value("id"));
    }
    const int previousIndex=commandChoice->findData(selectedCommand);if(previousIndex>=0)commandChoice->setCurrentIndex(previousIndex);
    commandChoice->setVisible(selected&&commandChoice->count()>1);
    runCommandButton->setText(commandChoice->count()>0?commandChoice->currentText():tr("Open Plugin"));
    commandChoice->setEnabled(selected&&controller&&plugin.enabled&&!stopping&&commandChoice->count()>0);
    runCommandButton->setEnabled(commandChoice->isEnabled());
    enableButton->setEnabled(selected&&controller&&!stopping);enableButton->setText(plugin.enabled?tr("Disable"):tr("Enable"));
    setPrimary(enableButton,selected&&!plugin.enabled);setPrimary(runCommandButton,selected&&plugin.enabled);
    reloadButton->setEnabled(selected&&controller&&plugin.enabled&&!stopping);
    uninstallButton->setEnabled(selected&&controller&&!stopping);logsButton->setEnabled(selected&&controller);inspectInstalledButton->setEnabled(selected);
    bool newer=false;
    for(const auto &resource:registry->resources())if(resource.id==id&&resource.repository==plugin.package.repository) {
        PluginPackages::SemVer current,next;
        newer=PluginPackages::SemVer::parse(plugin.package.metadata.value("version").toString(),&current)
            &&PluginPackages::SemVer::parse(resource.metadata.value("version").toString(),&next)&&next.compare(current)>0;
    }
    const bool remote=plugin.package.repository.startsWith("https://github.com/");
    updateButton->setText(newer?tr("Update…"):tr("Check Updates…"));
    updateButton->setEnabled(selected&&controller&&installer&&(newer||remote)&&!plugin.updatePending&&!stopping);
    updateButton->setToolTip(plugin.validationDeferred?tr("Use this version once or roll it back before another update."):(newer?tr("Install the newer registered version."):tr("Check this plugin’s original repository for a newer compatible version.")));
    rollbackButton->setEnabled(selected&&controller&&plugin.previous.has_value()&&!stopping);
    for(auto *button:{runCommandButton,enableButton,updateButton,uninstallButton,overviewButton,inspectInstalledButton,logsButton,moreButton})button->setVisible(selected);
    moreButton->setEnabled(selected);
    if(!selected){installedPane->empty(installed&&!installed->loadError().isEmpty()?installed->loadError():installed&&installed->plugins().size()?tr("No installed plugins match your search."):tr("No plugins are installed. Open Discover, select a registered resource, and choose Install."));return;}
    installedPane->setIdentity(plugin.package.metadata["name"].toString(),tr("%1 · %2").arg(plugin.package.metadata["version"].toString(),displayOrigin(plugin.package.repository)),resourceIcon(plugin.package.metadata,palette()));
    installedPane->states->show();installedPane->values[0]->setText(plugin.package.metadata["version"].toString());installedPane->values[1]->setText(plugin.enabled?tr("Enabled"):tr("Disabled"));installedPane->values[2]->setText(processLabel(state));installedPane->values[3]->setText(tr("%1 open").arg(controller?controller->pluginPanelCount(id):0));
    const auto failure=controller?controller->pluginFailureCategory(id):QString();
    installedPane->setNotice(failure.isEmpty()?QString():tr("Plugin needs attention"),controller?controller->pluginFailureMessage(id):QString());
    if(inspectInstalledButton->isChecked()) {
        const auto runtime=plugin.package.metadata["runtime"].toObject();QStringList access;for(const auto &value:plugin.package.metadata["capabilities"].toObject()["required"].toArray())access.append(value.toString());
        const auto logLines=controller?controller->pluginLog(id).trimmed().split('\n'):QStringList();const auto recent=logLines.mid(qMax(0,logLines.size()-3)).join('\n');
        installedDetails->setHtml(QStringLiteral("<table width='100%' cellspacing='0'>")+paragraph(tr("Plugin ID"),id)+paragraph(tr("Source"),displayOrigin(plugin.package.repository))+paragraph(tr("Runtime / entry"),runtime["kind"].toString()+" · "+runtime["entry"].toString())
            +(failure.isEmpty()?QString():paragraph(tr("Failure"),failure))+"</table><p><b>"+tr("Host API access").toHtmlEscaped()+"</b><br>"+access.join(", ").toHtmlEscaped()+"<br>"+tr("Local code also has your user account's OS permissions.").toHtmlEscaped()+"</p><p><b>"+tr("Recent activity").toHtmlEscaped()+"</b></p><p class='caption'>"+(recent.isEmpty()?tr("No runtime activity yet."):recent).toHtmlEscaped().replace('\n',"<br>")+"</p>");
        return;
    }
    installedDetails->setHtml(QStringLiteral("<p>%1</p><table width='100%' cellspacing='0'>").arg(field(plugin.package.metadata,"description"))
        +paragraph(tr("Source"),displayOrigin(plugin.package.repository))+paragraph(tr("Plugin ID"),id)
        +paragraph(tr("Host API access"),[&]{QStringList list;for(const auto &value:plugin.package.metadata["capabilities"].toObject()["required"].toArray())list.append(value.toString().section('@',0,0));return list.join(", ");}())
        +paragraph(tr("Update policy"),newer?tr("Newer version registered · update available"):tr("Check manually · same installation source"))
        +(failure.isEmpty()?QString():paragraph(tr("Failure"),failure))+"</table><p class='caption'>"
        +(plugin.validationDeferred?tr("This package was checked. Activation will be validated on first use; its previous version remains available."):tr("Inspect shows package metadata and access. Logs shows runtime activity.")).toHtmlEscaped()+"</p>");
}
void PluginManagerDialog::inspectInstalledMetadata() {
    if(!installed||!installedList->currentItem())return;
    const auto plugin=installed->plugin(installedList->currentItem()->data(Qt::UserRole).toString());auto metadata=plugin.package.metadata;
    metadata.insert("installation",QJsonObject{{"origin",plugin.package.repository},{"sha256",plugin.package.digest},{"installedAt",plugin.installedAt.toString(Qt::ISODate)},{"enabled",plugin.enabled},{"validationDeferred",plugin.validationDeferred}});
    if(controller)metadata.insert("runtimeStatus",QJsonObject{{"state",controller->pluginState(plugin.id())},{"failure",controller->pluginFailureCategory(plugin.id())},{"message",controller->pluginFailureMessage(plugin.id())}});
    inspect(metadata);
}
void PluginManagerDialog::installLocalPackage()
{
    if(!installer||!resourceList->currentItem())return;
    const auto id=resourceList->currentItem()->data(Qt::UserRole).toString();
    for(const auto &resource:resources)if(resource.id==id){
        const QUrl origin(resource.repository);
        const auto path=QFileDialog::getExistingDirectory(this,tr("Select Built Plugin Package"),origin.isLocalFile()?QFileInfo(origin.toLocalFile()).absolutePath():QString());if(path.isEmpty())return;
        PluginLifecycleDialog review(this,"localInstallReview",tr("Install Plugin"),tr("The package will be checked and installed disabled. Enable it when you are ready."),tr("Install"));
        PluginReview::identity(review,resource.metadata);PluginReview::fact(review,tr("Package folder"),path);PluginReview::access(review,resource.metadata);review.finish();
        if(review.exec()!=QDialog::Accepted)return;pendingReleaseUpdate=false;
        if(!installer->installDirectory(path,resource))status->setText(tr("An installation is already in progress or this plugin is already installed."));
        else status->setText(tr("Checking plugin package…"));return;
    }
}
void PluginManagerDialog::installGitHubPackage()
{
    if(!installer||!resourceList->currentItem())return;
    const auto id=resourceList->currentItem()->data(Qt::UserRole).toString();
    for(const auto &resource:resources)if(resource.id==id) {
        pendingReleaseId=id;pendingReleaseUpdate=false;pendingReleaseMetadata=resource.metadata;
        if(!installer->lookupReleases(resource)){pendingReleaseId.clear();status->setText(tr("Release lookup cannot start while another installation is in progress."));}
        else status->setText(tr("Looking up published plugin packages…"));
        return;
    }
}
void PluginManagerDialog::updatePlugin()
{
    if(!installer||!installed||!installedList->currentItem())return;
    const auto id=installedList->currentItem()->data(Qt::UserRole).toString();const auto plugin=installed->plugin(id);
    if(plugin.package.repository.startsWith("https://github.com/")){
        checkingUpdateId=id;QString ref;
        for(const auto &source:sources)if(source.repository==plugin.package.repository){ref=source.ref;if(ref.isEmpty())break;}
        status->setText(tr("Checking updates for %1…").arg(plugin.package.metadata["name"].toString()));resolver->resolve(plugin.package.repository,ref);updateBusyState();return;
    }
    for(const auto &resource:registry->resources())if(resource.id==id&&resource.repository==plugin.package.repository){
        if(resource.repository.startsWith("file:")){
            const auto path=QFileDialog::getExistingDirectory(this,tr("Select Built Plugin Update"));if(path.isEmpty())return;
            PluginLifecycleDialog review(this,"localUpdateReview",tr("Update Plugin"),tr("Your enabled setting and plugin data are preserved. Open the preview again after updating. Failed activation restores the previous version."),tr("Update"));
            PluginReview::identity(review,resource.metadata);PluginReview::fact(review,tr("Version"),plugin.package.metadata["version"].toString()+" → "+resource.metadata["version"].toString());PluginReview::access(review,resource.metadata);review.finish();
            if(review.exec()==QDialog::Accepted){pendingReleaseUpdate=true;if(!installer->updateDirectory(path,resource))status->setText(tr("Another plugin operation is in progress."));}return;
        }
        pendingReleaseId=id;pendingReleaseUpdate=true;pendingReleaseMetadata=resource.metadata;
        if(!installer->lookupReleases(resource,true)){pendingReleaseId.clear();status->setText(tr("Another plugin operation is in progress."));}return;
    }
}
void PluginManagerDialog::reviewReleases(const QString &id,const QVector<PluginPackages::ReleaseAsset> &assets)
{
    if(pendingReleaseId!=id)return;pendingReleaseId.clear();const bool updating=pendingReleaseUpdate;
    if(assets.isEmpty()){status->setText(tr("No package matches this catalog version and platform. Refresh the source or check its published releases."));return;}
    auto metadata=pendingReleaseMetadata;if(metadata.isEmpty())for(const auto &resource:registry->resources())if(resource.id==id&&resource.repository==assets[0].repository)metadata=resource.metadata;
    PluginLifecycleDialog review(this,updating?"releaseUpdateReview":"releaseInstallReview",updating?tr("Update Plugin"):tr("Install Plugin"),updating?tr("Your enabled setting and plugin data are preserved. Open the preview again after updating. Failed activation restores the previous version."):tr("The package will be checked and installed disabled. Enable it when you are ready."),updating?tr("Update"):tr("Install"));
    PluginReview::identity(review,metadata);
    if(updating)PluginReview::fact(review,tr("Version"),installed->plugin(id).package.metadata["version"].toString()+" → "+metadata["version"].toString());
    PluginReview::fact(review,tr("Repository"),assets[0].repository);
    auto *choice=new QComboBox(&review);choice->setObjectName("releaseAssetChoice");choice->setAccessibleName(tr("Published package"));for(const auto &asset:assets)choice->addItem(asset.tag+" · "+asset.name+" · "+tr("%1 KiB").arg(asset.size/1024));review.content->addWidget(choice);
    PluginReview::access(review,metadata);review.paragraph(tr("SHA-256 and package contents will be checked before installation."),true);review.finish();review.resize(680,620);
    if(review.exec()!=QDialog::Accepted){status->setText(updating?tr("Update cancelled. The installed version was preserved."):tr("Installation cancelled. The plugin remains available in Discover."));return;}
    if(!installer->installRelease(assets[choice->currentIndex()]))status->setText(tr("The package cannot be installed now. Refresh its selection and try again."));
}

void PluginManagerDialog::registerResources()
{
    PluginLifecycleDialog entry(this,"registerSourceEntry",tr("Add Plugin Source"),tr("Add a GitHub repository or local metadata file. You will review its plugins before registering the source."),tr("Check Repository"));
    auto *layout=entry.content;
    const auto field=[&](const QString &name,QWidget *input){auto *block=new QWidget(&entry);auto *row=new QVBoxLayout(block);row->setContentsMargins(0,0,0,0);row->setSpacing(8);auto *label=new QLabel(name,block);label->setBuddy(input);row->addWidget(label);row->addWidget(input);layout->addWidget(block);return block;};
    auto *kind = new QComboBox(&entry);
    kind->setObjectName(QStringLiteral("sourceKindInput"));
    kind->addItems({tr("GitHub repository"), tr("Local metadata file")});
    field(tr("Source"),kind);
    auto *repository = new QLineEdit(&entry);
    repository->setObjectName(QStringLiteral("repositoryInput"));
    repository->setPlaceholderText(QStringLiteral("https://github.com/owner/repository"));
    auto *ref = new QLineEdit(&entry);
    ref->setObjectName(QStringLiteral("sourceRefInput"));
    ref->setPlaceholderText(tr("Default branch"));
    auto *repositoryField=field(tr("GitHub repository"),repository);
    auto *refField=field(tr("Git ref (optional)"),ref);
    auto *localRow = new QWidget(&entry);
    auto *localLayout = new QHBoxLayout(localRow);
    localLayout->setContentsMargins(0, 0, 0, 0);
    auto *localPath = new QLineEdit(localRow);
    localPath->setObjectName(QStringLiteral("localMetadataInput"));
    localPath->setPlaceholderText(tr("Path to notepadnext-plugin.json"));
    auto *browse = new QPushButton(tr("Browse…"), localRow);
    connect(browse, &QPushButton::clicked, &entry, [&] {
        const auto path = QFileDialog::getOpenFileName(&entry, tr("Select Plugin Metadata"), localPath->text(), tr("JSON files (*.json)"));
        if (!path.isEmpty()) localPath->setText(path);
    });
    localLayout->addWidget(localPath);
    localLayout->addWidget(browse);
    auto *localField=field(tr("Metadata file"),localRow);localField->hide();
    auto *error=new QLabel(&entry);error->setObjectName("sourceInputError");error->setTextFormat(Qt::PlainText);error->setWordWrap(true);layout->addWidget(error);
    auto *buttons=entry.buttons;disconnect(buttons,&QDialogButtonBox::accepted,&entry,&QDialog::accept);
    connect(kind, &QComboBox::currentIndexChanged, &entry, [&](int index) {
        repositoryField->setVisible(index == 0);
        refField->setVisible(index == 0);
        localField->setVisible(index == 1);
        auto *confirm=buttons->button(QDialogButtonBox::Ok);confirm->setText(index == 0 ? tr("Check Repository") : tr("Check Metadata"));confirm->setAccessibleName(confirm->text());
        error->clear();
    });
    entry.finish();
    connect(buttons, &QDialogButtonBox::rejected, &entry, &QDialog::reject);
    SourceSnapshot localSnapshot;
    connect(buttons, &QDialogButtonBox::accepted, &entry, [&] {
        QString canonical, validationError;
        if (kind->currentIndex() == 1) {
            if (!PluginMetadata::readLocalFile(localPath->text(), &localSnapshot, &validationError)) { error->setText(validationError); return; }
            entry.accept();
            return;
        }
        if (!PluginMetadata::normalizeRepository(repository->text(), &canonical, &validationError)) { error->setText(validationError); return; }
        if (ref->text().size() > 192 || ref->text().contains(QChar::Null)) { error->setText(tr("Invalid Git ref.")); return; }
        repository->setText(canonical);
        entry.accept();
    });
    entry.resize(680,500);
    if (entry.exec() != QDialog::Accepted) return;
    refreshing = false;
    if (kind->currentIndex() == 1) { finishLookup(localSnapshot); return; }
    status->setText(tr("Reading repository metadata…"));
    resolver->resolve(repository->text(), ref->text());
    updateBusyState();
}

void PluginManagerDialog::refreshSource()
{
    const QString key = sourceList->currentItem() ? sourceList->currentItem()->data(Qt::UserRole).toString() : QString();
    for (const auto &source : sources) {
        if (source.key() != key) continue;
        refreshing = true;
        status->setText(tr("Refreshing source metadata…"));
        if (source.isLocal()) {
            SourceSnapshot snapshot;
            QString error;
            if (!PluginMetadata::readLocalFile(source.localPath, &snapshot, &error)) status->setText(error);
            else finishLookup(snapshot);
            updateBusyState();
            return;
        }
        resolver->resolve(source.repository, source.ref);
        updateBusyState();
        return;
    }
}

void PluginManagerDialog::refreshCatalog()
{
    if(resolver->isBusy()||(installer&&installer->isBusy())||catalogRefreshing)return;
    catalogQueue=registry->sources();catalogTotal=catalogQueue.size();catalogFailures=0;
    catalogRefreshing=true;advanceCatalogRefresh();
}

void PluginManagerDialog::advanceCatalogRefresh()
{
    if(!catalogRefreshing)return;
    if(catalogQueue.isEmpty()){
        catalogRefreshing=false;refreshing=false;
        status->setText(tr("Catalog refreshed: %1 sources updated, %2 unavailable. Previous resources from unavailable sources were kept.").arg(catalogTotal-catalogFailures).arg(catalogFailures));updateBusyState();return;
    }
    const auto source=catalogQueue.takeFirst();refreshing=true;
    status->setText(tr("Refreshing source %1 of %2…").arg(catalogTotal-catalogQueue.size()).arg(catalogTotal));
    if(source.isLocal()){
        SourceSnapshot snapshot;QString error;
        if(PluginMetadata::readLocalFile(source.localPath,&snapshot,&error))finishLookup(snapshot);
        else{++catalogFailures;QTimer::singleShot(0,this,&PluginManagerDialog::advanceCatalogRefresh);}
    }else resolver->resolve(source.repository,source.ref);
    updateBusyState();
}

void PluginManagerDialog::finishLookup(const SourceSnapshot &snapshot)
{
    if(catalogRefreshing){QString error;if(!registry->registerSource(snapshot,&error))++catalogFailures;QTimer::singleShot(0,this,&PluginManagerDialog::advanceCatalogRefresh);return;}
    if(!checkingUpdateId.isEmpty()){
        const auto id=checkingUpdateId;checkingUpdateId.clear();const auto plugin=installed->plugin(id);
        for(const auto &resource:snapshot.resources)if(resource.id==id&&resource.repository==plugin.package.repository){
            PluginPackages::SemVer current,next;
            if(!PluginPackages::SemVer::parse(plugin.package.metadata["version"].toString(),&current)||!PluginPackages::SemVer::parse(resource.metadata["version"].toString(),&next)){status->setText(tr("The source has an invalid version."));updateBusyState();return;}
            for(const auto &source:sources)if(source.key()==snapshot.key()){QString error;if(!registry->registerSource(snapshot,&error)){status->setText(error);updateBusyState();return;}break;}
            if(next.compare(current)<=0){PluginLifecycleDialog review(this,"pluginUpToDate",tr("Plugin Is Up to Date"),tr("No newer version is available from its current source."),tr("Done"));PluginReview::identity(review,plugin.package.metadata);review.buttons->button(QDialogButtonBox::Cancel)->hide();review.finish();review.exec();status->setText(tr("%1 is up to date.").arg(plugin.package.metadata["name"].toString()));updateBusyState();return;}
            pendingReleaseId=id;pendingReleaseUpdate=true;pendingReleaseMetadata=resource.metadata;
            if(!installer->lookupReleases(resource,true)){pendingReleaseId.clear();status->setText(tr("Another plugin operation is in progress."));}updateBusyState();return;
        }
        status->setText(tr("The source no longer lists this installed plugin. Its installed version was kept."));updateBusyState();return;
    }
    updateBusyState();
    if(!refreshing){
        PluginLifecycleDialog review(this,"registerSourceReview",tr("Register Plugin Source"),tr("Registering makes plugins available in Discover. Installation and execution are separate choices."),tr("Register Source"));
        PluginReview::identity(review,snapshot.metadata);PluginReview::fact(review,tr("Origin"),snapshot.isLocal()?snapshot.localPath:snapshot.repository);
        PluginReview::fact(review,tr("Source type"),snapshot.isMarketplace()?tr("Marketplace"):tr("Single plugin"));
        PluginReview::fact(review,tr("Available plugins"),QString::number(snapshot.resources.size()));
        QStringList names;for(int i=0;i<qMin(8,snapshot.resources.size());++i){const auto &r=snapshot.resources[i];names.append(r.metadata["name"].toString()+" · "+r.metadata["version"].toString());}if(snapshot.resources.size()>8)names.append(tr("%1 more plugins will be available in Discover").arg(snapshot.resources.size()-8));review.paragraph(names.join("\n"));review.finish();
        if(review.exec()!=QDialog::Accepted){status->setText(tr("Registration cancelled. Existing sources are unchanged."));return;}
    }
    QString error;if(!registry->registerSource(snapshot,&error)){status->setText(error);return;}
    if(!refreshing)tabs->setCurrentIndex(1);
    status->setText(refreshing?tr("Source refreshed. %1 plugins available.").arg(snapshot.resources.size()):tr("Source registered. Choose a plugin in Discover."));
}

void PluginManagerDialog::removeSource()
{
    const auto key=sourceList->currentItem()?sourceList->currentItem()->data(Qt::UserRole).toString():QString();
    for(const auto &source:sources)if(source.key()==key){
        PluginLifecycleDialog review(this,"removeSourceReview",tr("Remove Plugin Source"),tr("Plugins available only from this source leave Discover. Installed plugins, their data and settings are kept."),tr("Remove Source"),true);
        PluginReview::identity(review,source.metadata);PluginReview::fact(review,tr("Origin"),source.isLocal()?source.localPath:source.repository);review.finish();
        if(review.exec()!=QDialog::Accepted)return;
        QString error;if(!registry->removeSource(key,&error))status->setText(error);else status->setText(tr("Source removed. Installed plugins were kept."));return;
    }
}

void PluginManagerDialog::inspect(const QJsonObject &metadata)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Inspect Metadata"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Indented)));
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    NativeAppearance::polishDialog(&dialog,tr("Plugin Metadata"));text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));dialog.resize(680, 480);
    dialog.exec();
}
