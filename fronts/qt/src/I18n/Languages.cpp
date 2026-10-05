#include "Languages.h"

#include <QRegularExpression>

namespace I18n {

const QList<Language>& supportedLanguages()
{
    static const QList<Language> languages {
        { QStringLiteral("en"), QStringLiteral("English") },
        { QStringLiteral("be"), QStringLiteral("Беларуская") },
        { QStringLiteral("de"), QStringLiteral("Deutsch") },
        { QStringLiteral("es"), QStringLiteral("Español") },
        { QStringLiteral("fr"), QStringLiteral("Français") },
        { QStringLiteral("it"), QStringLiteral("Italiano") },
        { QStringLiteral("ru"), QStringLiteral("Русский") },
    };
    return languages;
}

namespace {

bool isSupported(const QString& code)
{
    for (const Language& language : supportedLanguages()) {
        if (language.code == code)
            return true;
    }
    return false;
}

} // namespace

QString resolveLanguage(const QString& setting, const QStringList& uiLanguages)
{
    if (isSupported(setting))
        return setting;
    for (const QString& tag : uiLanguages) {
        // "ru-RU", "be_BY", "de-Latn-DE": the language is what comes first.
        const QString code = tag.section(QRegularExpression(QStringLiteral("[-_]")), 0, 0).toLower();
        if (isSupported(code))
            return code;
    }
    return QStringLiteral("en");
}

} // namespace I18n
