#pragma once

#include <QHash>
#include <QList>

#include "GlobalHotkeys.h"

namespace Integration {

// Carbon's RegisterEventHotKey: still the way for an app to have a key
// whichever app is in front, and unlike an event tap it needs no
// Accessibility permission. The presses come to the app's event target on
// the main thread.
class GlobalHotkeysMac : public GlobalHotkeys {
    Q_OBJECT

public:
    explicit GlobalHotkeysMac(QObject* parent = nullptr);
    ~GlobalHotkeysMac() override;

    Hotkeys::Registry::GlobalSupport support() const override { return Hotkeys::Registry::GlobalSupport::Direct; }
    void setEntries(const QList<Entry>& entries) override;

    // For the Carbon handler: the press of the hotkey registered as `number`.
    void pressed(quint32 number);

private:
    void unregisterAll();

    void* handler_ = nullptr; // EventHandlerRef
    QList<void*> refs_; // EventHotKeyRef
    // Registered hotkeys by the number given to RegisterEventHotKey.
    QHash<quint32, QString> registered_;
    quint32 nextNumber_ = 1;
};

} // namespace Integration
