#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QVariantMap>

#include <functional>

#include "GlobalHotkeys.h"

class QDBusMessage;
class QDBusServiceWatcher;

namespace Integration {

// The XDG GlobalShortcuts portal — how GNOME (48 and later) and other
// Wayland desktops give an app global keys. The app only says which actions
// it has and a key it would like for each; the desktop asks the user once and
// from then on owns the keys (changed in its own settings, not the app's).
//
// It runs on a private D-Bus connection: an app that isn't sandboxed must
// tell the portal its id (host.portal.Registry.Register) before any other
// call on the connection, and Qt's shared one has usually made some by then.
class GlobalHotkeysPortal : public GlobalHotkeys {
    Q_OBJECT

public:
    // The portal's GlobalShortcuts interface version; 0 if the session has no
    // such portal.
    static uint availableVersion();

    explicit GlobalHotkeysPortal(uint version, QObject* parent = nullptr);
    ~GlobalHotkeysPortal() override;

    Hotkeys::Registry::GlobalSupport support() const override { return support_; }
    QString unavailableReason() const override { return reason_; }
    bool canConfigureInSystem() const override { return version_ >= 2 && !session_.isEmpty() && bound_; }

    void setEntries(const QList<Entry>& entries) override;
    void configureInSystem() override;

private slots:
    void onResponse(const QDBusMessage& message);
    void onActivated(const QDBusMessage& message);
    void onShortcutsChanged(const QDBusMessage& message);
    void onPortalOwnerChanged(const QString& service, const QString& oldOwner, const QString& newOwner);

private:
    using ResponseHandler = std::function<void(uint code, const QVariantMap& results)>;

    QString newToken();
    QString requestPath(const QString& token) const;
    // Calls a GlobalShortcuts method that answers through a Request; `handler`
    // gets the answer.
    void callWithRequest(const QString& method, const QList<QVariant>& leading, QVariantMap options,
        const ResponseHandler& handler, const std::function<void(const QString&)>& onError);
    void createSession();
    void bindShortcuts(quint64 generation);
    void closeSession();
    void fail(const QString& why);
    void registerApp();
    void setTriggers(const QVariant& shortcuts);

    QDBusConnection connection_;
    QString connectionName_;
    QDBusServiceWatcher* watcher_ = nullptr;
    uint version_ = 0;

    Hotkeys::Registry::GlobalSupport support_ = Hotkeys::Registry::GlobalSupport::SystemAssigned;
    QString reason_;

    QList<Entry> entries_;
    // What the session was bound with: the global actions' ids and names.
    QString boundSignature_;
    QString session_;
    bool bound_ = false;
    // Changes with every session, so an answer to an earlier one is ignored.
    quint64 generation_ = 0;
    int tokenCounter_ = 0;
    QHash<QString, ResponseHandler> pending_;
    QHash<QString, QString> triggers_;
};

} // namespace Integration
