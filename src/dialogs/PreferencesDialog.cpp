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


#include "PreferencesDialog.h"
#include "NotepadNextApplication.h"
#include "TranslationManager.h"
#include "ui_PreferencesDialog.h"
#include "ScintillaNext.h"
#include "NativeAppearance.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QPushButton>


PreferencesDialog::PreferencesDialog(ApplicationSettings *settings, QWidget *parent) :
    QDialog(parent, Qt::Tool),
    ui(new Ui::PreferencesDialog),
    settings(settings)
{
    ui->setupUi(this);
    setupMacLayout();

    QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
    QPixmap pixmap = icon.pixmap(QSize(16, 16));
    ui->labelAppRestartIcon->setPixmap(pixmap);
    ui->labelAppRestartIcon->hide();
    ui->labelAppRestart->hide();

    MapSettingToCheckBox(ui->checkBoxMenuBar, &ApplicationSettings::showMenuBar, &ApplicationSettings::setShowMenuBar, &ApplicationSettings::showMenuBarChanged);
    MapSettingToCheckBox(ui->checkBoxToolBar, &ApplicationSettings::showToolBar, &ApplicationSettings::setShowToolBar, &ApplicationSettings::showToolBarChanged);
    MapSettingToCheckBox(ui->checkBoxStatusBar, &ApplicationSettings::showStatusBar, &ApplicationSettings::setShowStatusBar, &ApplicationSettings::showStatusBarChanged);
    MapSettingToCheckBox(ui->checkBoxRecenterSearchDialog, &ApplicationSettings::centerSearchDialog, &ApplicationSettings::setCenterSearchDialog, &ApplicationSettings::centerSearchDialogChanged);

    MapSettingToGroupBox(ui->gbxRestorePreviousSession, &ApplicationSettings::restorePreviousSession, &ApplicationSettings::setRestorePreviousSession, &ApplicationSettings::restorePreviousSessionChanged);
    connect(ui->gbxRestorePreviousSession, &QGroupBox::toggled, this, [=, this](bool checked) {
        if (!checked) {
            ui->checkBoxUnsavedFiles->setChecked(false);
            ui->checkBoxRestoreTempFiles->setChecked(false);
        }
        else {
            QMessageBox::warning(this, tr("Warning"), tr("This feature is experimental and it should not be considered safe for critically important work. It may lead to possible data loss. Use at your own risk."));
        }
    });

    MapSettingToCheckBox(ui->checkBoxUnsavedFiles, &ApplicationSettings::restoreUnsavedFiles, &ApplicationSettings::setRestoreUnsavedFiles, &ApplicationSettings::restoreUnsavedFilesChanged);
    MapSettingToCheckBox(ui->checkBoxRestoreTempFiles, &ApplicationSettings::restoreTempFiles, &ApplicationSettings::setRestoreTempFiles, &ApplicationSettings::restoreTempFilesChanged);

    MapSettingToCheckBox(ui->checkBoxCombineSearchResults, &ApplicationSettings::combineSearchResults, &ApplicationSettings::setCombineSearchResults, &ApplicationSettings::combineSearchResultsChanged);

    populateTranslationComboBox();
    connect(ui->comboBoxTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=, this](int index) {
        settings->setTranslation(ui->comboBoxTranslation->itemData(index).toString());
        showApplicationRestartRequired();
    });

    MapSettingToCheckBox(ui->checkBoxExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosed, &ApplicationSettings::setExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosedChanged);

    ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    connect(ui->fcbDefaultFont, &QFontComboBox::currentFontChanged, this, [=, this](const QFont &f) {
        settings->setFontName(f.family());
    });
    connect(settings, &ApplicationSettings::fontNameChanged, this, [=, this](QString fontName){
        ui->fcbDefaultFont->setCurrentFont(QFont(fontName));
    });

    ui->spbDefaultFontSize->setValue(settings->fontSize());
    connect(ui->spbDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), settings, &ApplicationSettings::setFontSize);
    connect(settings, &ApplicationSettings::fontSizeChanged, ui->spbDefaultFontSize, &QSpinBox::setValue);

    ui->comboBoxLineEndings->addItem(tr("System Default"), QString(""));
    ui->comboBoxLineEndings->addItem(tr("Windows (CR LF)"), ScintillaNext::eolModeToString(SC_EOL_CRLF));
    ui->comboBoxLineEndings->addItem(tr("Linux (LF)"), ScintillaNext::eolModeToString(SC_EOL_LF));
    ui->comboBoxLineEndings->addItem(tr("Macintosh (CR)"), ScintillaNext::eolModeToString(SC_EOL_CR));

    // Select the current one
    int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
    ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);

    connect(ui->comboBoxLineEndings, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=, this](int index) {
        settings->setDefaultEOLMode(ui->comboBoxLineEndings->itemData(index).toString());
    });
    connect(settings, &ApplicationSettings::defaultEOLModeChanged, this, [=, this](QString defaultEOLMode) {
        int index = ui->comboBoxLineEndings->findData(defaultEOLMode);
        ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);
    });

    MapSettingToCheckBox(ui->checkBoxHighlightURLs, &ApplicationSettings::urlHighlighting, &ApplicationSettings::setURLHighlighting, &ApplicationSettings::urlHighlightingChanged);
    MapSettingToCheckBox(ui->checkBoxShowLineNumbers, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers, &ApplicationSettings::showLineNumbersChanged);
    MapSettingToCheckBox(ui->checkBoxAutoCompletion, &ApplicationSettings::autoCompletion, &ApplicationSettings::setAutoCompletion, &ApplicationSettings::autoCompletionChanged);

    QButtonGroup *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(ui->radioFollowCurrentDirectory, ApplicationSettings::FollowCurrentDocument);
    buttonGroup->addButton(ui->radioLastUsedDirectory, ApplicationSettings::RememberLastUsed);
    buttonGroup->addButton(ui->radioHardCoded, ApplicationSettings::HardCoded);

    connect(buttonGroup, &QButtonGroup::idClicked, this, [=, this](int id) {
        ApplicationSettings::DefaultDirectoryBehaviorEnum e = static_cast<ApplicationSettings::DefaultDirectoryBehaviorEnum>(id);
        settings->setDefaultDirectoryBehavior(e);
    });

    connect(ui->radioHardCoded, &QRadioButton::toggled, this, [=, this](bool checked){
        ui->btnSelectHardCodedPath->setEnabled(checked);
        ui->txtHardCodedPath->setEnabled(checked);
    });

    connect(ui->btnSelectHardCodedPath, &QToolButton::clicked, this, [=, this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Default Directory"));
        if (dir.isEmpty()) return; // user cancelled

        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    connect(ui->txtHardCodedPath, &QLineEdit::editingFinished, this, [=, this]() {
        QString dir = ui->txtHardCodedPath->text();
        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    if (auto b = buttonGroup->button(settings->defaultDirectoryBehavior())) {
        b->setChecked(true);
    }

    if (settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded) {
        ui->txtHardCodedPath->setText((QDir::toNativeSeparators(settings->defaultDirectory())));
    }
    else {
        ui->txtHardCodedPath->setText(QString());
    }
}

void PreferencesDialog::setupMacLayout()
{
#ifdef Q_OS_MACOS
    auto *holding=new QWidget(this);
    const QList<QWidget*> controls{
        ui->checkBoxMenuBar,ui->checkBoxToolBar,ui->checkBoxStatusBar,
        ui->gbxRestorePreviousSession,ui->checkBoxRecenterSearchDialog,
        ui->checkBoxCombineSearchResults,ui->comboBoxTranslation,
        ui->checkBoxExitOnLastTabClosed,ui->groupBox,ui->comboBoxLineEndings,
        ui->checkBoxHighlightURLs,ui->checkBoxShowLineNumbers,ui->checkBoxAutoCompletion,
        ui->groupBox_2,ui->labelAppRestartIcon,ui->labelAppRestart,ui->buttonBox
    };
    for(auto *control:controls)control->setParent(holding);
    delete ui->scrollArea;delete layout();
    auto *root=new QVBoxLayout(this);
    auto *body=new QHBoxLayout();body->setSpacing(24);root->addLayout(body,1);
    auto *categories=new QListWidget(this);categories->setObjectName("settingsCategories");
    categories->setAccessibleName(tr("Settings categories"));categories->setFixedWidth(176);categories->setIconSize(QSize(18,18));categories->setWordWrap(true);categories->setTextElideMode(Qt::ElideNone);
    categories->setTabKeyNavigation(false);
    body->addWidget(categories);
    auto *pages=new QStackedWidget(this);pages->setObjectName("settingsPages");body->addWidget(pages,1);
    const auto page=[&](const QString &name,const QString &symbol){
        auto *item=new QListWidgetItem(name,categories);item->setData(Qt::UserRole+1,symbol);
        auto *scroll=new QScrollArea(pages);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content=new QWidget(scroll);auto *layout=new QVBoxLayout(content);
        layout->setContentsMargins(0,0,8,0);layout->setSpacing(16);layout->setAlignment(Qt::AlignTop);
        auto *heading=new QLabel(name,content);heading->setProperty("dialogRole","section");layout->addWidget(heading);
        scroll->setWidget(content);pages->addWidget(scroll);return layout;
    };
    const auto group=[](QVBoxLayout *page,const QString &title){
        auto *box=new QGroupBox(title);auto *layout=new QVBoxLayout(box);layout->setSpacing(8);page->addWidget(box);return layout;
    };
    const auto field=[](QVBoxLayout *page,const QString &title,QWidget *input){
        auto *row=new QHBoxLayout();auto *label=new QLabel(title);label->setBuddy(input);
        row->addWidget(label);row->addWidget(input,1);page->addLayout(row);
    };
    auto *general=page(tr("General"),"gearshape");
    field(general,tr("Language"),ui->comboBoxTranslation);
    auto *appearance=group(general,tr("Window appearance"));
    appearance->addWidget(ui->checkBoxMenuBar);appearance->addWidget(ui->checkBoxToolBar);appearance->addWidget(ui->checkBoxStatusBar);
    auto *windows=group(general,tr("Windows"));windows->addWidget(ui->checkBoxExitOnLastTabClosed);
    auto *editor=page(tr("Editor"),"textformat");
    editor->addWidget(ui->groupBox);
    ui->label_2->setBuddy(ui->fcbDefaultFont);ui->label_3->setBuddy(ui->spbDefaultFontSize);
    auto *editing=group(editor,tr("Editing"));
    editing->addWidget(ui->checkBoxShowLineNumbers);editing->addWidget(ui->checkBoxHighlightURLs);editing->addWidget(ui->checkBoxAutoCompletion);
    auto *files=page(tr("Files"),"doc");
    field(files,tr("New document line endings"),ui->comboBoxLineEndings);
    files->addWidget(ui->groupBox_2);files->addWidget(ui->gbxRestorePreviousSession);
    ui->txtHardCodedPath->setAccessibleName(tr("Default directory path"));
    ui->radioHardCoded->setText(tr("Custom folder"));
    ui->btnSelectHardCodedPath->setProperty("nativeSymbol","folder");ui->btnSelectHardCodedPath->setText({});
    ui->btnSelectHardCodedPath->setAccessibleName(tr("Choose default directory"));ui->btnSelectHardCodedPath->setToolTip(tr("Choose default directory"));
    auto *search=page(tr("Search"),"magnifyingglass");auto *searchOptions=group(search,tr("Search behavior"));
    searchOptions->addWidget(ui->checkBoxRecenterSearchDialog);searchOptions->addWidget(ui->checkBoxCombineSearchResults);
    connect(categories,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);categories->setCurrentRow(0);
    auto *footer=new QHBoxLayout();footer->setSpacing(8);root->addLayout(footer);
    footer->addWidget(ui->labelAppRestartIcon);footer->addWidget(ui->labelAppRestart,1);footer->addWidget(ui->buttonBox);
    ui->labelAppRestart->setWordWrap(true);ui->labelAppRestart->setProperty("dialogRole","caption");
    auto *done=ui->buttonBox->button(QDialogButtonBox::Ok);done->setText(tr("Done"));
    delete holding;
    NativeAppearance::polishDialog(this,tr("Settings"),tr("Changes are saved automatically."));
    NativeAppearance::setDialogPrimary(this,done);
    const QList<QWidget*> order{categories,ui->comboBoxTranslation,ui->checkBoxMenuBar,ui->checkBoxToolBar,ui->checkBoxStatusBar,ui->checkBoxExitOnLastTabClosed,
        ui->fcbDefaultFont,ui->spbDefaultFontSize,ui->checkBoxShowLineNumbers,ui->checkBoxHighlightURLs,ui->checkBoxAutoCompletion,
        ui->comboBoxLineEndings,ui->radioFollowCurrentDirectory,ui->radioLastUsedDirectory,ui->radioHardCoded,ui->txtHardCodedPath,ui->btnSelectHardCodedPath,
        ui->gbxRestorePreviousSession,ui->checkBoxUnsavedFiles,ui->checkBoxRestoreTempFiles,ui->checkBoxRecenterSearchDialog,ui->checkBoxCombineSearchResults,done};
    for(int i=1;i<order.size();++i)QWidget::setTabOrder(order[i-1],order[i]);
    setMinimumSize(720,500);resize(840,590);categories->setFocus();
#endif
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

void PreferencesDialog::showApplicationRestartRequired() const
{
    ui->labelAppRestartIcon->show();
    ui->labelAppRestart->show();
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToCheckBox(QCheckBox *checkBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    checkBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, checkBox, &QCheckBox::setChecked);
    connect(checkBox, &QCheckBox::toggled, settings, setter);
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToGroupBox(QGroupBox *groupBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    groupBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, groupBox, &QGroupBox::setChecked);
    connect(groupBox, &QGroupBox::toggled, settings, setter);
}

void PreferencesDialog::populateTranslationComboBox()
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);

    // Add the system default at the top
    ui->comboBoxTranslation->addItem(tr("<System Default>"), QStringLiteral(""));

    // TODO: sort this list and keep the system default at the top
    for (const auto &localeName : app->getTranslationManager()->availableTranslations())
    {
        QLocale locale(localeName);
        const QString localeDisplay = TranslationManager::FormatLocaleTerritoryAndLanguage(locale);
        ui->comboBoxTranslation->addItem(localeDisplay, localeName);
    }

    // Select the current one
    int index = ui->comboBoxTranslation->findData(settings->translation());
    if (index != -1) {
        ui->comboBoxTranslation->setCurrentIndex(index);
    }
}
