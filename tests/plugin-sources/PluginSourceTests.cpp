// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginMetadata.h"
#include "RegisteredResourceRegistry.h"
#include "SourceEntryResolver.h"
#include "PluginManagerDialog.h"

#include <QFile>
#include <QApplication>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <cstring>

using namespace PluginSources;

namespace {
QJsonObject fixture(const char *name)
{
    QFile file(QStringLiteral(PLUGIN_FIXTURES_DIR) + QLatin1Char('/') + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot load fixture");
    return QJsonDocument::fromJson(file.readAll()).object();
}

SourceSnapshot snapshot(const QJsonObject &metadata, const QString &repository)
{
    SourceSnapshot result;
    QString error;
    if (!PluginMetadata::parse(QJsonDocument(metadata).toJson(), repository, {}, &result, &error)) qFatal("%s", qPrintable(error));
    result.commit = QString(40, QLatin1Char('a'));
    return result;
}

struct Response { QByteArray bytes; int status = 200; int delay = 0; };

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, const Response &response, QObject *parent)
        : QNetworkReply(parent), content(response.bytes)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        setHeader(QNetworkRequest::ContentLengthHeader, content.size());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (response.status >= 400) setError(ContentNotFoundError, QStringLiteral("HTTP failure"));
        QTimer::singleShot(response.delay, this, [this] {
            if (isFinished()) return;
            emit metaDataChanged();
            emit readyRead();
            setFinished(true);
            emit finished();
        });
    }
    void abort() override { setFinished(true); setError(OperationCanceledError, QStringLiteral("Cancelled")); emit finished(); }
    qint64 bytesAvailable() const override { return content.size() - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const auto count = qMin(maxSize, qint64(content.size() - offset));
        if (count <= 0) return -1;
        std::memcpy(data, content.constData() + offset, size_t(count));
        offset += count;
        return count;
    }
private:
    QByteArray content;
    qint64 offset = 0;
};

class FakeNetwork : public QNetworkAccessManager
{
public:
    QVector<Response> responses;
    QVector<QUrl> requests;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request.url());
        if (responses.isEmpty()) qFatal("Unexpected network request");
        return new FakeReply(request, responses.takeFirst(), this);
    }
};

Response metadataResponse(const QJsonObject &object)
{
    const auto bytes = QJsonDocument(object).toJson();
    return {QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("file")}, {QStringLiteral("encoding"), QStringLiteral("base64")},
        {QStringLiteral("size"), bytes.size()}, {QStringLiteral("content"), QString::fromLatin1(bytes.toBase64())}}).toJson()};
}

Response commitResponse()
{
    return {QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("sha"), QString(40, QLatin1Char('a'))}}}).toJson()};
}
}

class PluginSourceTests : public QObject
{
    Q_OBJECT
private slots:
    void repositoryNormalization();
    void metadataDiscriminator();
    void invalidMetadataIsRejected();
    void registrationPersistenceAndMerging();
    void conflictsAndFailedWritesAreAtomic();
    void refreshReplacesOnlyItsOwnAssociations();
    void corruptCacheIsPreserved();
    void resolverPinsMetadataAndDoesNotInstall();
    void resolverFailureAndCancellation();
    void managerListsSearchesAndSharesRegistry();
    void managerRegistrationFlow_data();
    void managerRegistrationFlow();
    void localRegistrationRefreshAndPersistence();
    void localMetadataValidation();
};

void PluginSourceTests::repositoryNormalization()
{
    QString normalized, error;
    QVERIFY(PluginMetadata::normalizeRepository(QStringLiteral(" https://github.com/Owner/Repo.git/ "), &normalized, &error));
    QCOMPARE(normalized, QStringLiteral("https://github.com/Owner/Repo"));
    for (const QString &input : {QStringLiteral("http://github.com/a/b"), QStringLiteral("https://github.com/a/b/tree/main"),
        QStringLiteral("https://github.com/a/b?ref=main"), QStringLiteral("https://github.com@evil.example/a/b"),
        QStringLiteral("https://user@github.com/a/b"), QStringLiteral("https://github.com/a/%2e%2e"),
        QStringLiteral("https://github.com/a/.."), QStringLiteral("https://github.com/a/b#x")})
        QVERIFY2(!PluginMetadata::normalizeRepository(input, &normalized, &error), qPrintable(input));
}

void PluginSourceTests::metadataDiscriminator()
{
    const auto single = snapshot(fixture("markdown-preview.manifest.json"), QStringLiteral("https://github.com/example/markdown-preview"));
    QVERIFY(!single.isMarketplace());
    QCOMPARE(single.resources.size(), 1);
    const auto marketplace = snapshot(fixture("marketplace.manifest.json"), QStringLiteral("https://github.com/example/notepadnext-marketplace"));
    QVERIFY(marketplace.isMarketplace());
    QCOMPARE(marketplace.resources.size(), 3);
    auto empty = fixture("marketplace.manifest.json");
    empty.insert(QStringLiteral("list"), QJsonArray());
    QCOMPARE(snapshot(empty, QStringLiteral("https://github.com/example/notepadnext-marketplace")).resources.size(), 0);
}

void PluginSourceTests::invalidMetadataIsRejected()
{
    auto valid = fixture("markdown-preview.manifest.json");
    const auto rejects = [&](const QJsonObject &input) {
        SourceSnapshot untouched;
        untouched.repository = QStringLiteral("unchanged");
        QString error;
        QVERIFY(!PluginMetadata::parse(QJsonDocument(input).toJson(), QStringLiteral("https://github.com/example/markdown-preview"), {}, &untouched, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(untouched.repository, QStringLiteral("unchanged"));
    };
    for (const QJsonValue &list : {QJsonValue(), QJsonValue(true), QJsonValue(QStringLiteral("false")), QJsonValue(0)}) {
        auto invalid = valid; invalid.insert(QStringLiteral("list"), list); rejects(invalid);
    }
    auto invalid = valid; invalid.remove(QStringLiteral("list")); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("manifestVersion"), 2); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("unknownField"), true); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("version"), QStringLiteral("1.02.3")); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("version"), QStringLiteral("1.2.3-01")); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("name"), QStringLiteral(" ")); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("repository"), QStringLiteral("https://github.com/evil/replacement")); rejects(invalid);
    auto runtime = valid.value(QStringLiteral("runtime")).toObject();
    runtime.insert(QStringLiteral("entry"), QStringLiteral("../outside.js"));
    invalid = valid; invalid.insert(QStringLiteral("runtime"), runtime); rejects(invalid);
    auto capabilities = valid.value(QStringLiteral("capabilities")).toObject();
    capabilities.insert(QStringLiteral("optional"), capabilities.value(QStringLiteral("required")));
    invalid = valid; invalid.insert(QStringLiteral("capabilities"), capabilities); rejects(invalid);
    invalid = valid; invalid.insert(QStringLiteral("activationEvents"), QJsonArray{QStringLiteral("onCommand:other.plugin.run")}); rejects(invalid);
    auto marketplace = fixture("marketplace.manifest.json");
    auto entries = marketplace.value(QStringLiteral("list")).toArray();
    entries.append(entries.first()); marketplace.insert(QStringLiteral("list"), entries);
    SourceSnapshot result; QString error;
    QVERIFY(!PluginMetadata::parse(QJsonDocument(marketplace).toJson(), QStringLiteral("https://github.com/example/notepadnext-marketplace"), {}, &result, &error));
    entries = QJsonArray{fixture("marketplace.manifest.json")}; marketplace.insert(QStringLiteral("list"), entries);
    QVERIFY(!PluginMetadata::parse(QJsonDocument(marketplace).toJson(), QStringLiteral("https://github.com/example/notepadnext-marketplace"), {}, &result, &error));
    QVERIFY(!PluginMetadata::parse(QByteArray(PluginMetadata::MaxMetadataBytes + 1, ' '), QStringLiteral("https://github.com/example/markdown-preview"), {}, &result, &error));
    QVERIFY(!PluginMetadata::parse(QByteArray(33, '[') + QByteArray(33, ']'), QStringLiteral("https://github.com/example/markdown-preview"), {}, &result, &error));
}

void PluginSourceTests::registrationPersistenceAndMerging()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("plugins/sources.json"));
    RegisteredResourceRegistry registry(path);
    QString error;
    QVERIFY(registry.load(&error));
    const auto direct = snapshot(fixture("markdown-preview.manifest.json"), QStringLiteral("https://github.com/example/markdown-preview"));
    const auto marketplace = snapshot(fixture("marketplace.manifest.json"), QStringLiteral("https://github.com/example/notepadnext-marketplace"));
    QVERIFY(registry.registerSource(direct, &error));
    QVERIFY(registry.registerSource(direct, &error));
    QCOMPARE(registry.sources().size(), 1);
    QVERIFY(registry.registerSource(marketplace, &error));
    QCOMPARE(registry.resources().size(), 3);
    for (const auto &resource : registry.resources()) if (resource.id == direct.resources.first().id) QCOMPARE(resource.sourceIds.size(), 2);
    RegisteredResourceRegistry reopened(path);
    QVERIFY2(reopened.load(&error), qPrintable(error));
    QCOMPARE(reopened.resources().size(), 3);
    QCOMPARE(reopened.sources().first().digest, direct.digest);
    QVERIFY(reopened.removeSource(marketplace.key(), &error));
    QCOMPARE(reopened.resources().size(), 1);
    QVERIFY(reopened.removeSource(direct.key(), &error));
    QCOMPARE(reopened.resources().size(), 0);
    QCOMPARE(reopened.sources().size(), 0);
}

void PluginSourceTests::conflictsAndFailedWritesAreAtomic()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("sources.json"));
    RegisteredResourceRegistry registry(path);
    QString error;
    const auto direct = snapshot(fixture("markdown-preview.manifest.json"), QStringLiteral("https://github.com/example/markdown-preview"));
    QVERIFY(registry.registerSource(direct, &error));
    auto metadata = fixture("marketplace.manifest.json");
    auto entries = metadata.value(QStringLiteral("list")).toArray();
    auto first = entries.first().toObject();
    first.insert(QStringLiteral("repository"), QStringLiteral("https://github.com/another/publisher"));
    entries[0] = first; metadata.insert(QStringLiteral("list"), entries);
    const auto conflict = snapshot(metadata, QStringLiteral("https://github.com/example/notepadnext-marketplace"));
    QFile before(path); QVERIFY(before.open(QIODevice::ReadOnly)); const auto original = before.readAll(); before.close();
    QSignalSpy changed(&registry, &RegisteredResourceRegistry::changed);
    QVERIFY(!registry.registerSource(conflict, &error));
    QCOMPARE(changed.size(), 0);
    QCOMPARE(registry.sources().size(), 1);
    QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(), original);
    RegisteredResourceRegistry unwritable(directory.path()); // A directory cannot be replaced by QSaveFile.
    QVERIFY(!unwritable.registerSource(direct, &error));
    QCOMPARE(unwritable.resources().size(), 0);
}

void PluginSourceTests::corruptCacheIsPreserved()
{
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("sources.json")));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken JSON"); file.close();
    RegisteredResourceRegistry registry(file.fileName());
    QString error;
    QVERIFY(!registry.load(&error));
    const auto direct = snapshot(fixture("markdown-preview.manifest.json"), QStringLiteral("https://github.com/example/markdown-preview"));
    QVERIFY(!registry.registerSource(direct, &error));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken JSON"));
}

void PluginSourceTests::refreshReplacesOnlyItsOwnAssociations()
{
    QTemporaryDir directory;
    RegisteredResourceRegistry registry(directory.filePath(QStringLiteral("sources.json")));
    QString error;
    const auto direct = snapshot(fixture("markdown-preview.manifest.json"), QStringLiteral("https://github.com/example/markdown-preview"));
    const auto marketplace = snapshot(fixture("marketplace.manifest.json"), QStringLiteral("https://github.com/example/notepadnext-marketplace"));
    QVERIFY(registry.registerSource(direct, &error));
    QVERIFY(registry.registerSource(marketplace, &error));
    auto empty = marketplace.metadata;
    empty.insert(QStringLiteral("list"), QJsonArray());
    const auto refreshed = snapshot(empty, marketplace.repository);
    QVERIFY(registry.registerSource(refreshed, &error));
    QCOMPARE(registry.sources().size(), 2);
    QCOMPARE(registry.resources().size(), 1);
    QCOMPARE(registry.resources().first().sourceIds, QStringList{direct.key()});
    auto invalid = refreshed;
    invalid.metadata.insert(QStringLiteral("list"), true);
    QVERIFY(!registry.registerSource(invalid, &error));
    QCOMPARE(registry.sources().size(), 2);
    QCOMPARE(registry.resources().size(), 1);
    auto malformed = direct;
    malformed.resources.clear(); // Caller cannot inject/drop validated resources.
    QVERIFY(registry.registerSource(malformed, &error));
    QCOMPARE(registry.resources().size(), 1);
}

void PluginSourceTests::resolverPinsMetadataAndDoesNotInstall()
{
    FakeNetwork network;
    network.responses = {commitResponse(), metadataResponse(fixture("markdown-preview.manifest.json"))};
    SourceEntryResolver resolver(nullptr, &network);
    QSignalSpy resolved(&resolver, &SourceEntryResolver::resolved);
    QSignalSpy failed(&resolver, &SourceEntryResolver::failed);
    resolver.resolve(QStringLiteral("https://github.com/example/markdown-preview"), QStringLiteral("release/next"));
    QTRY_COMPARE(resolved.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(network.requests.size(), 2);
    QCOMPARE(network.requests[0].path(), QStringLiteral("/repos/example/markdown-preview/commits"));
    QCOMPARE(QUrlQuery(network.requests[0]).queryItemValue(QStringLiteral("sha")), QStringLiteral("release/next"));
    QCOMPARE(network.requests[1].path(), QStringLiteral("/repos/example/markdown-preview/contents/notepadnext-plugin.json"));
    QCOMPARE(QUrlQuery(network.requests[1]).queryItemValue(QStringLiteral("ref")), QString(40, QLatin1Char('a')));
    const auto source = qvariant_cast<SourceSnapshot>(resolved.first().first());
    QCOMPARE(source.resources.size(), 1);
    QCOMPARE(source.commit, QString(40, QLatin1Char('a')));
    QVERIFY(!resolver.isBusy());
    // There is no package/install/process API in this subsystem; only two
    // bounded metadata requests occur, and resolution does not write a registry.
}

void PluginSourceTests::resolverFailureAndCancellation()
{
    FakeNetwork network;
    SourceEntryResolver resolver(nullptr, &network);
    QSignalSpy failed(&resolver, &SourceEntryResolver::failed);
    QSignalSpy cancelled(&resolver, &SourceEntryResolver::cancelled);
    QSignalSpy resolved(&resolver, &SourceEntryResolver::resolved);
    network.responses = {commitResponse(), {QByteArray("{}"), 404}};
    resolver.resolve(QStringLiteral("https://github.com/example/markdown-preview"));
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("notepadnext-plugin.json")));
    QVERIFY(!resolver.isBusy());
    network.responses = {{QByteArray("{}"), 200, 50}};
    resolver.resolve(QStringLiteral("https://github.com/example/markdown-preview"));
    resolver.cancel();
    QCOMPARE(cancelled.size(), 1);
    QTest::qWait(80);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(resolved.size(), 0);
    QVERIFY(!resolver.isBusy());
    network.responses = {{QByteArray(2 * 1024 * 1024 + 1, ' '), 200}};
    resolver.resolve(QStringLiteral("https://github.com/example/markdown-preview"));
    QTRY_COMPARE(failed.size(), 2);
    QVERIFY(!resolver.isBusy());
}

void PluginSourceTests::managerListsSearchesAndSharesRegistry()
{
    QTemporaryDir directory;
    RegisteredResourceRegistry registry(directory.filePath(QStringLiteral("sources.json")));
    PluginManagerDialog first(&registry), second(&registry);
    auto *list = first.findChild<QListWidget *>(QStringLiteral("discoverPlugins"));
    auto *other = second.findChild<QListWidget *>(QStringLiteral("discoverPlugins"));
    auto *tabs = first.findChild<QTabWidget *>(QStringLiteral("pluginManagerTabs"));
    QCOMPARE(list->count(), 0);
    QCOMPARE(tabs->currentIndex(), 1);
    QString error;
    const auto marketplace = snapshot(fixture("marketplace.manifest.json"), QStringLiteral("https://github.com/example/notepadnext-marketplace"));
    QVERIFY(registry.registerSource(marketplace, &error));
    QCOMPARE(list->count(), 3);
    QCOMPARE(other->count(), 3);
    auto *search = first.findChild<QLineEdit *>(QStringLiteral("resourceSearch"));
    search->setText(QStringLiteral("Markdown"));
    QCOMPARE(list->count(), 1);
    search->setText(QStringLiteral("no match"));
    QCOMPARE(list->count(), 0);
    search->clear();
    QCOMPARE(list->count(), 3);
    QVERIFY(registry.removeSource(marketplace.key(), &error));
    QCOMPARE(list->count(), 0);
    QCOMPARE(other->count(), 0);
}

void PluginSourceTests::managerRegistrationFlow_data()
{
    QTest::addColumn<QString>("fixtureName");
    QTest::addColumn<QString>("repository");
    QTest::addColumn<int>("expectedResources");
    QTest::addColumn<bool>("confirm");
    QTest::newRow("direct-plugin") << QStringLiteral("markdown-preview.manifest.json")
        << QStringLiteral("https://github.com/example/markdown-preview") << 1 << true;
    QTest::newRow("marketplace") << QStringLiteral("marketplace.manifest.json")
        << QStringLiteral("https://github.com/example/notepadnext-marketplace") << 3 << true;
    QTest::newRow("cancel-confirmation") << QStringLiteral("marketplace.manifest.json")
        << QStringLiteral("https://github.com/example/notepadnext-marketplace") << 0 << false;
}

void PluginSourceTests::managerRegistrationFlow()
{
    QFETCH(QString, fixtureName);
    QFETCH(QString, repository);
    QFETCH(int, expectedResources);
    QFETCH(bool, confirm);
    QTemporaryDir directory;
    const QString storage = directory.filePath(QStringLiteral("sources.json"));
    RegisteredResourceRegistry registry(storage);
    FakeNetwork network;
    network.responses = {commitResponse(), metadataResponse(fixture(qPrintable(fixtureName)))};
    PluginManagerDialog dialog(&registry, nullptr, &network);
    dialog.show();
    // Exercise the actual modal input and confirmation, not its private slots.
    QTimer confirmationTimer;
    bool confirmationSeen = false;
    connect(&confirmationTimer, &QTimer::timeout, &dialog, [&] {
        auto *box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!box || box->objectName()!="registerSourceReview") return;
        confirmationSeen = true;
        confirmationTimer.stop();
        box->findChild<QDialogButtonBox*>()->button(confirm?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
    });
    confirmationTimer.start(10);
    QTimer::singleShot(0, &dialog, [&] {
        auto *entry = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(entry);
        auto *input = entry->findChild<QLineEdit *>(QStringLiteral("repositoryInput"));
        QVERIFY(input);
        input->setText(repository);
        entry->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    dialog.findChild<QPushButton *>(QStringLiteral("registerResources"))->click();
    QTRY_VERIFY(confirmationSeen);
    QCOMPARE(registry.resources().size(), expectedResources);
    QCOMPARE(registry.sources().size(), confirm ? 1 : 0);
    QCOMPARE(QFile::exists(storage), confirm);
    QCOMPARE(dialog.findChild<QTabWidget *>(QStringLiteral("pluginManagerTabs"))->currentIndex(), 1);
    QCOMPARE(dialog.findChild<QListWidget *>(QStringLiteral("discoverPlugins"))->count(), expectedResources);
    QCOMPARE(network.requests.size(), 2);
}

void PluginSourceTests::localRegistrationRefreshAndPersistence()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("notepadnext-plugin.json");
    auto metadata = fixture("markdown-preview.manifest.json");
    metadata.remove("repository");
    const auto write = [&] {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(QJsonDocument(metadata).toJson()) > 0;
    };
    QVERIFY(write());
    const auto storage = directory.filePath("sources.json");
    RegisteredResourceRegistry registry(storage);
    FakeNetwork network;
    PluginManagerDialog dialog(&registry, nullptr, &network);
    dialog.show();
    QTimer confirmation;
    bool confirmed = false;
    connect(&confirmation, &QTimer::timeout, &dialog, [&] {
        if (auto *box = qobject_cast<QDialog *>(QApplication::activeModalWidget());box&&box->objectName()=="registerSourceReview") {
            confirmed = true;
            confirmation.stop();
            box->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        }
    });
    confirmation.start(10);
    QTimer::singleShot(0, &dialog, [&] {
        auto *entry = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(entry);
        entry->findChild<QComboBox *>("sourceKindInput")->setCurrentIndex(1);
        entry->findChild<QLineEdit *>("localMetadataInput")->setText(path);
        entry->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    dialog.findChild<QPushButton *>("registerResources")->click();
    QVERIFY(confirmed);
    QCOMPARE(registry.sources().size(), 1);
    QCOMPARE(registry.resources().size(), 1);
    const auto source = registry.sources()[0];
    QVERIFY(source.isLocal());
    QVERIFY(source.commit.isEmpty());
    QCOMPARE(source.localPath, QFileInfo(path).canonicalFilePath());
    QCOMPARE(source.repository, QUrl::fromLocalFile(source.localPath).toString(QUrl::FullyEncoded));
    QCOMPARE(registry.resources()[0].sourceIds, QStringList{source.key()});
    QCOMPARE(dialog.findChild<QTabWidget *>("pluginManagerTabs")->currentIndex(), 1);
    QCOMPARE(dialog.findChild<QListWidget *>("discoverPlugins")->count(), 1);
    QVERIFY(network.requests.isEmpty());
    metadata.insert("version", "1.0.1");
    QVERIFY(write());
    dialog.findChild<QTabWidget *>("pluginManagerTabs")->setCurrentIndex(2);
    dialog.findChild<QPushButton *>("refreshSourceMetadata")->click();
    QCOMPARE(registry.resources()[0].metadata["version"].toString(), QString("1.0.1"));
    // Refresh Catalog covers every source, including failures outside the
    // selected row, and retains the unavailable source's cached resources.
    const auto secondPath=directory.filePath("second.json");
    auto secondBytes=QJsonDocument(metadata).toJson();secondBytes.replace(metadata["id"].toString().toUtf8(),"example.second");auto secondMetadata=QJsonDocument::fromJson(secondBytes).object();
    QFile secondFile(secondPath);QVERIFY(secondFile.open(QIODevice::WriteOnly));secondFile.write(QJsonDocument(secondMetadata).toJson());secondFile.close();
    SourceSnapshot secondSnapshot;QString secondError;
    QVERIFY2(PluginMetadata::readLocalFile(secondPath,&secondSnapshot,&secondError),qPrintable(secondError));QVERIFY(registry.registerSource(secondSnapshot,&secondError));
    secondMetadata.insert("version","1.0.2");QVERIFY(secondFile.open(QIODevice::WriteOnly|QIODevice::Truncate));secondFile.write(QJsonDocument(secondMetadata).toJson());secondFile.close();
    QVERIFY(QFile::remove(path));
    dialog.findChild<QPushButton *>("refreshPluginCatalog")->click();
    auto *status=dialog.findChild<QLabel*>("registrationStatus");
    QTRY_VERIFY(status->text().contains("1 sources updated, 1 unavailable"));
    QCOMPARE(registry.resources().size(),2);
    bool secondUpdated=false;for(const auto &resource:registry.resources())if(resource.id=="example.second")secondUpdated=resource.metadata["version"]=="1.0.2";QVERIFY(secondUpdated);
    QVERIFY(registry.removeSource(secondSnapshot.key(),&secondError));
    QVERIFY(registry.sources()[0].digest != source.digest);
    QVERIFY(network.requests.isEmpty());
    // Cached metadata remains inspectable when the author's directory is gone.
    QVERIFY(!QFile::exists(path));
    dialog.findChild<QPushButton *>("refreshSourceMetadata")->click();
    QCOMPARE(registry.resources()[0].metadata["version"].toString(), QString("1.0.1"));
    RegisteredResourceRegistry restored(storage);
    QString error;
    QVERIFY2(restored.load(&error), qPrintable(error));
    QCOMPARE(restored.sources()[0].localPath, source.localPath);
    QCOMPARE(restored.resources()[0].sourceIds, QStringList{source.key()});
    QCOMPARE(restored.resources()[0].metadata["version"].toString(), QString("1.0.1"));
}

void PluginSourceTests::localMetadataValidation()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("notepadnext-plugin.json");
    auto metadata = fixture("markdown-preview.manifest.json");
    metadata.insert("repository", "https://github.com/example/markdown-preview");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(metadata).toJson());
    file.close();
    SourceSnapshot source;
    QString error, canonical;
    QVERIFY2(PluginMetadata::readLocalFile(path, &source, &error), qPrintable(error));
    QCOMPARE(source.resources[0].repository, QString("https://github.com/example/markdown-preview"));
    QVERIFY(!PluginMetadata::normalizeRepository(QUrl::fromLocalFile(path).toString(), &canonical, &error));
    for (const auto &origin : {QString("file://remote/path.json"), QString("file:///tmp/a.json?x"), QString("file:///tmp/a.json#x"), QString("file:relative.json")})
        QVERIFY2(!PluginMetadata::normalizeOrigin(origin, &canonical, &error), qPrintable(origin));
    QVERIFY(!PluginMetadata::sameOrigin("file:///tmp/Plugin.json", "file:///tmp/plugin.json"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArray(PluginMetadata::MaxMetadataBytes + 1, ' '));
    file.close();
    const auto untouched = source.key();
    QVERIFY(!PluginMetadata::readLocalFile(path, &source, &error));
    QCOMPARE(source.key(), untouched);
}

QTEST_MAIN(PluginSourceTests)
#include "PluginSourceTests.moc"
