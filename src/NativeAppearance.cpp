// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeAppearance.h"
#include "ScintillaNext.h"
#include <cmath>
#include <QAction>
#include <QApplication>
#include <QChildEvent>
#include <QEvent>
#include <QFontDatabase>
#include <QIconEngine>
#include <QImage>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <DockWidgetTab.h>

namespace NativeAppearance {
#ifdef Q_OS_MACOS
namespace {
const QHash<QString,QString> actionSymbols{
    {"actionNew","doc.badge.plus"},{"actionOpen","folder"},{"actionSave","square.and.arrow.down"},
    {"actionSaveAll","square.and.arrow.down.on.square"},{"actionClose","xmark"},{"actionCloseAll","xmark.square"},
    {"actionCut","scissors"},{"actionCopy","doc.on.doc"},{"actionPaste","doc.on.clipboard"},
    {"actionUndo","arrow.uturn.backward"},{"actionRedo","arrow.uturn.forward"},
    {"actionFind","magnifyingglass"},{"actionReplace","arrow.left.arrow.right"},
    {"actionZoomIn","plus.magnifyingglass"},{"actionZoomOut","minus.magnifyingglass"},
    {"actionWordWrap","arrow.turn.down.left"},{"actionShowAllCharacters","paragraphsign"},
    {"actionShowIndentGuide","line.3.horizontal"},{"actionMacroRecording","record.circle"},
    {"actionPlayback","play"},{"actionRunMacroMultipleTimes","repeat"},{"actionSaveCurrentRecordedMacro","square.and.arrow.down"},
    {"actionReload","arrow.clockwise"},{"actionMoveToTrash","trash"},{"actionPrint","printer"},
    {"actionPrintPreview","doc.text.magnifyingglass"},{"actionPreferences","gearshape"},
    {"actionShowInExplorer","folder"},{"actionOpenTerminalHere","terminal"},
    {"actionPluginManager","puzzlepiece.extension"}
};
void styleTab(ads::CDockWidgetTab *tab) {
    const auto state=tab->property("nativeDocumentState").toInt();
    tab->setIconSize(QSize(10,10));
    const auto palette=tab->window()->palette();
    if(state==2)tab->setIcon(symbolIcon("lock",QIcon(":/icons/readonly.png"),palette));
    else if(state==1)tab->setIcon(symbolIcon("circle.fill",QIcon(":/icons/unsaved.png"),palette));
    else tab->setIcon({});
    if(auto *button=tab->findChild<QPushButton*>("tabCloseButton")){
        button->setIcon(symbolIcon("xmark",QIcon(":/icons/cross.svg"),palette));button->setIconSize(QSize(12,12));
        button->setAccessibleName(QCoreApplication::translate("NativeAppearance","Close document"));
    }
}
class Chrome final:public QObject {
    QMainWindow *window;
    bool refreshing=false;
    void buttons(QToolBar *toolbar) {
        toolbar->setIconSize(QSize(20,20));toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolbar->setContentsMargins(6,2,6,2);
        for(auto *button:toolbar->findChildren<QToolButton*>()){
            button->setFocusPolicy(Qt::StrongFocus);button->setAutoRaise(true);
            if(auto *action=button->defaultAction()){
                const auto label=[button,action]{auto text=action->text();text.remove('&');button->setAccessibleName(text);};label();
                if(button->property("nativeObservedAction").value<QObject*>()!=action){
                    button->setProperty("nativeObservedAction",QVariant::fromValue<QObject*>(action));
                    connect(action,&QAction::changed,button,label);
                }
            }
        }
    }
public:
    explicit Chrome(QMainWindow *window):QObject(window),window(window) {
        setObjectName("nativeMacAppearance");window->installEventFilter(this);
        auto font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);font.setPixelSize(13);window->setFont(font);
        if(auto *toolbar=window->findChild<QToolBar*>("mainToolBar")){
            auto *more=new QToolButton(toolbar);more->setObjectName("nativeMoreCommands");more->setProperty("nativeSymbol","ellipsis");
            more->setAccessibleName(tr("More editor commands"));more->setToolTip(tr("More editor commands"));more->setPopupMode(QToolButton::InstantPopup);
            auto *menu=new QMenu(more);more->setMenu(menu);
            const QStringList groups[]{
                {"actionSaveAll","actionClose","actionCloseAll"},
                {"actionCut","actionCopy","actionPaste"},
                {"actionZoomIn","actionZoomOut","actionShowAllCharacters","actionShowIndentGuide"},
                {"actionMacroRecording","actionPlayback","actionRunMacroMultipleTimes","actionSaveCurrentRecordedMacro"}
            };
            for(const auto &group:groups){if(!menu->isEmpty())menu->addSeparator();for(const auto &name:group)if(auto *action=window->findChild<QAction*>(name)){toolbar->removeAction(action);menu->addAction(action);}}
            const auto existing=toolbar->actions();bool hasContent=false;
            for(int i=0;i<existing.size();++i){if(existing[i]->isSeparator()){if(!hasContent||i==existing.size()-1)toolbar->removeAction(existing[i]);hasContent=false;}else hasContent=true;}
            toolbar->addWidget(more);
        }
        for(auto *toolbar:window->findChildren<QToolBar*>()){toolbar->installEventFilter(this);buttons(toolbar);}
        if(auto *recording=window->findChild<QAction*>("actionMacroRecording"))connect(recording,&QAction::toggled,this,[this]{refresh();});
        refresh();
    }
    void refresh() {
        if(refreshing)return;refreshing=true;
        const auto requestedPalette=window->palette();
        const auto sheet=chromeStyle(requestedPalette)+window->property("nativeCustomStyle").toString();
        if(window->styleSheet()!=sheet)window->setStyleSheet(sheet);
        // Stylesheet repolishing must not replace explicitly requested editor
        // colors while handling QWidget's palette propagation.
        window->setPalette(requestedPalette);
        for(auto *action:window->findChildren<QAction*>()){
            auto symbol=actionSymbols.value(action->objectName());
            if(action->objectName()=="actionMacroRecording"&&action->isChecked())symbol="stop.fill";
            if(!symbol.isEmpty())action->setIcon(symbolIcon(symbol,action->icon(),requestedPalette));
            const auto brand=action->property("hostBrandResource").toString();if(!brand.isEmpty())action->setIcon(brandIcon(brand,requestedPalette));
        }
        for(auto *tab:window->findChildren<ads::CDockWidgetTab*>())styleTab(tab);
        for(auto *editor:window->findChildren<ScintillaNext*>())editorColors(editor);
        for(auto *toolbar:window->findChildren<QToolBar*>())buttons(toolbar);
        if(auto *more=window->findChild<QToolButton*>("nativeMoreCommands"))more->setIcon(symbolIcon("ellipsis",{},requestedPalette));
        refreshing=false;
    }
    bool eventFilter(QObject *watched,QEvent *event)override {
        if(watched==window&&event->type()==QEvent::PaletteChange)refresh();
        if(auto *toolbar=qobject_cast<QToolBar*>(watched);toolbar&&event->type()==QEvent::ChildAdded){
            const QPointer<QToolBar> safe=toolbar;
            QTimer::singleShot(0,this,[this,safe]{if(safe)buttons(safe);});
        }
        return false;
    }
};
}
#endif
void restoreEditorColors(ScintillaNext *editor) {
#ifdef Q_OS_MACOS
    const auto colors=editor->QObject::property("nativeStyleColors").toList();
    if(colors.size()!=512)return;
    for(int style=0;style<256;++style){editor->styleSetFore(style,colors[style*2].toInt());editor->styleSetBack(style,colors[style*2+1].toInt());}
#else
    Q_UNUSED(editor);
#endif
}
void editorColors(ScintillaNext *editor,bool remember) {
#ifdef Q_OS_MACOS
    auto colors=editor->QObject::property("nativeStyleColors").toList();
    if(remember||colors.size()!=512) {
        colors.clear();colors.reserve(512);
        for(int style=0;style<256;++style){colors.append(int(editor->styleFore(style)));colors.append(int(editor->styleBack(style)));}
        editor->QObject::setProperty("nativeStyleColors",colors);
    }
    const auto palette=editor->window()->palette();const auto background=palette.color(QPalette::Base),foreground=palette.color(QPalette::Text);
    const bool dark=background.lightness()<128;
    const auto unpack=[](int value){return QColor(value&255,(value>>8)&255,(value>>16)&255);};
    const auto pack=[](QColor value){return value.red()|(value.green()<<8)|(value.blue()<<16);};
    const auto luminance=[](QColor color){const auto linear=[](double value){return value<=0.04045?value/12.92:std::pow((value+0.055)/1.055,2.4);};return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());};
    for(int style=0;style<256;++style) {
        auto fg=unpack(colors[style*2].toInt()),bg=unpack(colors[style*2+1].toInt());
        if(bg==Qt::white)bg=background;
        else if(dark&&bg.lightness()>160)bg=background.lighter(120);
        if(fg==Qt::black)fg=foreground;
        for(int step=0;step<20;++step) {
            const auto a=luminance(fg),b=luminance(bg);
            if((qMax(a,b)+0.05)/(qMin(a,b)+0.05)>=4.5)break;
            const double lightness=qBound(0.0,fg.lightnessF()+(dark?0.04:-0.04),1.0);
            fg.setHslF(qMax(0.0,fg.hslHueF()),fg.hslSaturationF(),lightness);
        }
        editor->styleSetFore(style,pack(fg));editor->styleSetBack(style,pack(bg));
    }
    editor->styleSetFore(STYLE_LINENUMBER,pack(dark?QColor("#b6b6bd"):QColor("#626267")));
    editor->styleSetBack(STYLE_LINENUMBER,pack(QColor(NativeAppearance::colors(editor->palette()).gutter)));
    editor->setCaretFore(pack(foreground));
    editor->setCaretLineBack(pack(dark?QColor("#263b52"):QColor("#e7effb")));
    editor->setSelBack(true,pack(dark?QColor("#355272"):QColor("#d4e3fa")));
    editor->setSelFore(true,pack(foreground));
    editor->setMarginLeft(12);editor->setExtraAscent(2);editor->setExtraDescent(2);
#else
    Q_UNUSED(editor);Q_UNUSED(remember);
#endif
}
void install(QMainWindow *window) {
#ifdef Q_OS_MACOS
    if(!window->findChild<QObject*>("nativeMacAppearance",Qt::FindDirectChildrenOnly))new Chrome(window);
#else
    Q_UNUSED(window);
#endif
}
void documentTab(ads::CDockWidgetTab *tab,bool dirty,bool readOnly) {
#ifdef Q_OS_MACOS
    tab->setProperty("nativeDocumentState",readOnly?2:dirty?1:0);styleTab(tab);
#else
    tab->setIcon(QIcon(readOnly?":/icons/readonly.png":dirty?":/icons/unsaved.png":":/icons/saved.png"));
#endif
}
QString chromeStyle(const QPalette &palette) {
    const auto c=colors(palette);
    return QStringLiteral(
        "QToolBar{background:%1;border:0;border-bottom:1px solid %5;spacing:2px;padding:2px 6px;min-height:34px}"
        "QToolBar QToolButton{color:%3;border:1px solid transparent;border-radius:5px;padding:5px;min-width:20px;min-height:20px}"
        "QToolBar QToolButton:hover{background:%6}QToolBar QToolButton:pressed,QToolBar QToolButton:checked{background:%7}"
        "QToolBar QToolButton:focus{border-color:%8}"
        "QToolBar::separator{background:%5;width:1px;margin:7px 6px}"
        "ads--CDockAreaTitleBar,ads--CDockAreaTabBar{background:%1;border:0;border-bottom:1px solid %5;min-height:30px;padding:0}"
        "ads--CDockWidgetTab{background:%1;border:0;border-right:1px solid %5;margin:0;padding:0 8px;min-height:30px}"
        "ads--CDockWidgetTab[activeTab=\"true\"],ads--CDockWidgetTab[focused=\"true\"]{background:%2;border:0;border-right:1px solid %5;padding:0 8px;margin:0}"
        "ads--CDockWidgetTab[focused=\"true\"]{border-bottom:2px solid %8}"
        "ads--CDockWidgetTab:hover[activeTab=\"false\"]{background:%6}"
        "ads--CDockWidgetTab ads--CElidingLabel,ads--CDockWidgetTab QLabel{color:%3;font-size:13px}"
        "ads--CDockWidgetTab[activeTab=\"false\"] QLabel{color:%4}"
        "#tabCloseButton{background:transparent;border:1px solid transparent;border-radius:4px;padding:2px;margin:0 2px 0 6px;min-width:12px;min-height:12px}"
        "#tabCloseButton:hover{background:%6}#tabCloseButton:focus{border-color:%8}"
        "QDockWidget{font-size:13px;color:%3}QDockWidget::title{background:%1;padding:7px 10px;border-bottom:1px solid %5}"
        "QStatusBar{background:%1;color:%4;border-top:1px solid %5;min-height:24px}"
        "QStatusBar QLabel{font-size:11px;color:%4;padding:0 8px}QStatusBar::item{border:0}"
        "#QuickFindWidget{background:%1;border:1px solid %5;border-bottom:2px solid %8}"
    ).arg(c.chrome,c.surface,c.text,c.secondary,c.border,c.hover,c.selected,c.accent);
}
}
