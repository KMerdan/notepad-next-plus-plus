// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINDOCUMENTBRIDGE_H
#define PLUGINDOCUMENTBRIDGE_H
#include <QHash>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include "ScintillaEdit.h"

namespace PluginDocuments {
// Owns buffer identities, never the widgets. All operations and notifications
// execute on the GUI thread, including snapshot copying and version checks.
class DocumentBridge : public QObject {
    Q_OBJECT
public:
    static constexpr qsizetype MaxSnapshotBytes = 2 * 1024 * 1024;
    explicit DocumentBridge(QObject *parent = nullptr);
    QString attach(ScintillaEdit *editor, const QUrl &uri, const QString &language);
    QString idFor(ScintillaEdit *editor) const;
    void detach(const QString &id);
    void setActive(const QString &id);
    void updateMetadata(const QString &id, const QUrl &uri, const QString &language);
    void reset(const QString &id);
    QJsonValue active() const;
    // Native presentation only; no widget or appearance API crosses RPC.
    QFont activeEditorFont() const;
    void refreshAppearance();
    QJsonObject info(const QString &id) const;
    QJsonArray list() const;
    QJsonObject snapshot(const QString &id, QJsonObject *error) const;
    bool bytePosition(const QString &id, const QJsonObject &position, sptr_t *result, QJsonObject *error) const;
    QJsonObject applyEdits(const QString &id, qint64 expectedVersion, const QJsonArray &edits, QJsonObject *error);
    qint64 eventSequence() const { return sequence; }
    void retainDeltaStream();
    void releaseDeltaStream();
    qint64 deltaSequence() const { return mirrorSequence; }
signals:
    void appearanceChanged();
    void event(const QString &type, const QJsonValue &document, qint64 eventSeq);
    void deltaEvent(const QJsonObject &event, qint64 eventSeq);
private:
    struct PendingDelta {QJsonObject range;sptr_t position=-1,length=0; qint64 offset=0,units=0;};
    struct Buffer { QPointer<ScintillaEdit> editor; QUrl uri; QString language; qint64 version = 1; bool applying = false; QList<QMetaObject::Connection> connections;PendingDelta pending; bool metadataPending=false; mutable QJsonObject lastInfo; };
    void emitEvent(const QString &type, const QJsonValue &document);
    void emitDelta(const QJsonObject &event);
    bool deltaPosition(ScintillaEdit *editor,sptr_t byte,QJsonObject *position) const;
    QHash<QString, Buffer> buffers;
    QString activeId;
    qint64 sequence = 0;
    qint64 mirrorSequence=0;
    int deltaObservers=0;
};
}
#endif
