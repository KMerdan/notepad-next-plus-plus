// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NATIVEAPPEARANCE_H
#define NATIVEAPPEARANCE_H
#include <QIcon>
#include <QPalette>
class QMainWindow;
class QDialog;
class QPushButton;
class QStatusBar;
class ScintillaNext;
namespace ads { class CDockWidgetTab; }
namespace NativeAppearance {
struct Colors {
    bool dark;
    QString chrome, surface, text, secondary, border, hover, selected, accent;
    QString fieldBorder, disabled, onAccent, gutter;
    QString successText, successSurface, errorText, errorSurface, destructive, onDestructive;
};
inline Colors colors(const QPalette &palette) {
    const bool dark=palette.color(QPalette::Window).lightness()<128;
    return {dark,dark?"#29292c":"#f1f1f1",dark?"#202023":"#ffffff",
        dark?"#eeeeef":"#242424",dark?"#b6b6bd":"#626267",
        dark?"#4b4b50":"#cbcbcb",dark?"#3a3a3f":"#e4e4e8",
        dark?"#253c57":"#e7effb",dark?"#85baff":"#005ec5",
        dark?"#777780":"#85858c",dark?"#898990":"#85858c",dark?"#202023":"#ffffff",dark?"#26262a":"#f7f7f7",
        dark?"#85d0a1":"#22643a",dark?"#243b2d":"#edf6ef",dark?"#ffb4b0":"#b3261e",dark?"#482c30":"#fff0ef",dark?"#ffb4b0":"#b3261e",dark?"#202023":"#ffffff"};
}
// Native symbols are resolved from macOS; other platforms keep their assets.
QIcon symbolIcon(const QString &name,const QIcon &fallback,const QPalette &palette);
QIcon brandIcon(const QString &resource,const QPalette &palette);
void install(QMainWindow *window);
void documentTab(ads::CDockWidgetTab *tab,bool dirty,bool readOnly);
void editorColors(ScintillaNext *editor,bool remember=false);
QString chromeStyle(const QPalette &palette);
void polishDialog(QDialog *dialog,const QString &heading={},const QString &description={});
void setDialogPrimary(QDialog *dialog,QPushButton *button);
void dialogFeedback(QStatusBar *status,const QString &tone);
}
#endif
