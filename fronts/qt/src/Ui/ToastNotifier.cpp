#include "ToastNotifier.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <functional>

#include "Icons.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {

constexpr int kToastMs = 4000;
constexpr int kToastSpacing = 6; // between stacked cards
constexpr int kEdgeMargin = 16; // from the anchor's bottom-right corner
constexpr int kMaxWidth = 380;
constexpr int kPadding = Theme::Spacing::space3;
constexpr int kCloseSide = 20;
constexpr int kBarWidth = 3;
constexpr int kShadowMargin = 10; // reserved around the card for its shadow
constexpr int kAppearMs = 260;
constexpr int kDisappearMs = 200;
constexpr int kRegroupMs = 220;
constexpr qreal kSlideDistance = 48.0;

// One toast: a card with the message, a × and a vertical countdown bar,
// all painted here (so the slide/fade can move and fade everything at
// once through the painter, no child widgets to keep in step). The card
// sits inside kShadowMargin of transparent room for its shadow.
class Toast : public QWidget {
public:
    Toast(const QString& message, bool error, QWidget* parent)
        : QWidget(parent)
        , message_(message)
        , error_(error)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);

        // Countdown: 1 → 0 over kToastMs; paused while hovered.
        countdown_ = new QVariantAnimation(this);
        countdown_->setDuration(kToastMs);
        countdown_->setStartValue(1.0);
        countdown_->setEndValue(0.0);
        connect(countdown_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            remaining_ = v.toReal();
            update(barRect().toAlignedRect());
        });
        connect(countdown_, &QVariantAnimation::finished, this, [this]() {
            if (onExpired)
                onExpired();
        });

        // Entrance/exit: 0 = off to the right and transparent, 1 = in place.
        presence_ = new QVariantAnimation(this);
        connect(presence_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            presenceValue_ = v.toReal();
            update();
        });
    }

    std::function<void()> onExpired; // time's up
    std::function<void()> onCloseClicked; // the ×
    std::function<void()> onGone; // exit animation finished

    // Sized for `width` (card width plus the shadow margins).
    void layoutForWidth(int width)
    {
        const int textWidth = width - 2 * kShadowMargin - textLeft() - kPadding - kCloseSide - kPadding - kBarWidth;
        const QFontMetrics metrics(Theme::font(Theme::TextStyle::Body));
        const int textHeight
            = metrics.boundingRect(QRect(0, 0, textWidth, INT_MAX), Qt::TextWordWrap, message_).height();
        const int cardHeight = qMax(textHeight, kCloseSide) + 2 * kPadding;
        resize(width, cardHeight + 2 * kShadowMargin);
    }

    void appear()
    {
        presence_->stop();
        presence_->setDuration(kAppearMs);
        presence_->setEasingCurve(QEasingCurve::OutCubic);
        presence_->setStartValue(presenceValue_);
        presence_->setEndValue(1.0);
        presence_->start();
        countdown_->start();
    }

    void disappear()
    {
        if (leaving_)
            return;
        leaving_ = true;
        countdown_->stop();
        setAttribute(Qt::WA_TransparentForMouseEvents); // on its way out
        presence_->stop();
        presence_->setDuration(kDisappearMs);
        presence_->setEasingCurve(QEasingCurve::InCubic);
        presence_->setStartValue(presenceValue_);
        presence_->setEndValue(0.0);
        connect(presence_, &QVariantAnimation::finished, this, [this]() {
            if (onGone)
                onGone();
        });
        presence_->start();
    }

    bool leaving() const { return leaving_; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // Slide in from the right while fading in (and back out on exit).
        painter.setOpacity(presenceValue_);
        painter.translate((1.0 - presenceValue_) * kSlideDistance, 0);

        const Theme::Palette& pal = Theme::palette();
        const QRect card = cardRect();
        Theme::paintSoftShadow(&painter, card, kShadowMargin, 2, 48, Theme::Radius::md);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), Theme::Radius::md, Theme::Radius::md);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface200);
        painter.drawPath(shape);

        // Message.
        painter.setFont(Theme::font(Theme::TextStyle::Body));
        painter.setPen(pal.ink);
        painter.drawText(textRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, message_);

        // ×, with a round hover backdrop.
        const QRect close = closeRect();
        if (closeHovered_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.surface300);
            painter.drawEllipse(close);
        }
        Theme::icon(QStringLiteral("close"), closeHovered_ ? Theme::IconColor::Ink : Theme::IconColor::InkSecondary, 14)
            .paint(&painter, close.adjusted(3, 3, -3, -3));

        // Countdown bar: the remaining share, draining from the top.
        const QRectF track = barRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.border);
        painter.drawRoundedRect(track, kBarWidth / 2.0, kBarWidth / 2.0);
        const qreal filled = track.height() * remaining_;
        painter.setBrush(error_ ? pal.accent : pal.inkSecondary);
        painter.drawRoundedRect(
            QRectF(track.left(), track.bottom() - filled, track.width(), filled), kBarWidth / 2.0, kBarWidth / 2.0);
    }

    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        if (countdown_->state() == QAbstractAnimation::Running)
            countdown_->pause(); // let it be read to the end
    }

    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        setCloseHovered(false);
        if (countdown_->state() == QAbstractAnimation::Paused)
            countdown_->resume();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        setCloseHovered(closeRect().contains(event->position().toPoint()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && closeRect().contains(event->position().toPoint()) && onCloseClicked)
            onCloseClicked();
    }

private:
    QRect cardRect() const { return rect().adjusted(kShadowMargin, kShadowMargin, -kShadowMargin, -kShadowMargin); }
    static int textLeft() { return kPadding; }
    QRect textRect() const
    {
        const QRect card = cardRect();
        return QRect(card.left() + textLeft(), card.top() + kPadding,
            closeRect().left() - kPadding - (card.left() + textLeft()), card.height() - 2 * kPadding);
    }
    QRect closeRect() const
    {
        const QRect card = cardRect();
        return QRect(
            card.right() - kBarWidth - kPadding - kCloseSide + 1, card.top() + kPadding, kCloseSide, kCloseSide);
    }
    QRectF barRect() const
    {
        const QRect card = cardRect();
        return QRectF(card.right() - kBarWidth - Theme::Spacing::space1 + 1, card.top() + Theme::Spacing::space2,
            kBarWidth, card.height() - 2 * Theme::Spacing::space2);
    }
    void setCloseHovered(bool hovered)
    {
        if (hovered == closeHovered_)
            return;
        closeHovered_ = hovered;
        setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update(closeRect());
    }

    QString message_;
    bool error_;
    bool leaving_ = false;
    bool closeHovered_ = false;
    qreal remaining_ = 1.0;
    qreal presenceValue_ = 0.0;
    QVariantAnimation* countdown_ = nullptr;
    QVariantAnimation* presence_ = nullptr;
};

} // namespace

ToastNotifier::ToastNotifier(QWidget* anchor)
    : QObject(anchor)
    , anchor_(anchor)
{
    anchor_->installEventFilter(this); // keep the stack in its corner on resize
}

void ToastNotifier::showError(const QString& message) { showToast(message, /*error=*/true); }

void ToastNotifier::showInfo(const QString& message) { showToast(message, /*error=*/false); }

void ToastNotifier::showToast(const QString& message, bool error)
{
    auto* toast = new Toast(message, error, anchor_);
    toast->layoutForWidth(qMin(kMaxWidth, anchor_->width() / 2) + 2 * kShadowMargin);
    toast->onExpired = [this, toast]() { dismiss(toast); };
    toast->onCloseClicked = [this, toast]() { dismiss(toast); };
    toast->onGone = [toast]() { toast->deleteLater(); };
    toasts_.append(toast);
    layoutToasts(toast);
    toast->show();
    toast->raise();
    toast->appear();
}

void ToastNotifier::dismiss(QWidget* toast)
{
    auto* t = static_cast<Toast*>(toast);
    if (t->leaving())
        return;
    toasts_.removeAll(toast);
    t->disappear(); // slides out where it is
    // The rest close the gap — gliding over the leaving one, not under it.
    for (const QPointer<QWidget>& other : std::as_const(toasts_)) {
        if (other)
            other->raise();
    }
    layoutToasts();
}

void ToastNotifier::layoutToasts(QWidget* entering)
{
    // Bottom-up from the anchor's corner; the card (not the shadow margin
    // around it) sits kEdgeMargin from the edges.
    int bottom = anchor_->height() - kEdgeMargin + kShadowMargin;
    for (int i = toasts_.size() - 1; i >= 0; --i) {
        QWidget* toast = toasts_.at(i);
        if (toast == nullptr)
            continue;
        const QPoint target(anchor_->width() - kEdgeMargin + kShadowMargin - toast->width(), bottom - toast->height());
        if (toast == entering) {
            toast->move(target); // its own slide-in does the motion
        } else if (toast->pos() != target) {
            // One glide per toast: a regroup while it's still moving
            // retargets it from where it is instead of racing another.
            if (auto* running = toast->findChild<QPropertyAnimation*>(QStringLiteral("glide")))
                running->stop(); // DeleteWhenStopped
            auto* glide = new QPropertyAnimation(toast, "pos", toast);
            glide->setObjectName(QStringLiteral("glide"));
            glide->setDuration(kRegroupMs);
            glide->setEasingCurve(QEasingCurve::OutCubic);
            glide->setEndValue(target);
            glide->start(QAbstractAnimation::DeleteWhenStopped);
        }
        bottom = target.y() + kShadowMargin - kToastSpacing + kShadowMargin;
    }
}

bool ToastNotifier::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == anchor_ && event->type() == QEvent::Resize) {
        for (const QPointer<QWidget>& toast : std::as_const(toasts_)) {
            if (toast)
                toast->move(anchor_->width() - kEdgeMargin + kShadowMargin - toast->width(), toast->y());
        }
        layoutToasts();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Ui
