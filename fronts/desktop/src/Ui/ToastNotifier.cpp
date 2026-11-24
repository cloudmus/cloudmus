#include "ToastNotifier.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <functional>
#include <optional>

#include "Icons.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Style.h"
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
constexpr int kDetailsGap = Theme::Spacing::space2; // between the message and its details
constexpr int kStatusIconSide = 16;
constexpr int kBarWidth = 3;
constexpr int kShadowMargin = 10; // reserved around the card for its shadow
constexpr int kAppearMs = 260;
constexpr int kDisappearMs = 200;
constexpr int kRegroupMs = 220;
constexpr qreal kSlideDistance = 48.0;

// One toast: a card with the message, a × and a vertical countdown bar,
// all painted here (so the slide/fade can move and fade everything at
// once through the painter, no child widgets to keep in step). The card
// sits inside kShadowMargin of transparent room for its shadow. Its
// right-click menu copies the message — an error is worth pasting into a
// bug report, and the toast is gone before it can be retyped.
class Toast : public QWidget {
public:
    Toast(const QString& message, const QString& details, ToastNotifier::Kind kind, QWidget* parent)
        : QWidget(parent)
        , message_(message)
        , details_(details)
        , kind_(kind)
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
    std::function<void()> onResized; // unfolded or folded: the stack regroups

    // What the toast shows now, which is what copying copies.
    QString shownText() const
    {
        return expanded_ && !details_.isEmpty() ? message_ + QStringLiteral("\n\n") + details_ : message_;
    }

    // Sized for `width` (card width plus the shadow margins).
    void layoutForWidth(int width)
    {
        const int textWidth = width - 2 * kShadowMargin - textLeft() - kPadding - buttonsWidth() - kBarWidth;
        const QFontMetrics metrics(Theme::font(Theme::TextStyle::Body));
        messageHeight_ = metrics.boundingRect(QRect(0, 0, textWidth, INT_MAX), Qt::TextWordWrap, message_).height();
        detailsHeight_ = 0;
        if (expanded_) {
            const QFontMetrics small(Theme::font(Theme::TextStyle::BodySecondary));
            detailsHeight_ = small
                                 .boundingRect(QRect(0, 0, detailsWidth(width), INT_MAX),
                                     Qt::TextWordWrap | Qt::TextWrapAnywhere, details_)
                                 .height();
        }
        const int cardHeight
            = qMax(messageHeight_, kCloseSide) + (expanded_ ? kDetailsGap + detailsHeight_ : 0) + 2 * kPadding;
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
        // A status toast is tinted with its color — fill washed toward it,
        // border in it — so it reads at a glance without its text.
        const std::optional<QColor> status = statusColor();
        QColor fill = pal.surfaceRaised;
        QColor border = pal.borderStrong;
        if (status) {
            fill = mix(pal.surfaceRaised, *status, 0.14);
            border = *status;
            border.setAlphaF(0.6);
        }
        painter.setPen(QPen(border, 1));
        painter.setBrush(fill);
        painter.drawPath(shape);

        if (status) {
            const QString glyph
                = kind_ == ToastNotifier::Kind::Error ? QStringLiteral("warning") : QStringLiteral("check");
            const QRect iconRect(card.left() + kPadding, card.top() + kPadding + (kCloseSide - kStatusIconSide) / 2,
                kStatusIconSide, kStatusIconSide);
            Theme::iconWithColor(glyph, *status, kStatusIconSide).paint(&painter, iconRect);
        }

        // Message.
        painter.setFont(Theme::font(Theme::TextStyle::Body));
        painter.setPen(pal.ink);
        painter.drawText(textRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, message_);
        if (expanded_) {
            painter.setFont(Theme::font(Theme::TextStyle::BodySecondary));
            painter.setPen(pal.inkSecondary);
            painter.drawText(
                detailsRect(), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere, details_);
        }

        // The chevron, where there is something behind the message.
        if (hasDetails()) {
            const QRect chevron = chevronRect();
            if (chevronHovered_) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(pal.surface300);
                painter.drawEllipse(chevron);
            }
            Theme::icon(expanded_ ? QStringLiteral("expand_less") : QStringLiteral("expand_more"),
                chevronHovered_ ? Theme::IconColor::Ink : Theme::IconColor::InkSecondary, 14)
                .paint(&painter, chevron.adjusted(3, 3, -3, -3));
        }

        // ×, with a round hover backdrop.
        const QRect close = closeRect();
        if (closeHovered_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.surface300);
            painter.drawEllipse(close);
        }
        Theme::icon(QStringLiteral("close"), closeHovered_ ? Theme::IconColor::Ink : Theme::IconColor::InkSecondary, 14)
            .paint(&painter, close.adjusted(3, 3, -3, -3));

        // Countdown bar: the remaining share, draining from the top. Not
        // while unfolded: it doesn't run then.
        if (expanded_)
            return;
        const QRectF track = barRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.border);
        painter.drawRoundedRect(track, kBarWidth / 2.0, kBarWidth / 2.0);
        const qreal filled = track.height() * remaining_;
        painter.setBrush(status.value_or(pal.inkSecondary));
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
        setChevronHovered(false);
        // The pointer moving onto the menu leaves the toast; it stays
        // paused until the menu is gone.
        if (!menuOpen_ && !expanded_ && countdown_->state() == QAbstractAnimation::Paused)
            countdown_->resume();
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (leaving_)
            return;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(Theme::icon(QStringLiteral("content_copy"), Theme::IconColor::Ink, 16),
            ToastNotifier::tr("Copy Message"), this, [this]() { QGuiApplication::clipboard()->setText(shownText()); });
        menuOpen_ = true;
        connect(menu, &QMenu::aboutToHide, this, [this]() {
            menuOpen_ = false;
            if (!expanded_ && !rect().contains(mapFromGlobal(QCursor::pos()))
                && countdown_->state() == QAbstractAnimation::Paused)
                countdown_->resume();
        });
        Theme::popupMenu(menu, event->globalPos());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        setCloseHovered(closeRect().contains(event->position().toPoint()));
        setChevronHovered(hasDetails() && chevronRect().contains(event->position().toPoint()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QPoint at = event->position().toPoint();
        if (closeRect().contains(at) && onCloseClicked)
            onCloseClicked();
        else if (hasDetails() && chevronRect().contains(at))
            setExpanded(!expanded_);
    }

private:
    QRect cardRect() const { return rect().adjusted(kShadowMargin, kShadowMargin, -kShadowMargin, -kShadowMargin); }
    // Past the status icon, when there is one.
    int textLeft() const { return kPadding + (statusColor() ? kStatusIconSide + Theme::Spacing::space2 : 0); }
    std::optional<QColor> statusColor() const
    {
        switch (kind_) {
            case ToastNotifier::Kind::Error:
                return Theme::palette().danger;
            case ToastNotifier::Kind::Success:
                return Theme::palette().success;
            case ToastNotifier::Kind::Info:
                break;
        }
        return std::nullopt;
    }
    static QColor mix(const QColor& base, const QColor& tint, qreal amount)
    {
        return QColor::fromRgbF(base.redF() + (tint.redF() - base.redF()) * amount,
            base.greenF() + (tint.greenF() - base.greenF()) * amount,
            base.blueF() + (tint.blueF() - base.blueF()) * amount);
    }
    bool hasDetails() const { return !details_.isEmpty(); }
    // The ×, and the chevron to its left when there is one.
    int buttonsWidth() const { return kCloseSide + (hasDetails() ? kPadding / 2 + kCloseSide : 0); }
    int detailsWidth(int width) const
    {
        return width - 2 * kShadowMargin - textLeft() - kPadding - kBarWidth - kPadding;
    }
    QRect textRect() const
    {
        const QRect card = cardRect();
        const int left = card.left() + textLeft();
        const int right = (hasDetails() ? chevronRect().left() : closeRect().left()) - kPadding;
        return QRect(left, card.top() + kPadding, right - left, qMax(messageHeight_, kCloseSide));
    }
    QRect detailsRect() const
    {
        const QRect card = cardRect();
        const QRect text = textRect();
        return QRect(text.left(), text.bottom() + 1 + kDetailsGap, detailsWidth(width()), detailsHeight_);
    }
    QRect chevronRect() const { return closeRect().translated(-(kCloseSide + kPadding / 2), 0); }
    void setExpanded(bool expanded)
    {
        if (expanded == expanded_ || leaving_)
            return;
        expanded_ = expanded;
        if (expanded_) {
            // What's being read doesn't go away by itself.
            countdown_->pause();
        } else {
            // Folded again: its time runs anew (paused if the pointer is on it).
            countdown_->stop();
            countdown_->start();
            if (underMouse())
                countdown_->pause();
        }
        layoutForWidth(width());
        update();
        if (onResized)
            onResized();
    }
    void setChevronHovered(bool hovered)
    {
        if (hovered == chevronHovered_)
            return;
        chevronHovered_ = hovered;
        setCursor(hovered || closeHovered_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update(chevronRect());
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
        setCursor(hovered || chevronHovered_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update(closeRect());
    }

    QString message_;
    QString details_;
    ToastNotifier::Kind kind_;
    bool leaving_ = false;
    bool expanded_ = false;
    bool closeHovered_ = false;
    bool chevronHovered_ = false;
    int messageHeight_ = 0;
    int detailsHeight_ = 0;
    bool menuOpen_ = false;
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

void ToastNotifier::showError(const QString& message, const QString& details)
{
    showToast(message, Kind::Error, details);
}

void ToastNotifier::showSuccess(const QString& message, const QString& details)
{
    showToast(message, Kind::Success, details);
}

void ToastNotifier::showInfo(const QString& message, const QString& details)
{
    showToast(message, Kind::Info, details);
}

void ToastNotifier::showToast(const QString& message, Kind kind, const QString& details)
{
    auto* toast = new Toast(message, details, kind, anchor_);
    toast->layoutForWidth(qMin(kMaxWidth, anchor_->width() / 2) + 2 * kShadowMargin);
    toast->onExpired = [this, toast]() { dismiss(toast); };
    toast->onCloseClicked = [this, toast]() { dismiss(toast); };
    toast->onGone = [toast]() { toast->deleteLater(); };
    toast->onResized = [this]() { layoutToasts(); };
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
