#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

namespace Ui {

// In-app toasts, stacked at the anchor's bottom-right corner. Each one
// slides in from the right while fading in, counts down its time on a
// thin vertical bar at its right edge (paused while hovered, so it can be
// read to the end), and slides back out fading when the time is up or its
// × is clicked; the rest of the stack glides to its new places whenever
// one comes or goes.
class ToastNotifier : public QObject {
    Q_OBJECT

public:
    explicit ToastNotifier(QWidget* anchor);

    void showError(const QString& message);
    void showInfo(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showToast(const QString& message, bool error);
    void dismiss(QWidget* toast);
    // Moves every toast to its slot in the stack — animated, unless it's
    // a toast just being placed for its entrance.
    void layoutToasts(QWidget* entering = nullptr);

    QWidget* anchor_;
    QList<QPointer<QWidget>> toasts_; // bottom-most last
};

} // namespace Ui
