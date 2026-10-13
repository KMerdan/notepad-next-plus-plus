// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextEncoding.h"
#include <QTextCodec>
#include <uchardet.h>
namespace {
QByteArray codecName(const QString &name) {return name.endsWith(" BOM")?name.chopped(4).toLatin1():name.toLatin1();}
QByteArray header(const QString &name) {
    if(name=="UTF-8 BOM")return QByteArray::fromHex("efbbbf");
    if(name=="UTF-16LE BOM")return QByteArray::fromHex("fffe");
    if(name=="UTF-16BE BOM")return QByteArray::fromHex("feff");
    return {};
}
bool decodeBytes(const QByteArray &bytes,QTextCodec *codec,QString *text) {
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    *text=codec->toUnicode(bytes.constData(),bytes.size(),&state);
    return !state.invalidChars&&!state.remainingChars;
}
}
QStringList TextEncoding::supported() {
    QStringList result;
    for(const auto &name:QStringList{"UTF-8","UTF-8 BOM","UTF-16LE","UTF-16LE BOM","UTF-16BE","UTF-16BE BOM","Windows-1252","ISO-8859-1","ISO-8859-15","Shift_JIS","EUC-JP","GB18030","GBK","Big5","EUC-KR","Windows-1251","KOI8-R"})
        if(QTextCodec::codecForName(codecName(name)))result.append(name);
    return result;
}
QString TextEncoding::label(const QString &encoding) {return encoding=="Shift_JIS"?QStringLiteral("Shift-JIS (Japanese)"):encoding;}
bool TextEncoding::decode(const QByteArray &bytes,const QString &encoding,QString *text,QString *error) {
    auto *codec=QTextCodec::codecForName(codecName(encoding));
    if(!supported().contains(encoding)||!codec){if(error)*error="Unsupported text encoding.";return false;}
    auto input=bytes;const auto bom=header(encoding);if(!bom.isEmpty()&&input.startsWith(bom))input.remove(0,bom.size());
    QString candidate;
    if(!decodeBytes(input,codec,&candidate)){if(error)*error=QString("The file is not valid %1. Its contents have not been changed.").arg(label(encoding));return false;}
    *text=candidate;return true;
}
bool TextEncoding::encode(const QString &text,const QString &encoding,QByteArray *bytes,QString *error) {
    auto *codec=QTextCodec::codecForName(codecName(encoding));
    if(!supported().contains(encoding)||!codec){if(error)*error="Unsupported text encoding.";return false;}
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    const auto encoded=codec->fromUnicode(text.constData(),text.size(),&state);
    QString roundTrip;
    if(state.invalidChars||!decodeBytes(encoded,codec,&roundTrip)||roundTrip!=text){
        if(error)*error=QString("%1 cannot represent all characters in this document. Choose UTF-8 or another Unicode encoding. No file has been written.").arg(label(encoding));return false;
    }
    *bytes=header(encoding)+encoded;return true;
}
QString TextEncoding::detected(const QByteArray &bytes) {
    if(bytes.startsWith(QByteArray::fromHex("efbbbf")))return "UTF-8 BOM";
    if(bytes.startsWith(QByteArray::fromHex("fffe")))return "UTF-16LE BOM";
    if(bytes.startsWith(QByteArray::fromHex("feff")))return "UTF-16BE BOM";
    QString text;if(decode(bytes,"UTF-8",&text,nullptr))return "UTF-8";
    auto detector=uchardet_new();uchardet_handle_data(detector,bytes.constData(),bytes.size());uchardet_data_end(detector);
    const auto guessed=QString::fromLatin1(uchardet_get_charset(detector));uchardet_delete(detector);
    for(const auto &encoding:supported())if(encoding.compare(guessed,Qt::CaseInsensitive)==0) {
        QByteArray roundTrip;if(decode(bytes,encoding,&text,nullptr)&&encode(text,encoding,&roundTrip,nullptr)&&roundTrip==bytes)return encoding;
    }
    // Latin-1 is a reversible byte mapping when detection is inconclusive.
    // The Encoding menu lets the user reload with the intended code page.
    return "ISO-8859-1";
}
