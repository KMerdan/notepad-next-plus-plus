// SPDX-License-Identifier: GPL-3.0-or-later
#include "HtmlPolicy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
namespace PluginWebviews {
bool HtmlPolicy::sanitize(const QString &fragment, const QHash<QString, QUrl> &images, QString *safe, QString *error) {
    const auto reject = [error](const QString &message) { *error = message; return false; };
    if (fragment.size() > MaxHtmlBytes || fragment.toUtf8().size() > MaxHtmlBytes) return reject("Panel content exceeds 6 MiB.");
    static const QSet<QString> tags = {"p","h1","h2","h3","h4","h5","h6","ul","ol","li","pre","code","em","strong","del","blockquote","hr","br","img","a","table","thead","tbody","tr","th","td","span","sup","sub"};
    QXmlStreamReader reader(QStringLiteral("<nnp-root>")+fragment+QStringLiteral("</nnp-root>"));
    QString result; QXmlStreamWriter writer(&result); int depth = 0;
    while (!reader.atEnd()) {
        const auto type = reader.readNext();
        if (type == QXmlStreamReader::DTD || type == QXmlStreamReader::ProcessingInstruction || type == QXmlStreamReader::EntityReference) return reject("Panel content contains a prohibited XML instruction.");
        if (type == QXmlStreamReader::StartElement) {
            const auto tag = reader.name().toString(); ++depth;
            if (depth == 1) continue;
            if (depth > 128 || !reader.namespaceUri().isEmpty() || !reader.namespaceDeclarations().isEmpty() || !tags.contains(tag)) return reject("Panel content contains an unsupported element: "+tag);
            writer.writeStartElement(tag);
            for (const auto &attribute : reader.attributes()) {
                const auto name=attribute.name().toString(); const auto value=attribute.value().toString();
                if(!attribute.namespaceUri().isEmpty()) return reject("Namespaced panel attributes are not supported.");
                if(name=="title" || (tag=="img" && name=="alt")) { if(value.size()>2048) return reject("Panel attribute is too long."); writer.writeAttribute(name,value); }
                else if(name=="class" && QRegularExpression("^[a-zA-Z0-9_ -]{0,128}$").match(value).hasMatch()) writer.writeAttribute(name,value);
                else if(tag=="a" && name=="href") {
                    const QUrl url(value,QUrl::StrictMode);
                    if(url.isValid() && !url.hasQuery() && value.startsWith('#')) writer.writeAttribute(name,value);
                    else if(url.isValid() && (url.scheme()=="https" || url.scheme()=="http") && url.userInfo().isEmpty() && !url.host().isEmpty()) writer.writeAttribute(name,url.toString(QUrl::FullyEncoded));
                    // Other link targets remain readable text without navigation.
                } else if(tag=="img" && name=="src") {
                    if(!images.contains(value)) return reject("Panel image is not a mapped resource."); writer.writeAttribute(name,images[value].toString(QUrl::FullyEncoded));
                } else if((tag=="td" || tag=="th") && name=="align" && (value=="left" || value=="right" || value=="center")) writer.writeAttribute(name,value);
                else if((tag=="img" && (name=="width" || name=="height")) || (tag=="ol" && name=="start")) {
                    bool ok=false; const auto number=value.toInt(&ok); if(!ok || number<1 || number>10000) return reject("Invalid numeric panel attribute."); writer.writeAttribute(name,QString::number(number));
                } else return reject("Panel attribute is not allowed: "+name);
            }
        } else if(type==QXmlStreamReader::EndElement) { if(depth>1) writer.writeEndElement(); --depth; }
        else if(type==QXmlStreamReader::Characters) writer.writeCharacters(reader.text().toString());
    }
    if(reader.hasError() || depth!=0) return reject("Expected a well-formed XHTML fragment: "+reader.errorString());
    *safe=result; *error={}; return true;
}
bool HtmlPolicy::readImage(const QString &root, const QString &relative, QByteArray *bytes, QByteArray *mime, QString *error) {
    const auto reject=[error](const QString &message) { *error=message; return false; };
    if(relative.isEmpty() || relative.size()>1024 || QDir::isAbsolutePath(relative) || relative.contains('\\') || relative.contains(':') || relative.contains(QChar::Null)) return reject("Image path must be relative.");
    for(const auto &part:relative.split('/')) if(part.isEmpty() || part=="." || part=="..") return reject("Image path contains traversal.");
    const auto base=QFileInfo(root).canonicalFilePath(), file=QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
    const auto prefix=base.endsWith('/') ? base : base+'/';
    if(base.isEmpty() || file.isEmpty() || !file.startsWith(prefix) || !QFileInfo(file).isFile()) return reject("Image is missing or escapes its resource root.");
    QFile input(file); if(input.size()>8*1024*1024 || !input.open(QIODevice::ReadOnly)) return reject("Image cannot be read or exceeds 8 MiB.");
    const auto content=input.read(8*1024*1024+1); if(content.size()>8*1024*1024) return reject("Image exceeds 8 MiB.");
    QBuffer buffer; buffer.setData(content); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer); reader.setDecideFormatFromContent(true);
    const auto format=reader.format(); const auto size=reader.size();
    if((format!="png" && format!="jpeg" && format!="jpg" && format!="webp") || !size.isValid() || size.width()>4096 || size.height()>4096) return reject("Expected a PNG, JPEG or WebP image no larger than 4096 pixels per side.");
    *bytes=content; *mime=format=="png" ? "image/png" : format=="webp" ? "image/webp" : "image/jpeg"; *error={}; return true;
}
}
