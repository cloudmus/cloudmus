#include "ToastNotifier.h"

#include <QLabel>
#include <QTimer>

namespace Ui {

namespace {
constexpr int kToastMs = 4000;
constexpr int kToastSpacing = 6;
} // namespace

ToastNotifier::ToastNotifier(QWidget* anchor)
    : QObject(anchor)
    , anchor_(anchor)
{
}

void ToastNotifier::showError(const QString& message)
{
    auto* toast = new QLabel(message, anchor_);
    toast->setWindowFlags(Qt::Widget);
    toast->setWordWrap(true);
    toast->setAutoFillBackground(true);
    toast->setStyleSheet(QStringLiteral("QLabel { background: palette(tooltip-base); color: palette(tooltip-text); "
                                        "border-radius: 4px; padding: 8px 12px; }"));
    toast->setMaximumWidth(anchor_->width() / 2);
    toast->adjustSize();
    toast->show();
    activeToasts_.append(toast);
    repositionToasts();

    QTimer::singleShot(kToastMs, toast, [this, toast]() {
        activeToasts_.removeAll(toast);
        toast->deleteLater();
        repositionToasts();
    });
}

void ToastNotifier::repositionToasts()
{
    int y = anchor_->height() - 16;
    for (auto it = activeToasts_.rbegin(); it != activeToasts_.rend(); ++it) {
        QWidget* toast = *it;
        y -= toast->height();
        toast->move(anchor_->width() - toast->width() - 16, y);
        toast->raise();
        y -= kToastSpacing;
    }
}

} // namespace Ui
