// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINWEBVIEWSERVICE_H
#define PLUGINWEBVIEWSERVICE_H
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <memory>
#include <functional>
class QMainWindow;
class QWebEnginePage;
namespace PluginDocuments { class DocumentBridge; }
namespace PluginWebviews {
void registerScheme(); // call before QApplication construction
// Reserve Qt's texture composition before the editor's first native surface.
void prepareWindow(QMainWindow *window);
class WebviewService : public QObject {
    Q_OBJECT
public:
    WebviewService(const QString &pluginId, const QString &packageRoot, QMainWindow *window, PluginDocuments::DocumentBridge *documents, QObject *parent=nullptr, const QString &owner={});
    ~WebviewService() override;
    QJsonObject create(const QString &viewType, const QString &title, const QString &position, QJsonObject *error);
    bool reveal(const QString &panelId, bool preserveFocus, QJsonObject *error);
    bool setHtml(const QString &panelId, const QString &html, const QJsonObject &renderContext, QJsonObject *error);
    // One bounded background preparation per panel. Publication still occurs
    // on the GUI thread after rechecking document and panel ownership.
    void setHtmlAsync(const QString &panelId, const QString &html, const QJsonObject &renderContext,
                      std::function<void(const QJsonObject &)> completed);
    QString mapResource(const QString &panelId, const QJsonObject &resource, QJsonObject *error);
    bool postMessage(const QString &panelId, const QJsonValue &message, QJsonObject *error);
    QJsonObject createCompatible(const QString &panelId, const QString &viewType, const QString &title, bool enableScripts, const QStringList &localResourceRoots, QJsonObject *error);
    bool setDocument(const QString &panelId, const QString &html, QJsonObject *error);
    QString mapAsset(const QString &panelId, const QString &path, QJsonObject *error);
    bool setTitle(const QString &panelId, const QString &title, QJsonObject *error);
    void dispose(const QString &panelId);
    void clear();
    void setPublished(bool published);
    int panelCount() const;
    int resourceCount() const;
    QWebEnginePage *page(const QString &panelId) const;
    QUrl pageUrl(const QString &panelId) const;
signals:
    void panelsChanged();
    void event(const QString &panelId, const QString &type, const QJsonValue &payload);
    void externalLinkRequested(const QUrl &url);
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString appearanceCss() const;
    void refreshAppearance();
    void applyAppearance(const QString &panelId);
    void refreshPanelContext(const QString &panelId);
    struct Private;
    QJsonObject createPanel(const QString &panelId, const QString &viewType, const QString &title, const QString &position, bool compatible, QJsonObject *error);
    void viewState(const QString &panelId);
    QJsonObject renderContextError(const QString &panelId, const QJsonObject &context) const;
    void publishHtml(const QString &panelId, const QByteArray &document, const QString &revision);
    std::unique_ptr<Private> d;
};
}
#endif
