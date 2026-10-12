// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeAppearance.h"
#include <QIconEngine>
#include <QImage>
#include <QPainter>
namespace NativeAppearance {
#ifdef Q_OS_MACOS
QImage symbolMask(const QString &name);
namespace {
class SymbolEngine final:public QIconEngine {
    QString name;
    QImage mask;
    QPalette palette;
    bool brand=false;
public:
    SymbolEngine(QString name,QImage mask,QPalette palette,bool brand=false):name(std::move(name)),mask(std::move(mask)),palette(std::move(palette)),brand(brand){}
    QIconEngine *clone()const override{return new SymbolEngine(*this);}
    QString key()const override{return "NotepadNext.NativeSymbol";}
    QString iconName()override{return name;}
    void paint(QPainter *painter,const QRect &rect,QIcon::Mode mode,QIcon::State state)override {
        auto foreground=palette.color(mode==QIcon::Disabled?QPalette::Disabled:QPalette::Active,QPalette::WindowText);
        if(brand&&mode!=QIcon::Disabled)foreground=QColor(colors(palette).accent);
        else if(mode==QIcon::Selected)foreground=palette.color(QPalette::HighlightedText);
        else if(state==QIcon::On)foreground=QColor(palette.color(QPalette::Window).lightness()<128?"#85baff":"#005ec5");
        QImage tinted=mask;
        QPainter coloring(&tinted);coloring.setCompositionMode(QPainter::CompositionMode_SourceIn);coloring.fillRect(tinted.rect(),foreground);coloring.end();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);painter->drawImage(rect,tinted);
    }
    QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state)override {
        QPixmap result(size);result.fill(Qt::transparent);QPainter painter(&result);paint(&painter,QRect(QPoint(),size),mode,state);return result;
    }
    QPixmap scaledPixmap(const QSize &size,QIcon::Mode mode,QIcon::State state,qreal scale)override {
        auto result=pixmap(size*scale,mode,state);result.setDevicePixelRatio(scale);return result;
    }
};
}
#endif
QIcon symbolIcon(const QString &name,const QIcon &fallback,const QPalette &palette) {
#ifdef Q_OS_MACOS
    const auto mask=symbolMask(name);
    if(!mask.isNull())return QIcon(new SymbolEngine(name,mask,palette));
#else
    Q_UNUSED(name);Q_UNUSED(palette);
#endif
    return fallback;
}

QIcon brandIcon(const QString &resource,const QPalette &palette) {
#ifdef Q_OS_MACOS
    const auto mask=QIcon(resource).pixmap(QSize(128,128)).toImage();
    if(!mask.isNull())return QIcon(new SymbolEngine(resource,mask,palette,true));
#else
    Q_UNUSED(palette);
#endif
    return QIcon(resource);
}
}
