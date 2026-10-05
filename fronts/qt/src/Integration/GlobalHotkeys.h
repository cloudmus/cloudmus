#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

#include "Registry.h"

namespace Integration {

// The desktop's way of giving the app hotkeys that work with its window out
// of focus. One subclass per mechanism (KDE's kglobalaccel, the XDG portal
// — GNOME's way —, X11 key grabs, Windows' RegisterHotKey); the first that
// the session offers is used, see createGlobalHotkeys().
class GlobalHotkeys : public QObject {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString description;
        QKeySequence key;
        // Not global: the entry is only here so the desktop lets go of
        // whatever it still has for it.
        bool global = true;
    };

    using QObject::QObject;

    virtual Hotkeys::Registry::GlobalSupport support() const = 0;
    // Why there is no global support, when it is known.
    virtual QString unavailableReason() const { return { }; }
    // Whether the desktop has an editor for the keys that configureInSystem()
    // can open.
    virtual bool canConfigureInSystem() const { return false; }

    // Every action, in the catalog's order; called again whenever any of
    // them changes.
    virtual void setEntries(const QList<Entry>& entries) = 0;
    virtual void configureInSystem() { }

signals:
    void activated(const QString& id, const QString& activationToken);
    // Actions whose key the desktop would not give (taken by another app).
    void failedChanged(const QSet<QString>& ids);
    // The desktop set an action's key, or the user changed it there.
    void systemKeyChanged(const QString& id, const QKeySequence& key);
    // What the desktop set the keys to, in its own words (the portal).
    void systemTriggersChanged(const QHash<QString, QString>& triggers);
    // support() & co. changed.
    void stateChanged();
};

// The backend this session can use; one that does nothing (support() None)
// when it has none.
std::unique_ptr<GlobalHotkeys> createGlobalHotkeys();

} // namespace Integration
