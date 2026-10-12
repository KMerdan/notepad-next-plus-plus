// SPDX-License-Identifier: GPL-3.0-or-later
#include "SemVer.h"
#include <QRegularExpression>
#include <algorithm>
namespace PluginPackages {
namespace {
bool numeric(const QString &s) { return !s.isEmpty() && std::all_of(s.begin(),s.end(),[](QChar c){return c >= '0' && c <= '9';}); }
int numberCompare(const QString &a,const QString &b) { return a.size()!=b.size() ? (a.size()<b.size()?-1:1) : QString::compare(a,b,Qt::CaseSensitive); }
QString increment(QString value) {
    for(qsizetype i=value.size()-1;i>=0;--i) {
        if(value[i]!='9') { value[i]=QChar(value[i].unicode()+1); return value; }
        value[i]='0';
    }
    return '1'+value;
}
}
bool SemVer::parse(const QString &text,SemVer *result) {
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    if(text.size()>128) return false;
    const auto m=pattern.match(text);
    if(!m.hasMatch()) return false;
    SemVer v; v.core={m.captured(1),m.captured(2),m.captured(3)};
    if(!m.captured(4).isEmpty()) v.prerelease=m.captured(4).split('.');
    for(const auto &id:v.prerelease) if(numeric(id) && id.size()>1 && id.startsWith('0')) return false;
    if(result) *result=v;
    return true;
}
int SemVer::compare(const SemVer &other) const {
    for(int i=0;i<3;++i) if(const int c=numberCompare(core[i],other.core[i]);c) return c;
    if(prerelease.isEmpty()!=other.prerelease.isEmpty()) return prerelease.isEmpty()?1:-1;
    for(qsizetype i=0;i<qMin(prerelease.size(),other.prerelease.size());++i) {
        const auto &a=prerelease[i],&b=other.prerelease[i];
        const bool an=numeric(a),bn=numeric(b);
        if(an!=bn) return an?-1:1;
        if(const int c=an?numberCompare(a,b):QString::compare(a,b,Qt::CaseSensitive);c) return c;
    }
    return prerelease.size()==other.prerelease.size()?0:(prerelease.size()<other.prerelease.size()?-1:1);
}
bool SemVer::satisfies(const QString &version,const QString &range,QString *error) {
    SemVer actual;
    const auto invalid=[&]{if(error)*error=QStringLiteral("Unsupported or invalid SemVer range: %1").arg(range);return false;};
    if(!parse(version,&actual)||range.isEmpty()||range.size()>128) return invalid();
    static const QRegularExpression term(QStringLiteral("^(>=|<=|>|<|=|\\^|~)?(.+)$"));
    bool any=false;
    for(const auto &branch:range.split("||")) {
        const auto terms=branch.trimmed().split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        if(terms.isEmpty()) return invalid();
        bool matched=true,preAllowed=actual.prerelease.isEmpty();
        for(const auto &text:terms) {
            if(text=="*") continue;
            const auto m=term.match(text); SemVer bound;
            if(!m.hasMatch()||!parse(m.captured(2),&bound)) return invalid();
            if(!bound.prerelease.isEmpty()&&actual.core==bound.core) preAllowed=true;
            const int c=actual.compare(bound); const auto op=m.captured(1);
            if(op==">=") matched &= c>=0;
            else if(op=="<=") matched &= c<=0;
            else if(op==">") matched &= c>0;
            else if(op=="<") matched &= c<0;
            else if(op=="^"||op=="~") {
                SemVer upper=bound; upper.prerelease.clear();
                int pivot=op=="~"?1:(bound.core[0]!="0"?0:bound.core[1]!="0"?1:2);
                upper.core[pivot]=increment(upper.core[pivot]);
                for(int i=pivot+1;i<3;++i) upper.core[i]="0";
                matched &= c>=0 && actual.compare(upper)<0;
            } else matched &= c==0;
        }
        any |= matched&&preAllowed;
    }
    if(!any&&error) *error=QStringLiteral("Host version %1 does not satisfy %2.").arg(version,range);
    return any;
}
}
