#pragma once

class QIcon;

namespace Integration {

// Windows: who the shell thinks this process is. One AppUserModelID ties
// the windows, the SMTC session and the tray's notifications to one app;
// registering it gives that app a name and an icon.
namespace AppIdentity {

inline constexpr wchar_t kAppUserModelId[] = L"CloudMus.CloudMus";

// Registers kAppUserModelId for the current user with the app's name and
// `icon`. Windows 11 shows tray balloons as toasts headed by the name it
// finds for the AppUserModelID — without this, the id itself
// ("CloudMus.CloudMus"). Also gives the installer's Start Menu shortcut the
// id, which the media flyout (SMTC) needs to name the app. Logs a warning on
// failure. Call once at startup, after QApplication (COM) and before the
// first notification.
void registerAppUserModelId(const QIcon& icon);

} // namespace AppIdentity

} // namespace Integration
