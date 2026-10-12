// SPDX-License-Identifier: GPL-3.0-or-later
#include <QImage>
#include <QString>
#include <QHash>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace NativeAppearance {
QImage symbolMask(const QString &name) {
    // Masks contain only the system glyph. Color comes from the Qt palette,
    // so normal, selected, disabled and dark appearance use the same geometry.
    static QHash<QString,QImage> cache;
    if(cache.contains(name))return cache.value(name);
    QImage result;
    @autoreleasepool {
        if(@available(macOS 11.0,*)) {
            NSString *symbolName=[NSString stringWithUTF8String:name.toUtf8().constData()];
            NSImage *symbol=[NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
            if(symbol) {
                NSImageSymbolConfiguration *configuration=[NSImageSymbolConfiguration configurationWithPointSize:20 weight:NSFontWeightRegular scale:NSImageSymbolScaleMedium];
                symbol=[symbol imageWithSymbolConfiguration:configuration];
                // AppKit rasterizes into the proposed rectangle. A square
                // stretches wide symbols (ellipsis/minus) into tall bars.
                const NSSize size=symbol.size;
                const double scale=128.0/qMax(size.width,size.height);
                NSRect proposed=NSMakeRect(0,0,size.width*scale,size.height*scale);
                CGImageRef source=[symbol CGImageForProposedRect:&proposed context:nil hints:nil];
                if(source) {
                    result=QImage(128,128,QImage::Format_RGBA8888_Premultiplied);result.fill(Qt::transparent);
                    CGColorSpaceRef colorSpace=CGColorSpaceCreateDeviceRGB();
                    const CGBitmapInfo flags=CGBitmapInfo(kCGImageAlphaPremultipliedLast)|CGBitmapInfo(kCGBitmapByteOrder32Big);
                    CGContextRef context=CGBitmapContextCreate(result.bits(),128,128,8,result.bytesPerLine(),colorSpace,flags);
                    if(context) {
                        const double width=CGImageGetWidth(source),height=CGImageGetHeight(source),factor=112.0/qMax(width,height);
                        CGContextDrawImage(context,CGRectMake((128-width*factor)/2,(128-height*factor)/2,width*factor,height*factor),source);
                        CGContextRelease(context);
                    }else result={};
                    CGColorSpaceRelease(colorSpace);
                }
            }
        }
    }
    cache.insert(name,result);return result;
}
}
