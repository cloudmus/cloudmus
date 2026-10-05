#include "GlobalHotkeys.h"

#include <QGuiApplication>

#ifdef Q_OS_WIN
#include "GlobalHotkeysWin.h"
#else
#include "GlobalHotkeysKGlobalAccel.h"
#include "GlobalHotkeysPortal.h"
#ifdef CLOUDMUS_HOTKEYS_X11
#include "GlobalHotkeysX11.h"
#endif
#endif

namespace Integration {

namespace {
// What is used where no mechanism is available.
class NoGlobalHotkeys : public GlobalHotkeys {
public:
    explicit NoGlobalHotkeys(const QString& reason)
        : reason_(reason)
    {
    }

    Hotkeys::Registry::GlobalSupport support() const override { return Hotkeys::Registry::GlobalSupport::None; }
    QString unavailableReason() const override { return reason_; }
    void setEntries(const QList<Entry>&) override { }

private:
    QString reason_;
};
} // namespace

std::unique_ptr<GlobalHotkeys> createGlobalHotkeys()
{
#ifdef Q_OS_WIN
    return std::make_unique<GlobalHotkeysWin>();
#else
    // CLOUDMUS_HOTKEYS_BACKEND=kglobalaccel|portal|x11|none picks one
    // regardless of the desktop — to try the others where they exist
    // (KDE's portal speaks GlobalShortcuts too).
    const QString forced = qEnvironmentVariable("CLOUDMUS_HOTKEYS_BACKEND");
    const auto wanted = [&forced](const char* name) { return forced.isEmpty() || forced == QLatin1String(name); };

    if (wanted("kglobalaccel") && GlobalHotkeysKGlobalAccel::available())
        return std::make_unique<GlobalHotkeysKGlobalAccel>();
    if (wanted("portal")) {
        if (const uint version = GlobalHotkeysPortal::availableVersion(); version > 0)
            return std::make_unique<GlobalHotkeysPortal>(version);
    }
#ifdef CLOUDMUS_HOTKEYS_X11
    if (wanted("x11") && GlobalHotkeysX11::available())
        return std::make_unique<GlobalHotkeysX11>();
#endif
    return std::make_unique<NoGlobalHotkeys>(QCoreApplication::translate(
        "Integration::GlobalHotkeys", "This desktop has no way for an app to have global shortcuts."));
#endif
}

} // namespace Integration
