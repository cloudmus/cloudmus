#include "QrCodeView.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QThreadPool>
#include <QVariantAnimation>

#include <algorithm>
#include <functional>
#include <memory>

#include "FocusRing.h"
#include "Icons.h"
#include "QrPicture.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Transition.h"

namespace Ui {

namespace {
// A picture has no size of its own to hint: large enough for a phone to read
// it at arm's length, small enough to sit in a card.
constexpr int kSide = 240;
// The round "this gets larger" badge on the code's corner, small enough to stay
// in the code's blank margin.
constexpr int kBadgeSide = 32;
constexpr int kBadgeOverhang = 8;
constexpr int kBadgeIconSize = 20;
constexpr qreal kPressedScale = 0.97;
constexpr int kPressMs = 150;
// Out over the window and back, as on mobile.
constexpr int kGrowMs = 320;
constexpr qreal kBackdropAlpha = 0.94;
// The picture's rounded corners, as a share of its side (Qr::picture's).
constexpr qreal kCornerShare = 0.1;

// `content` drawn sharp for `side` logical pixels on a screen of this density.
QPixmap drawCode(const QString& content, int side, qreal ratio)
{
    QPixmap pixmap = QPixmap::fromImage(Qr::picture(content, qRound(side * ratio)));
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}
} // namespace

// The code over the whole of a window, on its way there from the view or back.
// Takes the keyboard and the mouse meanwhile; deletes itself once it's back.
class QrOverlay : public QWidget {
public:
    QrOverlay(QrCodeView* source, QWidget* window)
        : QWidget(window)
        , source_(source)
        , backdrop_(this, effect({ 0.0 }), effect({ 0.0 }))
        , code_(this, effect({ }), effect({ }))
    {
        setObjectName(QStringLiteral("qrOverlay"));
        setFocusPolicy(Qt::StrongFocus);
        window->installEventFilter(this);
        setGeometry(window->rect());
        code_.setOnSettled([this]() {
            if (!open_)
                deleteLater();
        });
    }

    bool isOpen() const { return open_; }

    // `standIn` is shown until the large picture is drawn.
    void present(const QRect& from, const QPixmap& standIn)
    {
        open_ = true;
        from_ = from;
        show();
        raise();
        setFocus(Qt::PopupFocusReason);
        backdrop_.show(0);
        code_.setEffects(effect(placedAt(from_)), effect({ }));
        code_.show(standIn);
        redraw();
    }

    // Back to `from` (where the view is now; the last known place otherwise).
    void dismiss(const QRect& from = { })
    {
        if (!open_)
            return;
        open_ = false;
        if (from.isValid())
            from_ = from;
        // What's under it can be used again at once.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        if (hasFocus() && source_ != nullptr && source_->isVisible())
            source_->setFocus(Qt::PopupFocusReason);
        backdrop_.hide();
        code_.setEffects(effect({ }), effect(placedAt(from_)));
        code_.hide();
    }

    // The picture for the size at rest, drawn off the UI thread: at a window's
    // size it takes long enough to hold the animation up.
    void redraw()
    {
        if (!open_ || source_ == nullptr)
            return;
        QThreadPool::globalInstance()->start(
            [self = QPointer<QrOverlay>(this), request = ++request_, content = source_->content(),
                side = target().width(), ratio = devicePixelRatioF()] {
                const QImage image = Qr::picture(content, qRound(side * ratio));
                // The application as the context: the overlay may be gone, and
                // only its own thread may ask.
                QMetaObject::invokeMethod(
                    qApp,
                    [self, request, image, ratio] {
                        // Not one the window has been resized past, or leaving.
                        if (!self || request != self->request_ || !self->open_)
                            return;
                        if (QPixmap* current = self->code_.current()) {
                            *current = QPixmap::fromImage(image);
                            current->setDevicePixelRatio(ratio);
                            self->update();
                        }
                    },
                    Qt::QueuedConnection);
            });
    }

    // Set by the view: it knows its own place.
    std::function<void()> onCloseRequested;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        backdrop_.forEach([&](int, const LayerState& state) {
            QColor shade(Qt::black);
            shade.setAlphaF(kBackdropAlpha * state.opacity);
            painter.fillRect(rect(), shade);
        });
        const QRect place = target();
        code_.forEach([&](const QPixmap& pixmap, const LayerState& state) {
            painter.save();
            applyLayerState(painter, state, QRectF(place).center());
            painter.drawPixmap(place, pixmap);
            painter.restore();
        });
    }

    bool event(QEvent* event) override
    {
        // No shortcut (play/pause, closing a dialog) acts from under it.
        if (event->type() == QEvent::ShortcutOverride && open_) {
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            redraw();
        }
        return QWidget::eventFilter(watched, event);
    }

    bool focusNextPrevChild(bool) override { return false; }

    void keyPressEvent(QKeyEvent* event) override
    {
        switch (event->key()) {
            case Qt::Key_Escape:
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                if (onCloseRequested)
                    onCloseRequested();
                break;
            default:
                break;
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override { event->accept(); }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        event->accept();
        if (event->button() == Qt::LeftButton && onCloseRequested)
            onCloseRequested();
    }

    void wheelEvent(QWheelEvent* event) override { event->ignore(); }

private:
    static TransitionEffect effect(const LayerState& state) { return { kGrowMs, QEasingCurve::OutCubic, state }; }

    // The code at rest: as large as the window allows, in its middle.
    QRect target() const
    {
        const int side = std::max(1, std::min(width(), height()) - 2 * Theme::Spacing::space5);
        QRect place(0, 0, side, side);
        place.moveCenter(rect().center());
        return place;
    }

    // The resting picture moved and scaled to cover `from`.
    LayerState placedAt(const QRect& from) const
    {
        const QRectF place(target());
        return { 1.0, from.width() / place.width(), QRectF(from).center() - place.center() };
    }

    QPointer<QrCodeView> source_;
    bool open_ = false;
    QRect from_;
    // The latest picture asked for.
    int request_ = 0;
    // The value is a stand-in: the backdrop is only its opacity.
    LayerTransition<int> backdrop_;
    LayerTransition<QPixmap> code_;
};

QrCodeView::QrCodeView(QWidget* parent)
    : QAbstractButton(parent)
    , press_(new QVariantAnimation(this))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("QR code"));
    press_->setDuration(kPressMs);
    connect(press_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        pressScale_ = value.toReal();
        update();
    });
    const auto pressTo = [this](qreal scale) {
        press_->stop();
        press_->setStartValue(pressScale_);
        press_->setEndValue(scale);
        press_->start();
    };
    connect(this, &QAbstractButton::pressed, this, [pressTo]() { pressTo(kPressedScale); });
    connect(this, &QAbstractButton::released, this, [pressTo]() { pressTo(1.0); });
    connect(this, &QAbstractButton::clicked, this, &QrCodeView::expand);
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

// The overlay is the window's child, not this widget's: it goes with it.
QrCodeView::~QrCodeView() { delete overlay_.data(); }

void QrCodeView::setContent(const QString& content)
{
    if (content == content_)
        return;
    content_ = content;
    picture_ = { };
    if (content_.isEmpty())
        collapse();
    else if (overlay_ != nullptr)
        overlay_->redraw();
    update();
}

bool QrCodeView::isExpanded() const { return overlay_ != nullptr && overlay_->isOpen(); }

QSize QrCodeView::sizeHint() const { return { kSide + kBadgeOverhang, kSide + kBadgeOverhang }; }

QRect QrCodeView::pictureRect() const
{
    const int side = std::max(0, std::min(width(), height()) - kBadgeOverhang);
    return { 0, 0, side, side };
}

void QrCodeView::expand()
{
    if (content_.isEmpty() || isExpanded())
        return;
    // One on its way back gives way to the new one.
    delete overlay_.data();
    overlay_ = new QrOverlay(this, window());
    overlay_->onCloseRequested = [this]() { collapse(); };
    const QRect place = pictureRect();
    overlay_->present(QRect(mapTo(window(), place.topLeft()), place.size()),
        picture_.isNull() ? drawCode(content_, place.width(), devicePixelRatioF()) : picture_);
    update();
}

void QrCodeView::collapse()
{
    if (!isExpanded())
        return;
    // Gone from its place (signed in, say), it returns to where it last was.
    overlay_->dismiss(isVisible() ? QRect(mapTo(window(), pictureRect().topLeft()), pictureRect().size()) : QRect());
    // In its place again once the other has landed.
    connect(overlay_, &QObject::destroyed, this, qOverload<>(&QWidget::update));
}

void QrCodeView::hideEvent(QHideEvent* event)
{
    QAbstractButton::hideEvent(event);
    // Minimizing isn't going away; neither is being hidden and shown again at
    // once, as a card does while it lays itself out anew.
    if (event->spontaneous())
        return;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            if (!isVisible())
                collapse();
        },
        Qt::QueuedConnection);
}

void QrCodeView::paintEvent(QPaintEvent*)
{
    const QRect place = pictureRect();
    // The one over the window is this one, moved.
    if (content_.isEmpty() || place.isEmpty() || overlay_ != nullptr)
        return;
    const int pixels = qRound(place.width() * devicePixelRatioF());
    if (picture_.width() != pixels || picture_.devicePixelRatio() != devicePixelRatioF())
        picture_ = drawCode(content_, place.width(), devicePixelRatioF());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.save();
    painter.translate(QRectF(place).center());
    painter.scale(pressScale_, pressScale_);
    painter.translate(-QRectF(place).center());
    painter.drawPixmap(place, picture_);
    painter.restore();

    const QRect badge(width() - kBadgeSide, height() - kBadgeSide, kBadgeSide, kBadgeSide);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::palette().accent);
    painter.drawEllipse(badge);
    QRect glyph(0, 0, kBadgeIconSize, kBadgeIconSize);
    glyph.moveCenter(badge.center());
    Theme::icon(QStringLiteral("fullscreen"), Theme::IconColor::OnAccent, kBadgeIconSize).paint(&painter, glyph);

    if (Theme::focusVisible(this)) {
        const qreal radius = place.width() * kCornerShare;
        Theme::paintFocusRing(&painter, QRectF(place), radius);
    }
}

} // namespace Ui
