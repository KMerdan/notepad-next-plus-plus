// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeAppearance.h"
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QPainter>
#include <QCheckBox>
#include <QRadioButton>
#include <QKeySequenceEdit>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeAppearance {
#ifdef Q_OS_MACOS
namespace {
QString dialogStyle(const Colors &c) {
    return QStringLiteral(
        "QDialog[nativeDialog=\"true\"]{background:%1;color:%3}"
        "QLabel{color:%3}QLabel[dialogRole=\"heading\"]{font-size:20px;font-weight:600}"
        "QLabel[dialogRole=\"section\"]{font-size:16px;font-weight:600}"
        "QLabel[dialogRole=\"description\"]{color:%4;font-size:12px}QLabel[dialogRole=\"caption\"]{color:%4;font-size:11px}"
        "QGroupBox{background:%2;color:%3;border:0;border-radius:8px;margin-top:0;padding:28px 12px 12px}"
        "QGroupBox::title{subcontrol-origin:padding;subcontrol-position:top left;left:16px;top:8px;padding:0;font-weight:600}"
        "QCheckBox,QRadioButton{color:%3;spacing:8px;min-height:20px;padding:2px 4px;border:1px solid transparent;border-radius:4px}"
        "QCheckBox:focus,QRadioButton:focus{border-color:%8}QCheckBox:disabled,QRadioButton:disabled{color:%10}"
        "QLineEdit,QComboBox,QAbstractSpinBox,QKeySequenceEdit{background:%2;color:%3;border:1px solid %9;border-radius:6px;min-height:24px;padding:4px 8px;selection-background-color:%8;selection-color:%11}"
        "QComboBox:editable QLineEdit,QAbstractSpinBox QLineEdit,QKeySequenceEdit QLineEdit{border:0;background:transparent;padding:0;min-height:0}"
        "QLineEdit:focus,QComboBox:focus,QAbstractSpinBox:focus,QKeySequenceEdit:focus{border:1px solid %8}"
        "QLineEdit:disabled,QComboBox:disabled,QAbstractSpinBox:disabled{background:%1;color:%10;border-color:%5}"
        "QComboBox QAbstractItemView{background:%2;color:%3;selection-background-color:%7;selection-color:%3}"
        "QComboBox{padding-right:32px}QComboBox::drop-down{border:0;background:transparent;width:24px;margin:2px}"
        "QAbstractSpinBox::up-button,QAbstractSpinBox::down-button{background:transparent;border:0;width:20px}"
        "QAbstractSpinBox::up-arrow,QAbstractSpinBox::down-arrow{width:10px;height:10px}"
        "QPushButton,QToolButton{background:%2;color:%3;border:1px solid %5;border-radius:6px;padding:4px 12px;min-height:24px}"
        "QPushButton{min-width:64px}QToolButton{padding:4px 6px}"
        "QPushButton[dialogIconButton=\"true\"]{min-width:18px;max-width:18px;min-height:18px;max-height:18px;padding:6px}"
        "QPushButton:hover,QToolButton:hover{background:%6}QPushButton:pressed,QToolButton:pressed{background:%7}"
        "QPushButton:focus,QToolButton:focus{border-color:%8}"
        "QPushButton:disabled,QToolButton:disabled{color:%10;background:%1;border-color:%5}"
        "QPushButton[dialogPrimary=\"true\"]{background:%8;color:%11;border-color:%8;font-weight:600}"
        "QPushButton[dialogPrimary=\"true\"]:disabled{background:%1;color:%10;border-color:%5}"
        "QScrollArea{border:0;background:transparent}QScrollArea>QWidget>QWidget{background:transparent}"
        "QScrollBar:vertical{background:%1;width:12px;margin:0}QScrollBar::handle:vertical{background:%9;min-height:24px;border-radius:4px;margin:2px}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;border:0}QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent}"
        "QListView,QTableView{background:%2;color:%3;border:1px solid %5;border-radius:8px;selection-background-color:%7;selection-color:%3;outline:0}"
        "QListView:focus,QTableView:focus{border-color:%8}"
        "QHeaderView::section{background:%1;color:%4;border:0;border-bottom:1px solid %5;padding:6px}"
        "QListWidget#settingsCategories{background:transparent;border:0;outline:0}"
        "QListWidget#settingsCategories::item{min-height:36px;padding:4px 10px;border:1px solid transparent;border-radius:6px;margin:2px 0}"
        "QListWidget#settingsCategories::item:selected{background:%7;color:%3}"
        "QListWidget#settingsCategories::item:focus{border-color:%8}"
        "QTabBar#dialogModeTabs::tab{background:%1;color:%4;border:1px solid %5;padding:7px 20px;min-width:72px}"
        "QTabBar#dialogModeTabs::tab:selected{background:%7;color:%8;border-color:%8}"
        "QTabBar#dialogModeTabs::tab:hover{background:%6}"
        "QStatusBar{background:%2;color:%3;border:0;border-radius:6px;min-height:32px;font-size:12px}"
        "QStatusBar::item{border:0}"
    ).arg(c.chrome,c.surface,c.text,c.secondary,c.border,c.hover,c.selected,c.accent,c.fieldBorder,c.disabled,c.onAccent)+QStringLiteral("QPushButton[dialogPrimary=\"true\"][dialogDestructive=\"true\"]{background:%1;color:%2;border-color:%1}").arg(c.destructive,c.onDestructive);
}

void spaceLayouts(QLayout *layout,bool root) {
    if(!layout)return;
    layout->setSpacing(root?16:8);
    if(root)layout->setContentsMargins(24,24,24,24);
    if(auto *form=qobject_cast<QFormLayout*>(layout)){
        form->setLabelAlignment(Qt::AlignLeading|Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeading|Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setHorizontalSpacing(16);form->setVerticalSpacing(12);
    }
    for(int i=0;i<layout->count();++i)if(auto *nested=layout->itemAt(i)->layout())spaceLayouts(nested,false);
}

// Cocoa's unchecked indicators follow the process-wide OS appearance rather
// than a window's palette. Draw their geometry with the same window tokens so
// a dialog also remains legible during explicit per-window theme changes.
class FormStyle final:public QProxyStyle {
public:
    FormStyle():QProxyStyle(QStyleFactory::create("macOS")){}
    int pixelMetric(PixelMetric metric,const QStyleOption *option=nullptr,const QWidget *widget=nullptr)const override {
        if(metric==PM_IndicatorWidth||metric==PM_IndicatorHeight||metric==PM_ExclusiveIndicatorWidth||metric==PM_ExclusiveIndicatorHeight)return 16;
        return QProxyStyle::pixelMetric(metric,option,widget);
    }
    void drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget=nullptr)const override {
        if(element==PE_IndicatorArrowDown||element==PE_IndicatorArrowUp||element==PE_IndicatorSpinDown||element==PE_IndicatorSpinUp){
            auto palette=widget?widget->window()->palette():option->palette;
            symbolIcon(element==PE_IndicatorArrowDown||element==PE_IndicatorSpinDown?"chevron.down":"chevron.up",{},palette).paint(painter,option->rect);
            return;
        }
        if(element!=PE_IndicatorCheckBox&&element!=PE_IndicatorRadioButton){QProxyStyle::drawPrimitive(element,option,painter,widget);return;}
        const auto c=colors(widget?widget->window()->palette():option->palette);const bool checked=option->state&State_On,enabled=option->state&State_Enabled;
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        const auto rect=QRectF(option->rect).adjusted(1,1,-1,-1);
        painter->setPen(QPen(QColor(enabled?(checked?c.accent:c.fieldBorder):c.border),1.25));
        painter->setBrush(QColor(checked&&enabled?c.accent:c.surface));
        if(element==PE_IndicatorRadioButton){
            painter->drawEllipse(rect);
            if(checked){painter->setPen(Qt::NoPen);painter->setBrush(QColor(enabled?c.onAccent:c.disabled));painter->drawEllipse(rect.adjusted(4,4,-4,-4));}
        }else{
            painter->drawRoundedRect(rect,4,4);
            if(checked){
                auto palette=option->palette;palette.setColor(QPalette::WindowText,QColor(enabled?c.onAccent:c.disabled));
                const auto icon=symbolIcon("checkmark",{},palette);icon.paint(painter,option->rect.adjusted(3,3,-3,-3));
            }
        }
        painter->restore();
    }
};

class DialogChrome final:public QObject {
    QDialog *dialog;
    bool refreshing=false;
    void positionIndicator(QComboBox *combo) {
        if(auto *indicator=combo->findChild<QLabel*>("nativeComboIndicator",Qt::FindDirectChildrenOnly))
            indicator->setGeometry(combo->layoutDirection()==Qt::RightToLeft?8:combo->width()-24,(combo->height()-16)/2,16,16);
    }
public:
    explicit DialogChrome(QDialog *dialog):QObject(dialog),dialog(dialog){
        setObjectName("nativeDialogAppearance");dialog->setProperty("nativeDialog",true);
        dialog->installEventFilter(this);
        auto *formStyle=new FormStyle();formStyle->setParent(dialog);
        for(auto *control:dialog->findChildren<QWidget*>())if(qobject_cast<QCheckBox*>(control)||qobject_cast<QRadioButton*>(control)||qobject_cast<QGroupBox*>(control)||qobject_cast<QComboBox*>(control)||qobject_cast<QAbstractSpinBox*>(control))control->setStyle(formStyle);
        // A styled Cocoa drop-down suppresses its arrow. Keep the real combo
        // and popup, with a native symbol that lets clicks pass through.
        for(auto *combo:dialog->findChildren<QComboBox*>()){
            combo->setFocusPolicy(Qt::StrongFocus);combo->installEventFilter(this);
            auto *indicator=new QLabel(combo);indicator->setObjectName("nativeComboIndicator");
            indicator->setAttribute(Qt::WA_TransparentForMouseEvents);indicator->setFocusPolicy(Qt::NoFocus);
            positionIndicator(combo);
        }
        for(auto *shortcut:dialog->findChildren<QKeySequenceEdit*>())shortcut->setAttribute(Qt::WA_StyledBackground,true);
        for(auto *list:dialog->findChildren<QListWidget*>())if(list->objectName()=="settingsCategories")
            connect(list,&QListWidget::itemChanged,this,[this]{refresh();});
        auto font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);font.setPixelSize(13);dialog->setFont(font);
        refresh();
    }
    void refresh() {
        if(refreshing)return;refreshing=true;
        auto palette=dialog->palette();const auto c=colors(palette);
        palette.setColor(QPalette::Highlight,QColor(c.selected));palette.setColor(QPalette::HighlightedText,QColor(c.text));
        auto sheet=dialogStyle(c);
        if(dialog->layoutDirection()==Qt::RightToLeft)sheet.replace("subcontrol-position:top left;left:16px","subcontrol-position:top right;right:16px");
        if(dialog->styleSheet()!=sheet)dialog->setStyleSheet(sheet);
        if(dialog->palette()!=palette)dialog->setPalette(palette);
        for(auto *combo:dialog->findChildren<QComboBox*>()){
            auto *indicator=combo->findChild<QLabel*>("nativeComboIndicator",Qt::FindDirectChildrenOnly);
            indicator->setPixmap(symbolIcon("chevron.down",{},palette).pixmap(QSize(16,16)));indicator->raise();positionIndicator(combo);
        }
        for(auto *label:dialog->findChildren<QLabel*>()){
            const auto resource=label->property("hostBrandResource").toString();
            if(!resource.isEmpty())label->setPixmap(brandIcon(resource,palette).pixmap(label->size()));
            if(label->buddy()){auto text=label->text();text.remove('&');text.remove(':');label->buddy()->setAccessibleName(text.trimmed());}
        }
        for(auto *button:dialog->findChildren<QAbstractButton*>()){
            button->setFocusPolicy(Qt::StrongFocus);
            if(!button->text().isEmpty()){
                auto text=button->text();text.remove('&');button->setAccessibleName(text);
            }
            const auto symbol=button->property("nativeSymbol").toString();
            if(!symbol.isEmpty())button->setIcon(symbolIcon(symbol,button->icon(),palette));
        }
        for(auto *list:dialog->findChildren<QListWidget*>())for(int row=0;row<list->count();++row){
            auto *item=list->item(row);const auto symbol=item->data(Qt::UserRole+1).toString();
            if(!symbol.isEmpty())item->setIcon(symbolIcon(symbol,item->icon(),palette));
            if(list->objectName()=="settingsCategories"){
                const auto wrapped=QFontMetrics(list->font()).boundingRect(QRect(0,0,list->width()-60,10000),Qt::TextWordWrap,item->text());
                item->setSizeHint(QSize(list->width(),qMax(40,wrapped.height()+16)));
            }
        }
        for(auto *group:dialog->findChildren<QGroupBox*>())group->setAlignment(dialog->layoutDirection()==Qt::RightToLeft?Qt::AlignRight:Qt::AlignLeft);
        for(auto *status:dialog->findChildren<QStatusBar*>())dialogFeedback(status,status->property("feedbackTone").toString());
        refreshing=false;
    }
    bool eventFilter(QObject *watched,QEvent *event)override {
        if(auto *combo=qobject_cast<QComboBox*>(watched)){
            if(event->type()==QEvent::Resize||event->type()==QEvent::LayoutDirectionChange)positionIndicator(combo);
        }else if(event->type()==QEvent::PaletteChange||event->type()==QEvent::Show||event->type()==QEvent::LayoutDirectionChange)refresh();
        return false;
    }
};
}
#endif

void polishDialog(QDialog *dialog,const QString &heading,const QString &description) {
#ifdef Q_OS_MACOS
    if(dialog->findChild<QObject*>("nativeDialogAppearance",Qt::FindDirectChildrenOnly))return;
    spaceLayouts(dialog->layout(),true);
    for(auto *group:dialog->findChildren<QGroupBox*>())spaceLayouts(group->layout(),false);
    for(auto *form:dialog->findChildren<QFormLayout*>())spaceLayouts(form,false);
    if(auto *layout=qobject_cast<QVBoxLayout*>(dialog->layout());layout&&!heading.isEmpty()){
        auto *header=new QWidget(dialog);auto *headerLayout=new QVBoxLayout(header);
        headerLayout->setContentsMargins(0,0,0,0);headerLayout->setSpacing(4);
        auto *title=new QLabel(heading,header);title->setProperty("dialogRole","heading");headerLayout->addWidget(title);
        if(!description.isEmpty()){
            auto *caption=new QLabel(description,header);caption->setProperty("dialogRole","description");caption->setWordWrap(true);headerLayout->addWidget(caption);
        }
        layout->insertWidget(0,header);
    }
    for(auto *button:dialog->findChildren<QPushButton*>())button->setAutoDefault(false);
    new DialogChrome(dialog);
#else
    Q_UNUSED(dialog);Q_UNUSED(heading);Q_UNUSED(description);
#endif
}

void setDialogPrimary(QDialog *dialog,QPushButton *button) {
#ifdef Q_OS_MACOS
    for(auto *candidate:dialog->findChildren<QPushButton*>()){
        candidate->setProperty("dialogPrimary",candidate==button);candidate->setDefault(candidate==button);
        candidate->style()->unpolish(candidate);candidate->style()->polish(candidate);candidate->update();
    }
#else
    Q_UNUSED(dialog);Q_UNUSED(button);
#endif
}

void dialogFeedback(QStatusBar *status,const QString &tone) {
#ifdef Q_OS_MACOS
    status->setProperty("feedbackTone",tone);
    const auto c=colors(status->window()->palette());
    const auto fg=tone=="red"?c.errorText:tone=="green"?c.successText:c.text;
    const auto bg=tone=="red"?c.errorSurface:tone=="green"?c.successSurface:c.surface;
    status->setStyleSheet(QString("QStatusBar{background:%1;color:%2;border:0;border-radius:6px;min-height:32px;font-size:12px}QStatusBar::item{border:0}").arg(bg,fg));
#else
    status->setStyleSheet(QStringLiteral("color: %1").arg(tone));
#endif
}
}
