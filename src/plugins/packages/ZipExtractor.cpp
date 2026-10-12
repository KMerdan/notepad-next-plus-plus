// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZipExtractor.h"
#include "PackageStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <zlib.h>
namespace PluginPackages {
namespace {
bool reject(QString *error,const QString &message){if(error)*error=message;return false;}
quint16 u16(const QByteArray &b,qsizetype offset){return qFromLittleEndian<quint16>(b.constData()+offset);}
quint32 u32(const QByteArray &b,qsizetype offset){return qFromLittleEndian<quint32>(b.constData()+offset);}
bool at(QFile &f,qint64 offset,qint64 count,QByteArray *bytes){if(offset<0||count<0||offset>f.size()-count||!f.seek(offset))return false;*bytes=f.read(count);return bytes->size()==count;}
bool extras(const QByteArray &bytes) {
    for(qsizetype i=0;i<bytes.size();) {
        if(bytes.size()-i<4)return false;const auto type=u16(bytes,i),size=u16(bytes,i+2);i+=4;
        if(size>bytes.size()-i)return false;
        // Only inert timestamp/Unix uid and Unicode-name metadata are accepted.
        // Paths always come from the mandatory name, never an alternate field.
        if(type!=0x5455&&type!=0x7875&&type!=0x7075&&type!=0x000a&&type!=0x5855)return false;
        i+=size;
    }
    return true;
}
struct Entry {QString path;QByteArray rawName;quint16 flags,method;quint32 crc,compressed,size,offset;bool directory;};
}
bool ZipExtractor::extract(const QString &archive,const QString &destination,QString *error,const std::atomic_bool *cancel,const std::function<void(qint64)> &progress) {
    const auto fail=[&](const QString &reason){return reject(error,"Invalid plugin ZIP: "+reason);};
    const auto interrupted=[&]{return cancel&&cancel->load();};
    if(interrupted())return reject(error,"Installation cancelled.");
    if(QFileInfo(destination).isSymLink()||!QFileInfo(destination).isDir()||!QDir(destination).entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System).isEmpty())return fail("the extraction directory must be empty and real.");
    QFile file(archive);
    if(QFileInfo(archive).isSymLink()||!file.open(QIODevice::ReadOnly)||file.size()<22||file.size()>MaxArchiveBytes)return fail("archive is missing, too small or exceeds 128 MiB.");
    const qint64 tailStart=qMax(qint64(0),file.size()-65557);QByteArray tail;
    if(!at(file,tailStart,file.size()-tailStart,&tail))return fail("cannot read archive footer.");
    qsizetype footer=-1;
    for(qsizetype i=tail.size()-22;i>=0;--i)if(u32(tail,i)==0x06054b50&&i+22+u16(tail,i+20)==tail.size()){footer=i;break;}
    if(footer<0)return fail("missing end-of-central-directory record.");
    const auto count=u16(tail,footer+10);const auto centralSize=u32(tail,footer+12),centralOffset=u32(tail,footer+16);
    if(u16(tail,footer+4)||u16(tail,footer+6)||u16(tail,footer+8)!=count||count==0||count>PackageStore::MaxFiles||centralSize>8*1024*1024||qint64(centralOffset)+centralSize!=tailStart+footer)return fail("unsupported multidisk/ZIP64 archive or directory budget.");
    QByteArray central;if(!at(file,centralOffset,centralSize,&central))return fail("cannot read central directory.");
    QVector<Entry> entries;QSet<QString> names;qint64 total=0;qsizetype cursor=0;
    for(int i=0;i<count;++i) {
        if(interrupted())return reject(error,"Installation cancelled.");
        if(central.size()-cursor<46||u32(central,cursor)!=0x02014b50)return fail("malformed central directory.");
        const auto madeBy=u16(central,cursor+4),needed=u16(central,cursor+6),flags=u16(central,cursor+8),method=u16(central,cursor+10);
        const auto nameSize=u16(central,cursor+28),extraSize=u16(central,cursor+30),commentSize=u16(central,cursor+32);
        const auto attributes=u32(central,cursor+38);const quint16 unixType=(attributes>>16)&0170000;
        if(needed>20||flags&~quint16(0x080e)||(method!=0&&method!=8)||(method==0&&(flags&6))||u16(central,cursor+34)||nameSize==0||nameSize>2048||cursor+46+nameSize+extraSize+commentSize>central.size())return fail("unsupported entry flags, compression or directory fields.");
        Entry e;e.flags=flags;e.method=method;e.crc=u32(central,cursor+16);e.compressed=u32(central,cursor+20);e.size=u32(central,cursor+24);e.offset=u32(central,cursor+42);e.rawName=central.mid(cursor+46,nameSize);
        if(!extras(central.mid(cursor+46+nameSize,extraSize)))return fail("unsupported extra metadata.");
        QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);e.path=decoder(e.rawName);
        if(decoder.hasError()||(!(flags&0x800)&&std::any_of(e.rawName.begin(),e.rawName.end(),[](char c){return quint8(c)>127;})))return fail("path encoding must be ASCII or declared UTF-8.");
        e.directory=e.path.endsWith('/');if(e.directory)e.path.chop(1);
        if(!PackageStore::isSafeRelativePath(e.path)||e.path.split('/').size()>32||e.path==".nnp-receipt.json")return fail("unsafe path: "+e.path);
        const auto alias=e.path.normalized(QString::NormalizationForm_C).toCaseFolded();if(names.contains(alias))return fail("duplicate portable path: "+e.path);names.insert(alias);
        if((madeBy>>8)==3&&unixType&&unixType!=(e.directory?0040000:0100000))return fail("links and special files are unsupported.");
        if((attributes&0x10)&&!e.directory)return fail("directory attribute contradicts its path.");
        if((e.directory&&(e.size||e.compressed||e.crc))||e.size>PackageStore::MaxFileBytes||e.compressed>MaxArchiveBytes||(total+=e.size)>PackageStore::MaxPackageBytes)return fail("expanded size budget exceeded.");
        entries.append(e);cursor+=46+nameSize+extraSize+commentSize;
    }
    if(cursor!=central.size())return fail("unexpected trailing central data.");
    std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b){return a.offset<b.offset;});
    qint64 nextOffset=0,copiedTotal=0;
    for(const auto &e:entries) {
        if(interrupted())return reject(error,"Installation cancelled.");
        if(e.offset!=nextOffset)return fail("overlapping, hidden or prefixed archive content.");
        QByteArray header;if(!at(file,e.offset,30,&header)||u32(header,0)!=0x04034b50||u16(header,4)>20||u16(header,6)!=e.flags||u16(header,8)!=e.method)return fail("central/local header mismatch.");
        const auto nameSize=u16(header,26),extraSize=u16(header,28);QByteArray local;
        if(!at(file,qint64(e.offset)+30,qint64(nameSize)+extraSize,&local)||local.left(nameSize)!=e.rawName||!extras(local.mid(nameSize)))return fail("local path/extra mismatch.");
        if(!(e.flags&8)&&(u32(header,14)!=e.crc||u32(header,18)!=e.compressed||u32(header,22)!=e.size))return fail("local size/CRC mismatch.");
        if((e.flags&8)&&((u32(header,14)&&u32(header,14)!=e.crc)||(u32(header,18)&&u32(header,18)!=e.compressed)||(u32(header,22)&&u32(header,22)!=e.size)))return fail("invalid descriptor header.");
        const qint64 dataOffset=qint64(e.offset)+30+nameSize+extraSize;
        nextOffset=dataOffset+e.compressed;
        if(nextOffset>centralOffset)return fail("entry exceeds file-data bounds.");
        if(e.flags&8) {
            QByteArray descriptor;if(!at(file,nextOffset,12,&descriptor))return fail("truncated data descriptor.");
            const bool signature=u32(descriptor,0)==0x08074b50;
            if(signature&&!at(file,nextOffset+4,12,&descriptor))return fail("truncated data descriptor.");
            if(u32(descriptor,0)!=e.crc||u32(descriptor,4)!=e.compressed||u32(descriptor,8)!=e.size)return fail("data descriptor mismatch.");
            nextOffset+=signature?16:12;if(nextOffset>centralOffset)return fail("descriptor exceeds data bounds.");
        }
        const auto outputPath=destination+'/'+e.path;
        if(e.directory){if(!QDir().mkpath(outputPath))return fail("cannot create directory.");continue;}
        if(!QDir().mkpath(QFileInfo(outputPath).absolutePath()))return fail("path conflicts with another file.");
        QFile output(outputPath);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!file.seek(dataOffset))return fail("cannot open extracted file.");
        qint64 remaining=e.compressed,written=0;uLong crc=crc32(0,Z_NULL,0);z_stream stream{};
        struct End {z_stream *stream;bool initialized=false;~End(){if(initialized)inflateEnd(stream);}} cleanup{&stream};
        if(e.method==8){if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)return fail("cannot initialize deflate.");cleanup.initialized=true;}
        bool ended=e.method==0;
        do {
            if(interrupted())return reject(error,"Installation cancelled.");
            const auto input=file.read(qMin(qint64(65536),remaining));if(input.size()!=qMin(qint64(65536),remaining))return fail("truncated compressed data.");remaining-=input.size();
            if(e.method==0) {
                written+=input.size();if(written>e.size||output.write(input)!=input.size())return fail("stored size mismatch or write failure.");crc=crc32(crc,reinterpret_cast<const Bytef*>(input.constData()),uInt(input.size()));
            }else {
                stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));stream.avail_in=uInt(input.size());
                do {
                    if(interrupted())return reject(error,"Installation cancelled.");
                    char bytes[65536];stream.next_out=reinterpret_cast<Bytef*>(bytes);stream.avail_out=sizeof(bytes);const int status=inflate(&stream,Z_NO_FLUSH);const auto count=sizeof(bytes)-stream.avail_out;
                    written+=count;if(written>e.size||written>PackageStore::MaxFileBytes||output.write(bytes,qint64(count))!=qint64(count))return fail("expanded size mismatch or write failure.");crc=crc32(crc,reinterpret_cast<const Bytef*>(bytes),uInt(count));
                    if(status==Z_STREAM_END){ended=true;if(stream.avail_in||remaining)return fail("trailing deflate data.");break;}
                    if(status!=Z_OK)return fail("corrupt or truncated deflate stream.");
                    if(!stream.avail_in&&stream.avail_out)break;
                }while(!ended);
            }
            if(progress)progress(copiedTotal+written);
        }while(remaining>0);
        if(!ended||written!=e.size||crc!=e.crc||!output.flush())return fail("CRC/size check or flush failed.");
        copiedTotal+=written;
    }
    if(nextOffset!=centralOffset)return fail("unlisted content before the central directory.");
    return true;
}
}
