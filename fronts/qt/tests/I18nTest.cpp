#include <QCoreApplication>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>

#include "FakeBackend.h"
#include "Languages.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "TestSupport.h"
#include "Translator.h"

namespace Tests {

namespace {
Rpc::BackendManifest fakeManifest()
{
    Rpc::BackendManifest manifest;
    manifest.id = QStringLiteral("fake");
    manifest.name = QStringLiteral("Fake Source");
    manifest.argv = fakeBackendArgv();
    return manifest;
}
} // namespace

class I18nTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { QLocale::setDefault(QLocale::c()); }

    void aChosenLanguageWins()
    {
        QCOMPARE(I18n::resolveLanguage(QStringLiteral("fr"), { QStringLiteral("ru-RU") }), QStringLiteral("fr"));
    }

    void autoTakesTheFirstSystemLanguageWeHave()
    {
        const QStringList system { QStringLiteral("ja-JP"), QStringLiteral("be_BY"), QStringLiteral("ru-RU") };
        QCOMPARE(I18n::resolveLanguage(QString(), system), QStringLiteral("be"));
        QCOMPARE(I18n::resolveLanguage(QString(), { QStringLiteral("de-Latn-AT") }), QStringLiteral("de"));
    }

    void anUnknownLanguageFallsBackToEnglish()
    {
        QCOMPARE(I18n::resolveLanguage(QString(), { QStringLiteral("ja-JP") }), QStringLiteral("en"));
        QCOMPARE(I18n::resolveLanguage(QStringLiteral("xx"), { }), QStringLiteral("en"));
    }

    void everyLanguageTheFrontClaimsIsListed()
    {
        QStringList codes;
        for (const I18n::Language& language : I18n::supportedLanguages())
            codes << language.code;
        codes.sort();
        QCOMPARE(codes,
            (QStringList { QStringLiteral("be"), QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("es"),
                QStringLiteral("fr"), QStringLiteral("it"), QStringLiteral("ru") }));
    }

    void theTranslatorSetsTheLocaleAndAnnouncesTheChange()
    {
        I18n::Translator translator(QStringLiteral(":/no-such-catalogs"));
        QSignalSpy spy(&translator, &I18n::Translator::languageChanged);
        translator.setLanguage(QStringLiteral("de"));
        QCOMPARE(translator.language(), QStringLiteral("de"));
        QCOMPARE(QLocale().language(), QLocale::German);
        QCOMPARE(spy.count(), 1);
        translator.setLanguage(QStringLiteral("de"));
        QCOMPARE(spy.count(), 1); // already in effect
        translator.setLanguage(QStringLiteral("en"));
        QCOMPARE(spy.count(), 2);
    }

    void aBackendIsToldTheLanguageAtStartAndLater()
    {
        Rpc::RpcClient client(fakeManifest());
        await(client.start(QProcessEnvironment::systemEnvironment(), QStringLiteral("ru")));
        QCOMPARE(client.sourceDescription(), QStringLiteral("locale=ru"));

        await(client.setLanguage(QStringLiteral("de")));
        const QJsonObject current = await(client.callRaw(QStringLiteral("fake.currentLocale"), { }, 5000));
        QCOMPARE(current.value(QStringLiteral("locale")).toString(), QStringLiteral("de"));
        await(client.shutdown());
    }

    void noLocaleMeansTheBackendsDefault()
    {
        Rpc::RpcClient client(fakeManifest());
        await(client.start(QProcessEnvironment::systemEnvironment()));
        QCOMPARE(client.sourceDescription(), QStringLiteral("locale=en"));
        await(client.shutdown());
    }
};

QObject* makeI18nTest() { return new I18nTest; }

} // namespace Tests

#include "I18nTest.moc"
