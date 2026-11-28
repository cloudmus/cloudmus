#include "Localizer.h"

#include <QLocale>
#include <QSettings>

#include "Backend.h"
#include "Languages.h"

namespace Ui {

namespace {
const QString kKey = QStringLiteral("ui/language");
}

Localizer::Localizer(Backend& backend, QQmlEngine& engine, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , engine_(engine)
    , setting_(QSettings().value(kKey).toString())
{
    translator_.setLanguage(setting_);
    connect(&backend_, &Backend::readyChanged, this, &Localizer::connectCore);
    connectCore();
}

QVariantList Localizer::languages() const
{
    QVariantList list;
    for (const I18n::Language& language : I18n::supportedLanguages())
        list.append(
            QVariantMap { { QStringLiteral("code"), language.code }, { QStringLiteral("name"), language.nativeName } });
    return list;
}

void Localizer::connectCore()
{
    QObject* screen = backend_.settingsScreen();
    if (screen == screen_.data())
        return;
    screen_ = screen;
    if (!screen)
        return;
    // The core can't tell the system's language on Android.
    QMetaObject::invokeMethod(screen, "setSystemLanguages", Q_ARG(QStringList, QLocale::system().uiLanguages()));
    // The core's language is the one saved; whatever it says wins.
    connect(screen, SIGNAL(languageChanged()), this, SLOT(syncFromCore()));
    syncFromCore();
}

void Localizer::syncFromCore()
{
    if (screen_)
        adopt(screen_->property("language").toString());
}

void Localizer::adopt(const QString& code)
{
    if (code == setting_)
        return;
    setting_ = code;
    QSettings().setValue(kKey, code);
    translator_.setLanguage(code);
    // Installing a translator is what makes the engine's bindings re-read
    // their qsTr(), through retranslate().
    engine_.retranslate();
    emit settingChanged();
}

void Localizer::setSetting(const QString& code)
{
    adopt(code);
    if (screen_)
        QMetaObject::invokeMethod(screen_, "setLanguage", Q_ARG(QString, code));
}

} // namespace Ui
