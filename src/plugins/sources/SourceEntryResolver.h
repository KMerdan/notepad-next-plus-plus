// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOURCEENTRYRESOLVER_H
#define SOURCEENTRYRESOLVER_H

#include "PluginMetadata.h"
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace PluginSources {

class SourceEntryResolver : public QObject
{
    Q_OBJECT
public:
    explicit SourceEntryResolver(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    bool isBusy() const { return busy; }
    void resolve(const QString &repository, const QString &ref = QString());
    void cancel();

signals:
    void resolved(const PluginSources::SourceSnapshot &snapshot);
    void failed(const QString &error);
    void cancelled();

private:
    enum class Stage { Commit, Metadata };
    void request(const QUrl &url, Stage stage);
    void finishReply();
    void fail(const QString &error);
    void reset();

    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QTimer deadline;
    Stage stage = Stage::Commit;
    bool busy = false;
    QByteArray buffer;
    QString repository;
    QString ref;
    QString commit;
};

}
#endif
