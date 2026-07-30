#pragma once

#include <QObject>

namespace Integration {

// User-configurable global hotkeys (distinct from hardware media keys,
// which MprisService already covers via org.mpris.MediaPlayer2) — via
// org.kde.kglobalaccel D-Bus registration, exactly like any native KDE
// app's global shortcuts. Neither this nor MprisService needs an X11
// key-grab, which wouldn't work under a Wayland session anyway.
//
// Fixed default bindings for this pass (Meta+Alt+P/Left/Right/S) — no
// rebinding UI yet, see Ui/SettingsDialog's doc comment. If
// org.kde.kglobalaccel isn't reachable on the session bus (non-KDE
// desktop), registration silently no-ops: hardware media keys via MPRIS
// already cover the primary "media keys" ask, so this is a pure bonus.
class GlobalShortcuts : public QObject {
    Q_OBJECT

public:
    explicit GlobalShortcuts(QObject* parent = nullptr);

signals:
    void playPauseTriggered();
    void nextTriggered();
    void previousTriggered();
    void stopTriggered();

private slots:
    void onGlobalShortcutPressed(const QString& componentUnique, const QString& actionUnique, qlonglong timestamp);

private:
    void registerAction(const QString& actionId, const QString& friendlyName, const QString& defaultShortcut);
};

} // namespace Integration
