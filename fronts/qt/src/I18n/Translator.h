#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

namespace I18n {

// The language of the front's own texts: installs the matching catalogs
// (the app's, and Qt's for its standard buttons and dialogs) and the
// default QLocale, so dates and numbers follow. Installing a translator
// makes Qt send every widget a LanguageChange event; whatever builds its
// texts up front re-reads them then.
class Translator : public QObject {
    Q_OBJECT

public:
    // `resourcePrefix`: where the app's cloudmus_<code>.qm are, a Qt
    // resource path normally.
    explicit Translator(QString resourcePrefix = QStringLiteral(":/i18n"), QObject* parent = nullptr);

    // `setting`: a language code, or empty to follow the system.
    void setLanguage(const QString& setting);
    // The language in effect — a code from supportedLanguages().
    const QString& language() const { return language_; }

signals:
    void languageChanged(const QString& code);

private:
    QString resourcePrefix_;
    QString language_;
    QTranslator appTranslator_;
    QTranslator qtTranslator_;
};

} // namespace I18n
