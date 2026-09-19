#include "ToastNotifier.h"

#include <QGraphicsDropShadowEffect>
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
    toast->setObjectName(QStringLiteral("toastLabel")); // styled by Theme::StyleSheet's global #toastLabel rule
    toast->setWindowFlags(Qt::Widget);
    toast->setWordWrap(true);
    toast->setAutoFillBackground(true);
    // A popup floating over arbitrary content with no border — the design
    // system's one permitted exception to "no shadows by default"
    // (QGraphicsDropShadowEffect in code, not a QSS box-shadow, which Qt
    // Style Sheets don't support at all).
    auto* shadow = new QGraphicsDropShadowEffect(toast);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 80));
    toast->setGraphicsEffect(shadow);
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
    // Backward index loop, not rbegin()/rend(): std::reverse_iterator over
    // QList<QWidget*>::iterator hits an ambiguous indirectly_readable_traits
    // instantiation on libstdc++ from GCC 10 (Debian bullseye's default) —
    // QList's iterator has exposed both value_type and element_type since
    // Qt 6.7's C++20 ranges support, which GCC 10's (pre-fix) libstdc++
    // can't disambiguate between. Fixed in GCC 11+, but bullseye stays on 10.
    for (int i = activeToasts_.size() - 1; i >= 0; --i) {
        QWidget* toast = activeToasts_.at(i);
        y -= toast->height();
        toast->move(anchor_->width() - toast->width() - 16, y);
        toast->raise();
        y -= kToastSpacing;
    }
}

} // namespace Ui
