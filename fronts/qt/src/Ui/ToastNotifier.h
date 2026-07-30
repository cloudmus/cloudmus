#pragma once

#include <QList>
#include <QWidget>

namespace Ui {

// Small, non-modal, auto-dismissing, stackable in-app error toasts,
// anchored to a corner of the parent widget (MainWindow). Distinct from
// Integration::NotificationToast (the OS-level org.freedesktop.Notifications
// popup for track-change + cover art) — this one is in-app, text-only,
// error-specific, and needs no D-Bus. Fed by RPC-level failures, backend
// `error` notifications, and playback resolve failures — see the plan's
// "Background operations: busy state & error surfacing".
class ToastNotifier : public QObject {
    Q_OBJECT

public:
    explicit ToastNotifier(QWidget* anchor);

    void showError(const QString& message);

private:
    void repositionToasts();

    QWidget* anchor_;
    QList<QWidget*> activeToasts_;
};

} // namespace Ui
