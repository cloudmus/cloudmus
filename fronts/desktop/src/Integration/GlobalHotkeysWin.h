#pragma once

#include <QAbstractNativeEventFilter>

#include "GlobalHotkeys.h"

namespace Integration {

// RegisterHotKey: Windows posts WM_HOTKEY to the thread for the registered
// keys, whichever window has the focus.
class GlobalHotkeysWin : public GlobalHotkeys, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalHotkeysWin(QObject* parent = nullptr);
    ~GlobalHotkeysWin() override;

    Hotkeys::Registry::GlobalSupport support() const override { return Hotkeys::Registry::GlobalSupport::Direct; }
    void setEntries(const QList<Entry>& entries) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void unregisterAll();

    // Registered hotkeys by the number given to RegisterHotKey.
    QHash<int, QString> registered_;
    int nextNumber_ = 1;
};

} // namespace Integration
