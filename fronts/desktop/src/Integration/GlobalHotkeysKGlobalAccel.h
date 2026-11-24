#pragma once

#include <QHash>
#include <QKeySequence>

#include "GlobalHotkeys.h"

class QDBusMessage;

namespace Integration {

// KDE's global shortcuts daemon (org.kde.kglobalaccel). Its keys are shared
// with System Settings → Shortcuts both ways: what the app sets shows up
// there at once, and what the user sets there reaches the app (while it
// runs, or the next time it starts — the daemon keeps them).
class GlobalHotkeysKGlobalAccel : public GlobalHotkeys {
    Q_OBJECT

public:
    static bool available();

    explicit GlobalHotkeysKGlobalAccel(QObject* parent = nullptr);

    Hotkeys::Registry::GlobalSupport support() const override { return Hotkeys::Registry::GlobalSupport::Direct; }
    void setEntries(const QList<Entry>& entries) override;

private slots:
    void onShortcutPressed(const QString& componentUnique, const QString& actionUnique, qlonglong timestamp);
    void onShortcutsChanged(const QDBusMessage& message);

private:
    QStringList actionId(const Entry& entry) const;
    QList<int> setShortcut(const QStringList& actionId, const QList<int>& keys, uint flags);
    void connectComponent();
    // The keys earlier versions left under the component `cloudmus`: the
    // user's own are kept (reported as changed), the component is removed.
    void migrateOldComponent();
    void postKey(const QString& id, const QKeySequence& key);

    // What the daemon has an action set to — absent: not registered yet.
    QHash<QString, QKeySequence> current_;
    QSet<QString> failed_;
    bool componentConnected_ = false;
};

} // namespace Integration
