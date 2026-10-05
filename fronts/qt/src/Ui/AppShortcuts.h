#pragma once

#include <QObject>
#include <QPointer>
#include <QShortcut>

#include <vector>

namespace Hotkeys {
class Dispatcher;
class Registry;
} // namespace Hotkeys

namespace Ui {

class MainWindow;
class WindowHost;

// The hotkeys that work while the app's own window is in front — every
// action's key, global or not (a global one the desktop delivers to the
// app instead of the window never gets here, so nothing fires twice).
// Follows the hotkey settings, and the window as WindowHost replaces it.
class AppShortcuts : public QObject {
    Q_OBJECT

public:
    AppShortcuts(WindowHost& windowHost, Hotkeys::Registry& registry, Hotkeys::Dispatcher& dispatcher,
        QObject* parent = nullptr);

private:
    void rebuild();

    WindowHost& windowHost_;
    Hotkeys::Registry& registry_;
    Hotkeys::Dispatcher& dispatcher_;
    // Pointers that notice a window taking its shortcuts along as it goes.
    std::vector<QPointer<QShortcut>> shortcuts_;
};

} // namespace Ui
