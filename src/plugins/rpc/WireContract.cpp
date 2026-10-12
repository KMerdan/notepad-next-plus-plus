// SPDX-License-Identifier: GPL-3.0-or-later
#include "WireContract.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
static void initializeWireResources() {Q_INIT_RESOURCE(wire_contract);}
namespace PluginRpc {
namespace {
QJsonObject read(const QString &file) {QFile f(file);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
struct Documents {
    QJsonObject schema,contract;
    Documents() {initializeWireResources();schema=read(":/plugins/wire.schema.json");contract=read(":/plugins/wire-contract.json");}
};
const Documents &documents() {static const Documents value;return value;}
bool json(const QJsonValue &value,int depth=0) {
    if(depth>128||value.isUndefined()||(value.isDouble()&&!std::isfinite(value.toDouble())))return false;
    if(value.isArray())for(const auto &item:value.toArray())if(!json(item,depth+1))return false;
    if(value.isObject())for(const auto &item:value.toObject())if(!json(item,depth+1))return false;
    return true;
}
bool matches(const QJsonValue &value,const QJsonObject &schema,const QJsonObject &defs,bool *tooLarge,int depth=0) {
    if(depth>192)return false;
    if(schema.contains("$ref")) {
        const auto ref=schema["$ref"].toString();if(!ref.startsWith("#/$defs/")||!defs.contains(ref.mid(8)))return false;
        if(!matches(value,defs[ref.mid(8)].toObject(),defs,tooLarge,depth+1))return false;
    }
    if(schema.contains("const")&&value!=schema["const"])return false;
    if(schema.contains("enum")&&!schema["enum"].toArray().contains(value))return false;
    if(schema.contains("oneOf")) {
        int count=0;for(const auto &branch:schema["oneOf"].toArray())if(matches(value,branch.toObject(),defs,tooLarge,depth+1))++count;
        if(count!=1)return false;
    }
    const auto type=schema["type"].toString();
    if(!type.isEmpty()) {
        const bool ok=(type=="object"&&value.isObject())||(type=="array"&&value.isArray())||(type=="string"&&value.isString())||(type=="null"&&value.isNull())||(type=="boolean"&&value.isBool())||(type=="integer"&&value.isDouble()&&std::isfinite(value.toDouble())&&std::floor(value.toDouble())==value.toDouble());
        if(!ok)return false;
    }
    if(value.isObject()) {
        const auto object=value.toObject(),properties=schema["properties"].toObject();
        for(const auto &key:schema["required"].toArray())if(!object.contains(key.toString()))return false;
        for(auto it=object.begin();it!=object.end();++it) {
            if(properties.contains(it.key())) {if(!matches(it.value(),properties[it.key()].toObject(),defs,tooLarge,depth+1))return false;}
            else if(schema["additionalProperties"].isBool()&&!schema["additionalProperties"].toBool())return false;
        }
    }
    if(value.isArray()) {
        const auto array=value.toArray();
        if((schema.contains("minItems")&&array.size()<schema["minItems"].toInt())||(schema.contains("maxItems")&&array.size()>schema["maxItems"].toInt()))return false;
        for(int i=0;i<array.size();++i) {
            if(schema.contains("items")&&!matches(array[i],schema["items"].toObject(),defs,tooLarge,depth+1))return false;
            if(schema["uniqueItems"].toBool())for(int j=0;j<i;++j)if(array[i]==array[j])return false;
        }
    }
    if(value.isString()) {
        const auto text=value.toString();
        if(schema.contains("x-nnp-maxUtf8Bytes")&&text.toUtf8().size()>schema["x-nnp-maxUtf8Bytes"].toInt()){*tooLarge=true;return false;}
        const auto length=text.toUcs4().size();
        if((schema.contains("minLength")&&length<schema["minLength"].toInt())||(schema.contains("maxLength")&&length>schema["maxLength"].toInt()))return false;
        if(schema.contains("pattern")&&!QRegularExpression(schema["pattern"].toString()).match(text).hasMatch())return false;
    }
    if(value.isDouble()) {
        if((schema.contains("minimum")&&value.toDouble()<schema["minimum"].toDouble())||(schema.contains("maximum")&&value.toDouble()>schema["maximum"].toDouble()))return false;
    }
    return true;
}
}
bool WireContract::validate(const QString &method,const QString &part,const QJsonValue &value,QString *error,int *failureCode) {
    const auto &data=documents();const auto contract=data.contract["methods"].toObject().value(method).toObject();
    const auto ref=contract.value(part+"Schema").toString();
    bool tooLarge=false;
    const bool ok=(part=="params"||part=="result")&&!ref.isEmpty()&&json(value)&&matches(value,QJsonObject{{"$ref",ref}},data.schema["$defs"].toObject(),&tooLarge);
    if(failureCode)*failureCode=tooLarge?1008:(part=="params"?-32602:-32603);
    if(error)*error=ok?QString():QStringLiteral("Invalid %1 contract for %2").arg(part,method);
    return ok;
}
QStringList WireContract::methods() {return documents().contract["methods"].toObject().keys();}
}
