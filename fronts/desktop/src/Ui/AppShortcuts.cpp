#include "AppShortcuts.h"

#include <QShortcut>

#include "Dispatcher.h"
#include "MainWindow.h"
#include "Registry.h"
#include "WindowHost.h"

namespace Ui {

AppShortcuts::AppShortcuts(
    WindowHost& windowHost, Hotkeys::Registry& registry, Hotkeys::Dispatcher& dispatcher, QObject* parent)
    : QObject(parent)
    , windowHost_(windowHost)
    , registry_(registry)
    , dispatcher_(dispatcher)
{
    connect(&windowHost_, &WindowHost::windowCreated, this, &AppShortcuts::rebuild);
    connect(&registry_, &Hotkeys::Registry::bindingsChanged, this, &AppShortcuts::rebuild);
    rebuild();
}

void AppShortcuts::rebuild()
{
    // The old ones are children of a window that may be gone already.
    for (const QPointer<QShortcut>& shortcut : shortcuts_)
        delete shortcut.data();
    shortcuts_.clear();

    MainWindow* window = windowHost_.window();
    if (window == nullptr)
        return;
    for (const Hotkeys::Binding& binding : registry_.bindings()) {
        if (binding.key.isEmpty())
            continue;
        auto* shortcut = new QShortcut(binding.key, window);
        shortcut->setContext(Qt::ApplicationShortcut);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, this, [this, action = binding.action]() {
            emit triggered();
            dispatcher_.trigger(action);
        });
        shortcuts_.push_back(shortcut);
    }
}

} // namespace Ui
