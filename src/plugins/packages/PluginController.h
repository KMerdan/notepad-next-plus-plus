// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINCONTROLLER_H
#define PLUGINCONTROLLER_H
#include <QObject>
namespace PluginPackages {
// Keeps the manager independent of Scintilla/WebEngine and permits UI tests
// with a fake controller. Runtime owns all concrete process lifecycle work.
class PluginController:public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString pluginState(const QString &id) const=0;
    virtual QString pluginLog(const QString &id) const=0;
    virtual int pluginPanelCount(const QString &id) const {Q_UNUSED(id);return 0;}
    virtual QString pluginFailureCategory(const QString &id) const {Q_UNUSED(id);return {};}
    virtual QString pluginFailureMessage(const QString &id) const {Q_UNUSED(id);return {};}
    virtual bool setEnabled(const QString &id,bool enabled,QString *error)=0;
    virtual void reload(const QString &id)=0;
    virtual void uninstall(const QString &id,bool keepData)=0;
    virtual void invoke(const QString &id,const QString &command)=0;
    virtual void rollback(const QString &id)=0;
signals:
    void logsChanged(const QString &pluginId);
    void statusChanged(const QString &pluginId);
    void operationFinished(const QString &pluginId,const QString &error);
};
}
#endif
