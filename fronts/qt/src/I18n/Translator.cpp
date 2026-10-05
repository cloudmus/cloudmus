#include "Translator.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>

#include "Languages.h"

namespace I18n {

namespace {

Q_LOGGING_CATEGORY(lcI18n, "cloudmus.i18n")

} // namespace

Translator::Translator(QString resourcePrefix, QObject* parent)
    : QObject(parent)
    , resourcePrefix_(std::move(resourcePrefix))
{
}

void Translator::setLanguage(const QString& setting)
{
    const QString code = resolveLanguage(setting, QLocale::system().uiLanguages());
    if (code == language_)
        return;

    QCoreApplication::removeTranslator(&appTranslator_);
    QCoreApplication::removeTranslator(&qtTranslator_);
    // English is the source language: nothing to load.
    if (code != QLatin1String("en")) {
        if (appTranslator_.load(QStringLiteral("cloudmus_%1.qm").arg(code), resourcePrefix_))
            QCoreApplication::installTranslator(&appTranslator_);
        else
            qCWarning(lcI18n) << "no catalog for" << code << "in" << resourcePrefix_;
        // Qt's own catalog: from its installation, else one shipped with
        // the app next to ours (Windows builds, say). Belarusian has none.
        if (qtTranslator_.load(
                QStringLiteral("qtbase_%1.qm").arg(code), QLibraryInfo::path(QLibraryInfo::TranslationsPath))
            || qtTranslator_.load(QStringLiteral("qtbase_%1.qm").arg(code), resourcePrefix_))
            QCoreApplication::installTranslator(&qtTranslator_);
    }
    QLocale::setDefault(QLocale(code));
    language_ = code;
    emit languageChanged(code);
}

} // namespace I18n
