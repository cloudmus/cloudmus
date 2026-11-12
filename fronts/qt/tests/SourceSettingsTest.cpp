#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "FakeBackend.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSettings.h"
#include "TestSupport.h"

namespace Tests {

class SourceSettingsTest : public QObject {
    Q_OBJECT

    const QString fake_ = QStringLiteral("fake");

private slots:
    void init() { installFakeBackendManifest(); }

    void whatARunningSourceSaysOfItself()
    {
        qputenv("CLOUDMUS_FAKE_SETTINGS", "1");
        QTemporaryDir dir;
        Config::Settings settings(dir.filePath("config.ini"));
        Rpc::SourceManager manager;
        ViewModel::SourceSettings sourceSettings(manager, settings);
        // Not running: all that's safe is that it may use the network.
        QVERIFY(!sourceSettings.facts(fake_).running);
        QVERIFY(sourceSettings.facts(fake_).usesNetwork);
        manager.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(sourceSettings.facts(fake_).running, 10000);
        qunsetenv("CLOUDMUS_FAKE_SETTINGS");
        const ViewModel::SourceSettings::Facts facts = sourceSettings.facts(fake_);
        QVERIFY(facts.hasSettings);
        QVERIFY(facts.downloadsStreams);
        QVERIFY(!facts.hasRadio);
        QVERIFY(!facts.needsAuth);

        const SettingsDescription described = Tests::await(sourceSettings.describe(fake_));
        QCOMPARE(described.fields.size(), 5);
        QVERIFY(ViewModel::SourceSettings::needRestart(described.fields, { QStringLiteral("flag") }));
        QVERIFY(!ViewModel::SourceSettings::needRestart(described.fields, { QStringLiteral("count") }));
        QCOMPARE(ViewModel::SourceSettings::initialValue(described.fields.first()).toString(), QStringLiteral("high"));

        QVERIFY(!Tests::await(sourceSettings.update(fake_, { { QStringLiteral("count"), 5 } })));
        const auto failure = Tests::await(sourceSettings.update(fake_, { { QStringLiteral("name"), QStringLiteral("bad") } }));
        QVERIFY(failure);
        QCOMPARE(failure->key, QStringLiteral("name"));
        QCOMPARE(failure->message, QStringLiteral("Not that name"));
        const SettingsDescription again = Tests::await(sourceSettings.describe(fake_));
        QCOMPARE(ViewModel::SourceSettings::initialValue(again.fields.at(1)).toInt(), 5);
    }

    void aConnectionChangedOnlyAsksForARestartIfItMatters()
    {
        QTemporaryDir dir;
        Config::Settings settings(dir.filePath("config.ini"));
        Rpc::SourceManager manager;
        ViewModel::SourceSettings sourceSettings(manager, settings);
        const QString direct = QLatin1String(Config::Settings::kDirectConnection);
        QVERIFY(sourceSettings.setConnection(fake_, direct));
        QCOMPARE(settings.sourceConnection(fake_), direct);
        QVERIFY(!sourceSettings.setConnection(fake_, direct));

        sourceSettings.setEnabled(fake_, false);
        QVERIFY(settings.disabledSources().contains(fake_));
        QVERIFY(!manager.isEnabled(fake_));
        // Switched off, it starts with whatever is set by then.
        QVERIFY(!sourceSettings.setConnection(fake_, QLatin1String(Config::Settings::kSystemConnection)));
        sourceSettings.setEnabled(fake_, true);
        QVERIFY(!settings.disabledSources().contains(fake_));
    }

    void onlySourcesOnAProxyThatChangedRestart()
    {
        QTemporaryDir dir;
        Config::Settings settings(dir.filePath("config.ini"));
        Rpc::SourceManager manager;
        ViewModel::SourceSettings sourceSettings(manager, settings);
        Config::ProxyConfig proxy { QStringLiteral("id1"), QStringLiteral("Home"), Config::ProxyConfig::Type::Http,
            QStringLiteral("host"), 1080, QString(), QString() };
        QVERIFY(sourceSettings.saveProxies({ proxy }).isEmpty());
        settings.setSourceConnection(fake_, proxy.id);

        proxy.name = QStringLiteral("Renamed");
        QVERIFY(sourceSettings.saveProxies({ proxy }).isEmpty());
        QCOMPARE(settings.proxies().first().name, QStringLiteral("Renamed"));
        proxy.port = 1081;
        QCOMPARE(sourceSettings.saveProxies({ proxy }), QSet<QString> { fake_ });
        QCOMPARE(sourceSettings.saveProxies({ }), QSet<QString> { fake_ });
        QVERIFY(settings.proxies().isEmpty());
    }
};

QObject* makeSourceSettingsTest() { return new SourceSettingsTest; }

} // namespace Tests

#include "SourceSettingsTest.moc"
