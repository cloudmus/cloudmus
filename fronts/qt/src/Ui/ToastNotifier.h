#pragma once

#include <QList>
#include <QWidget>

namespace Ui {

// Small, non-modal, auto-dismissing, stackable in-app toasts, anchored to a
// corner of the parent widget (MainWindow). Distinct from
// Integration::NotificationToast (the OS-level org.freedesktop.Notifications
// popup for track-change + cover art) — this one is in-app, text-only, and
// needs no D-Bus. Fed by RPC-level failures, backend `error` notifications,
// playback resolve failures — see the plan's "Background operations: busy
// state & error surfacing" — and, via showInfo(), context-menu actions
// (like/dislike/download) that have no other visual confirmation the way
// the toolbar's own button state already gives the currently-playing track.
class ToastNotifier : public QObject {
    Q_OBJECT

public:
    explicit ToastNotifier(QWidget* anchor);

    void showError(const QString& message);
    // Same look as showError() today (the #toastLabel QSS rule has no
    // error-specific styling) — this exists for call-site clarity
    // ("succeeded" vs "failed"), not a new visual state.
    void showInfo(const QString& message);

private:
    void showToast(const QString& message);
    void repositionToasts();

    QWidget* anchor_;
    QList<QWidget*> activeToasts_;
};

} // namespace Ui
