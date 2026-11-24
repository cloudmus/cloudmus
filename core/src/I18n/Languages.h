#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace I18n {

struct Language {
    QString code; // "ru" — also the suffix of its catalog, cloudmus_ru.qm
    QString nativeName; // as its speakers write it, whatever the current language
};

// The languages the front is translated into; English, the source
// language, included. In the order the settings list shows them.
const QList<Language>& supportedLanguages();

// The language to use: `setting` (a code from supportedLanguages()) if it
// names one, else — the empty "Auto" — the first of the system's
// `uiLanguages` (as QLocale::uiLanguages(): "ru-RU", "be_BY") we have, else
// English.
QString resolveLanguage(const QString& setting, const QStringList& uiLanguages);

} // namespace I18n
