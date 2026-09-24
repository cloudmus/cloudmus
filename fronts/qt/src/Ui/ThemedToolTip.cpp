#include "ThemedToolTip.h"

#include <QCoreApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWidget>

#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {
constexpr int kAnimationMs = 150;
constexpr int kMaxVisibleMs = 8000; // safety net if a Leave event is ever missed
constexpr int kCursorOffset = 16; // roughly matches the native QToolTip's own offset

// Same "hand-painted, margin-reserved shadow" technique as
// Theme::CloudMusStyle uses for QMenu, and for the same reason: a
// QGraphicsDropShadowEffect's blur bleeds past the widget's own geometry,
// but a top-level window can't paint beyond its own frame — it was
// getting silently clipped away invisible. Smaller/softer than the menu's
// (kMenuShadowMargin=12, max alpha 32) since this popup is much smaller.
constexpr int kShadowMargin = 8;
constexpr int kShadowOffsetY = 2;
constexpr int kShadowMaxAlpha = 40;

void paintShadow(QPainter* painter, const QRect& contentRect)
{
    Theme::paintSoftShadow(painter, contentRect, kShadowMargin, kShadowOffsetY, kShadowMaxAlpha, Theme::Radius::sm);
}
} // namespace

// Floating replacement for Qt's native QToolTip — see ThemedToolTip's
// class doc for why. A genuine independent top-level window (Qt::ToolTip
// flag), not confined to any parent widget's bounds — positioned in
// screen coordinates the same way the native tooltip already is, so it
// can appear anywhere on the screen, not just within the app's window.
class ThemedToolTipPopup : public QWidget {
public:
    ThemedToolTipPopup()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground); // real transparency outside the rounded shape
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents); // never intercepts clicks
        setFont(Theme::font(Theme::TextStyle::Caption));

        opacityAnim_ = new QPropertyAnimation(this, "windowOpacity", this);
        opacityAnim_->setDuration(kAnimationMs);
        opacityAnim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(opacityAnim_, &QPropertyAnimation::finished, this, [this]() {
            // Only actually hide after a *fade-out* — the same animation
            // object is reused for fading in, and its finished() fires
            // then too; without this check the popup would hide itself
            // immediately after every fade-in.
            if (opacityAnim_->endValue().toReal() <= 0.0)
                hide();
        });
    }

    void showWithText(const QString& text, const QPoint& globalPos)
    {
        text_ = text;
        const QFontMetrics metrics(font());
        const QSize textSize = metrics.size(Qt::TextSingleLine, text_);
        const int padX = Theme::Spacing::space2;
        const int padY = Theme::Spacing::space1;
        const QSize contentSize(textSize.width() + padX * 2, textSize.height() + padY * 2);
        resize(contentSize.width() + kShadowMargin * 2, contentSize.height() + kShadowMargin * 2);
        reposition(globalPos);

        animateOpacityTo(1.0);
        show();
        raise();
    }

    void fadeOutAndHide()
    {
        if (isVisible())
            animateOpacityTo(0.0);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        // See Theme::CloudMusStyle::drawPrimitive's identical comment: a
        // translucent top-level window's repaints must replace (Source),
        // not blend (SourceOver, the default) — otherwise repeated
        // repaints compound antialiased edge alpha into visibly "torn"
        // corners over time.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRect contentRect = rect().adjusted(kShadowMargin, kShadowMargin, -kShadowMargin, -kShadowMargin);
        paintShadow(&painter, contentRect);

        const Theme::Palette& pal = Theme::palette();
        QPainterPath path;
        // -0.5 on the bottom/right: keeps the 1px border stroke fully
        // inside the content box instead of being clipped/antialiased
        // against its edge.
        path.addRoundedRect(QRectF(contentRect).adjusted(0.5, 0.5, -0.5, -0.5), Theme::Radius::sm, Theme::Radius::sm);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface400);
        painter.drawPath(path);

        painter.setPen(pal.ink);
        painter.drawText(contentRect, Qt::AlignCenter, text_);
    }

private:
    void animateOpacityTo(qreal target)
    {
        opacityAnim_->stop();
        opacityAnim_->setStartValue(windowOpacity());
        opacityAnim_->setEndValue(target);
        opacityAnim_->start();
    }

    void reposition(const QPoint& globalPos)
    {
        // Anchor the VISIBLE content box (not the shadow-inflated window)
        // at the cursor — otherwise the tooltip would visually sit
        // kShadowMargin further right/down than the cursor, the same bug
        // Theme::CloudMusStyle had to compensate for with QMenu.
        const QSize contentSize = size() - QSize(2 * kShadowMargin, 2 * kShadowMargin);
        QRect content(globalPos + QPoint(kCursorOffset, kCursorOffset), contentSize);
        const QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect avail = screen->availableGeometry();

        // Flip the anchor per axis independently, rather than just
        // clamping — a tooltip glued to the wrong edge of the screen
        // reads worse than one that mirrors to the other side of the
        // cursor. This is a real top-level window, not a child confined
        // to the main window, so the only boundary that matters here is
        // the screen's own.
        if (content.right() > avail.right())
            content.moveRight(globalPos.x() - kCursorOffset);
        if (content.bottom() > avail.bottom())
            content.moveBottom(globalPos.y() - kCursorOffset);

        setGeometry(QRect(content.topLeft() - QPoint(kShadowMargin, kShadowMargin), size()));
    }

    QString text_;
    QPropertyAnimation* opacityAnim_;
};

ThemedToolTip::ThemedToolTip(QObject* parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
    safetyTimer_.setSingleShot(true);
    safetyTimer_.setInterval(kMaxVisibleMs);
    connect(&safetyTimer_, &QTimer::timeout, this, &ThemedToolTip::hidePopup);
}

bool ThemedToolTip::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::ToolTip: {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && !widget->toolTip().isEmpty()) {
                showFor(widget);
                return true; // suppress the native QToolTip::showText entirely
            }
            break;
        }
        case QEvent::Leave:
            if (watched == activeWidget_)
                hidePopup();
            break;
        case QEvent::MouseButtonPress:
            hidePopup();
            break;
        default:
            break;
    }
    return false;
}

void ThemedToolTip::showFor(QWidget* watched)
{
    // Qt resends QEvent::ToolTip on every mouse move within an already-
    // hovered widget, not just once on first entry — without this guard
    // the popup kept re-positioning to the live cursor position and
    // restarting its fade-in on every micro-movement, visibly "chasing"
    // the cursor instead of staying put like the native tooltip does.
    if (watched == activeWidget_ && popup_ && popup_->isVisible())
        return;
    if (!popup_)
        popup_ = new ThemedToolTipPopup; // no parent: a genuine top-level window, cleaned up by QApplication at exit
    activeWidget_ = watched;
    static_cast<ThemedToolTipPopup*>(popup_)->showWithText(watched->toolTip(), QCursor::pos());
    safetyTimer_.start();
}

void ThemedToolTip::hidePopup()
{
    safetyTimer_.stop();
    activeWidget_ = nullptr;
    if (popup_)
        static_cast<ThemedToolTipPopup*>(popup_)->fadeOutAndHide();
}

} // namespace Ui
