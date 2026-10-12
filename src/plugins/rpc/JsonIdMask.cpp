// SPDX-License-Identifier: GPL-3.0-or-later
#include "JsonIdMask.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QVector>

namespace PluginRpc {
namespace {
class Scanner {
public:
    explicit Scanner(const QByteArray &bytes) : bytes(bytes) {}
    bool parse() { if (!value(0, true)) return false; space(); return pos == bytes.size(); }
    QByteArray masked() const {
        auto result = bytes;
        for (auto it = numbers.crbegin(); it != numbers.crend(); ++it) result.replace(it->first, it->second, "0");
        return result;
    }
    QString fault;
private:
    void space() { while (pos < bytes.size() && (bytes[pos] == ' ' || bytes[pos] == '\r' || bytes[pos] == '\n' || bytes[pos] == '\t')) ++pos; }
    bool take(char c) { space(); if (pos == bytes.size() || bytes[pos] != c) return false; ++pos; return true; }
    bool digit() const { return pos < bytes.size() && bytes[pos] >= '0' && bytes[pos] <= '9'; }
    bool string() {
        if (!take('"')) return false;
        while (pos < bytes.size()) {
            const auto c = static_cast<unsigned char>(bytes[pos++]);
            if (c == '"') return true;
            if (c < 32) return false;
            if (c != '\\') continue;
            if (pos == bytes.size()) return false;
            const char escape = bytes[pos++];
            if (escape == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (pos == bytes.size()) return false;
                    const char h = bytes[pos++];
                    if (!(h >= '0' && h <= '9') && !(h >= 'a' && h <= 'f') && !(h >= 'A' && h <= 'F')) return false;
                }
            } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f'
                       && escape != 'n' && escape != 'r' && escape != 't') return false;
        }
        return false;
    }
    bool number() {
        if (pos < bytes.size() && bytes[pos] == '-') ++pos;
        if (!digit()) return false;
        if (bytes[pos] == '0') ++pos;
        else while (digit()) ++pos;
        if (pos < bytes.size() && bytes[pos] == '.') { ++pos; if (!digit()) return false; while (digit()) ++pos; }
        if (pos < bytes.size() && (bytes[pos] == 'e' || bytes[pos] == 'E')) {
            ++pos; if (pos < bytes.size() && (bytes[pos] == '+' || bytes[pos] == '-')) ++pos;
            if (!digit()) return false; while (digit()) ++pos;
        }
        return true;
    }
    bool value(int depth, bool envelope) {
        space(); if (pos == bytes.size()) return false;
        if (depth >= 128 && (bytes[pos] == '{' || bytes[pos] == '[')) { fault = QStringLiteral("RPC JSON exceeds 128 nesting levels."); return false; }
        if (bytes[pos] == '"') return string();
        if (bytes[pos] == '{') {
            ++pos; if (take('}')) return true;
            for (;;) {
                space(); const auto keyStart = pos;
                if (!string()) return false;
                const auto keyRaw = bytes.mid(keyStart, pos - keyStart);
                const bool id = envelope && QJsonDocument::fromJson('[' + keyRaw + ']').array().first().toString() == QStringLiteral("id");
                if (!take(':')) return false;
                space(); const auto start = pos;
                const bool numeric = pos < bytes.size() && (bytes[pos] == '-' || digit());
                if (!value(depth + 1, false)) return false;
                if (id) {
                    if (pos - start > 1024) { fault = QStringLiteral("RPC ID token exceeds 1024 bytes."); return false; }
                    if (numeric) numbers.append({start, pos - start});
                }
                if (take('}')) return true;
                if (!take(',')) return false;
            }
        }
        if (bytes[pos] == '[') {
            ++pos; if (take(']')) return true;
            for (;;) {
                if (!value(depth + 1, depth == 0)) return false;
                if (take(']')) return true;
                if (!take(',')) return false;
            }
        }
        for (const auto &literal : {QByteArray("true"), QByteArray("false"), QByteArray("null")})
            if (bytes.mid(pos, literal.size()) == literal) { pos += literal.size(); return true; }
        return number();
    }
    const QByteArray &bytes;
    qsizetype pos = 0;
    QVector<QPair<qsizetype, qsizetype>> numbers;
};
}
bool maskNumericIds(const QByteArray &input, QByteArray *treeInput, QString *fault)
{
    Scanner scanner(input);
    if (!scanner.parse()) { if (fault) *fault = scanner.fault; return false; }
    *treeInput = scanner.masked(); return true;
}
}
