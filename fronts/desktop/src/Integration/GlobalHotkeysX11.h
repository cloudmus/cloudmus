#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>

#include "GlobalHotkeys.h"

namespace Integration {

// Key grabs on X11's root window: for the X11 sessions without a daemon of
// their own to ask (not KDE), and without the portal. (GNOME on Wayland,
// even for X11 windows under XWayland, doesn't deliver them: that is the
// portal's.)
class GlobalHotkeysX11 : public GlobalHotkeys, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    static bool available();

    explicit GlobalHotkeysX11(QObject* parent = nullptr);
    ~GlobalHotkeysX11() override;

    Hotkeys::Registry::GlobalSupport support() const override { return Hotkeys::Registry::GlobalSupport::Direct; }
    void setEntries(const QList<Entry>& entries) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    struct Grab {
        QString id;
        quint8 keycode = 0;
        quint16 modifiers = 0;
    };

    void ungrabAll();

    void* connection_ = nullptr; // xcb_connection_t*
    quint32 root_ = 0;
    QList<Grab> grabs_;
    QHash<QString, QKeySequence> grabbedKeys_;
};

} // namespace Integration
