// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include "ScintillaEdit.h"
#include <functional>
class NotepadNextApplication;
class MainWindow;
class QMainWindow;
class ScintillaNext;
namespace PluginDocuments { class DocumentBridge; }
// Native application services. The transport only supplies an owner and a live
// commit guard; no Qt object, pointer or Scintilla message crosses the protocol.
class NativeEditorServices : public QObject {
public:
    NativeEditorServices(NotepadNextApplication *app,PluginDocuments::DocumentBridge *documents);
    QJsonValue dispatch(const QString &method,const QJsonObject &params,QMainWindow *window,
                        const QString &owner,std::function<bool()> allowed,QJsonObject *error);
    void releaseOwner(const QString &owner);
    void publishOwner(const QString &owner);
    int resourceCount(const QString &owner) const;
private:
    struct Decoration {QString owner,documentId,name;QPointer<ScintillaNext> editor;int indicator;qint64 version=0;QVector<QPair<sptr_t,sptr_t>> ranges;};
    void draw(const Decoration &value);
    void release(const QString &id);
    NotepadNextApplication *app;
    PluginDocuments::DocumentBridge *documents;
    QHash<QString,Decoration> decorations;
    QSet<QString> publishedOwners;
};
