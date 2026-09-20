#include "NowPlayingBar.h"

#include <QDesktopServices>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include "Icons.h"
#include "Metrics.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui {

namespace {
QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

// An Icon Button (design system spec) whose glyph itself needs to swap
// color on hover — QSS/currentColor can't recolor SVG icon *content* in
// Qt, only the background (handled declaratively via the "icon"/"play"
// dynamic property + Theme::StyleSheet's QPushButton[variant=...] rules),
// so the icon swap has to happen here in code.
class IconHoverButton : public QPushButton {
public:
    // Neutral: the usual transport control — transparent-to-surface-200
    // background, ink-secondary glyph at rest, ink on hover (see
    // Theme::StyleSheet's QPushButton[variant="icon"] rule).
    // Accent: the play/pause button specifically — this app's primary
    // action, so it gets the same accent-filled treatment as HeroPanel's
    // big play button (QPushButton[variant="play"]), just at the smaller
    // Icon Button size. Its glyph is always on-accent regardless of
    // hover — ink-secondary/ink would be unreadable against a solid
    // accent fill.
    enum class Scheme {
        Neutral,
        Accent
    };

    explicit IconHoverButton(const QString& iconName, Scheme scheme, QWidget* parent)
        : QPushButton(parent)
        , iconName_(iconName)
        , scheme_(scheme)
    {
        setProperty("variant", scheme_ == Scheme::Accent ? "play" : "icon");
        setFixedSize(Theme::Metrics::iconButtonSize, Theme::Metrics::iconButtonSize);
        setIconSize(QSize(Theme::Metrics::iconGlyphSize, Theme::Metrics::iconGlyphSize));
        applyIcon(restColor());
        // Checked buttons (like/dislike) need their glyph re-tinted on
        // toggle too — checked state can be set programmatically (see
        // setLikeState()/setDislikeState()) without a hover/leave event
        // ever firing.
        connect(
            this, &QPushButton::toggled, this, [this](bool) { applyIcon(underMouse() ? hoverColor() : restColor()); });
    }

    // Playback-state-driven icon changes (play/pause/refresh) go through
    // this, not setIcon() directly, so a subsequent hover/leave doesn't
    // reset the glyph back to whatever name the button was constructed
    // with.
    void setIconName(const QString& name)
    {
        iconName_ = name;
        applyIcon(underMouse() ? hoverColor() : restColor());
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        applyIcon(hoverColor());
        QPushButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        applyIcon(restColor());
        QPushButton::leaveEvent(event);
    }

private:
    // Checked reads as "activated" (liked/disliked) regardless of scheme —
    // an accent-colored glyph on top of the QSS :checked background
    // (Theme::StyleSheet.cpp's icon-variant rule) rather than just the flat
    // highlight every other checked icon button would otherwise get.
    Theme::IconColor restColor() const
    {
        if (isChecked())
            return Theme::IconColor::Accent;
        return scheme_ == Scheme::Accent ? Theme::IconColor::OnAccent : Theme::IconColor::InkSecondary;
    }
    Theme::IconColor hoverColor() const
    {
        if (isChecked())
            return Theme::IconColor::Accent;
        return scheme_ == Scheme::Accent ? Theme::IconColor::OnAccent : Theme::IconColor::Ink;
    }
    void applyIcon(Theme::IconColor color) { setIcon(Theme::icon(iconName_, color, Theme::Metrics::iconGlyphSize)); }

    QString iconName_;
    Scheme scheme_;
};

// The volume handle is visible only while hovering or dragging its track —
// QSS can't bind one subcontrol's visibility to the widget's own hover
// state in a single declarative rule the way `::handle:hover` sounds like
// it should, so this toggles a dynamic property the stylesheet's
// `QSlider[handleVisible="false"]::handle` rule reads. The seek slider is
// the opposite: its accent handle stays visible at all times (a plain
// QSlider, see the constructor below) so the current playback position
// reads at a glance without needing to hover the bar first.
class HoverHandleSlider : public QSlider {
public:
    explicit HoverHandleSlider(QWidget* parent)
        : QSlider(Qt::Horizontal, parent)
    {
        setProperty("handleVisible", false);
        connect(this, &QSlider::sliderPressed, this, [this]() { setHandleVisible(true); });
        connect(this, &QSlider::sliderReleased, this, [this]() { setHandleVisible(underMouse()); });
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        setHandleVisible(true);
        QSlider::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        setHandleVisible(isSliderDown());
        QSlider::leaveEvent(event);
    }

private:
    void setHandleVisible(bool visible)
    {
        setProperty("handleVisible", visible);
        style()->unpolish(this);
        style()->polish(this);
    }
};
} // namespace

NowPlayingBar::NowPlayingBar(QWidget* parent)
    : QWidget(parent)
{
    // No setFixedHeight(): the controls column below is two rows now
    // (buttons, then sliders) instead of one, so its natural height varies
    // with the current widget style/font rather than being a number worth
    // hardcoding — Fixed vertical policy alone already means "use
    // sizeHint()'s height as both min and max".
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    previousButton_ = new IconHoverButton(QStringLiteral("skip_previous"), IconHoverButton::Scheme::Neutral, this);
    playPauseButton_ = new IconHoverButton(QStringLiteral("play_arrow"), IconHoverButton::Scheme::Accent, this);
    nextButton_ = new IconHoverButton(QStringLiteral("skip_next"), IconHoverButton::Scheme::Neutral, this);
    stopButton_ = new IconHoverButton(QStringLiteral("stop"), IconHoverButton::Scheme::Neutral, this);
    // Opens the current track's page on its source platform (e.g. a
    // Yandex Music/YouTube Music track URL) — see setTrackWebUrl().
    // open_in_new (the standard "external link" box-with-arrow glyph) — a
    // plain action icon, same family/style as the rest of this row.
    openTrackPageButton_ = new IconHoverButton(QStringLiteral("open_in_new"), IconHoverButton::Scheme::Neutral, this);
    openTrackPageButton_->setToolTip(tr("Open track page"));
    // Like/dislike — checkable, real toggles (feedback.like/.dislike and
    // their .unlike/.undislike counterparts). Checked reads as "sent" (see
    // IconHoverButton's checked handling above); clicking again reverses
    // it (see setLikeState()/setDislikeState() and MainWindow's
    // likeToggledAsync()/dislikeToggledAsync()).
    likeButton_ = new IconHoverButton(QStringLiteral("thumb_up"), IconHoverButton::Scheme::Neutral, this);
    likeButton_->setCheckable(true);
    likeButton_->setToolTip(tr("Like"));
    dislikeButton_ = new IconHoverButton(QStringLiteral("thumb_down"), IconHoverButton::Scheme::Neutral, this);
    dislikeButton_->setCheckable(true);
    dislikeButton_->setToolTip(tr("Dislike"));
    connect(previousButton_, &QPushButton::clicked, this, &NowPlayingBar::previousClicked);
    connect(playPauseButton_, &QPushButton::clicked, this, &NowPlayingBar::playPauseClicked);
    connect(nextButton_, &QPushButton::clicked, this, &NowPlayingBar::nextClicked);
    connect(stopButton_, &QPushButton::clicked, this, &NowPlayingBar::stopClicked);
    connect(openTrackPageButton_, &QPushButton::clicked, this, [this]() {
        if (!currentWebUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(currentWebUrl_));
    });
    // clicked(), not toggled(): clicked() only fires from real user
    // interaction, so MainWindow's setLikeState()/setDislikeState() calls
    // (setChecked() under the hood) never loop back into another RPC call.
    connect(likeButton_, &QPushButton::clicked, this, &NowPlayingBar::likeClicked);
    connect(dislikeButton_, &QPushButton::clicked, this, &NowPlayingBar::dislikeClicked);
    // Nothing loaded yet at construction — setTrackAvailable()/
    // setQueueAvailable()/setTrackWebUrl() (driven by PlaybackController's
    // own state, see MainWindow) enable these once there's something to
    // act on.
    previousButton_->setEnabled(false);
    playPauseButton_->setEnabled(false);
    nextButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    openTrackPageButton_->setEnabled(false);
    likeButton_->setEnabled(false);
    dislikeButton_->setEnabled(false);
    // Top row of the controls column below — transport buttons, then
    // whatever setTrailingWidget() appends (MainWindow's hamburger menu
    // button) pinned to the right by the stretch.
    buttonsRow_ = new QHBoxLayout;
    buttonsRow_->addWidget(previousButton_);
    buttonsRow_->addWidget(playPauseButton_);
    buttonsRow_->addWidget(nextButton_);
    buttonsRow_->addWidget(stopButton_);

    // A vertical separator, not just spacing, so openTrackPageButton_/
    // likeButton_/dislikeButton_ visually read as their own group — "act on
    // the current track" actions, distinct from the transport controls to
    // their left.
    auto* transportSeparator = new QFrame(this);
    transportSeparator->setObjectName(QStringLiteral("transportSeparator"));
    transportSeparator->setFrameShape(QFrame::VLine);
    // Plain, not Sunken: a sunken/raised bevel is drawn from palette
    // light/dark roles regardless of QSS `color`, which would silently
    // ignore Theme::StyleSheet's #transportSeparator rule and keep
    // whatever 3D bevel the native style draws — this design system's flat
    // depth model (tone + hairline border, no bevels/shadows) needs a
    // plain line that actually takes that color.
    transportSeparator->setFrameShadow(QFrame::Plain);
    buttonsRow_->addSpacing(6);
    buttonsRow_->addWidget(transportSeparator);
    buttonsRow_->addSpacing(6);

    buttonsRow_->addWidget(openTrackPageButton_);
    buttonsRow_->addWidget(likeButton_);
    buttonsRow_->addWidget(dislikeButton_);
    buttonsRow_->addStretch(1);

    elapsedLabel_ = new QLabel(QStringLiteral("0:00"), this);
    durationLabel_ = new QLabel(QStringLiteral("0:00"), this);
    // Tabular figures so the transport bar's width doesn't jitter as the
    // digits change during playback.
    elapsedLabel_->setFont(Theme::tabularFont(Theme::TextStyle::Caption));
    durationLabel_->setFont(Theme::tabularFont(Theme::TextStyle::Caption));
    // Plain QSlider: the accent handle stays visible at all times (see
    // HoverHandleSlider's doc comment above).
    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setObjectName(QStringLiteral("seekSlider"));
    seekSlider_->setRange(0, 0);
    seekSlider_->setEnabled(false);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { userIsDraggingSeek_ = true; });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
        userIsDraggingSeek_ = false;
        emit seekRequested(seekSlider_->value());
    });
    volumeSlider_ = new HoverHandleSlider(this);
    volumeSlider_->setObjectName(QStringLiteral("volumeSlider"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setFixedWidth(100);
    connect(volumeSlider_, &QSlider::valueChanged, this, &NowPlayingBar::volumeChanged);

    // Bottom row of the controls column: seek slider (with elapsed/duration
    // labels) gets the stretch, volume trails after it — was its own
    // separate top-level row before, now shares this one instead of sitting
    // beside the transport buttons.
    auto* slidersRow = new QHBoxLayout;
    slidersRow->addWidget(elapsedLabel_);
    slidersRow->addWidget(seekSlider_, 1);
    slidersRow->addWidget(durationLabel_);
    slidersRow->addSpacing(12);
    auto* volumeIconLabel = new QLabel(this);
    // Matches the transport buttons (also Theme::icon) instead of an emoji
    // glyph, which looked out of place next to them and depended on the
    // font actually having a color-emoji glyph for it.
    volumeIconLabel->setPixmap(
        Theme::icon(QStringLiteral("volume_up"), Theme::IconColor::InkSecondary, 16).pixmap(16, 16));
    slidersRow->addWidget(volumeIconLabel);
    slidersRow->addWidget(volumeSlider_);

    // Buttons above the sliders instead of everything crammed into one row
    // — that one row left this widget's fixed 72px height mostly empty
    // padding above/below it, since none of these controls are anywhere
    // near that tall on their own.
    auto* rootLayout = new QVBoxLayout(this);
    // Design system's space4 (sides) / space3 (top/bottom) — was a much
    // tighter (8, 4, 8, 4), cramped against the design mockup's roomier bar.
    rootLayout->setContentsMargins(
        Theme::Spacing::space4, Theme::Spacing::space3, Theme::Spacing::space4, Theme::Spacing::space3);
    // Design system's space2 — was the layout's default spacing (a couple
    // px), leaving the buttons row and the sliders row visually stuck
    // together against the design mockup's clearer gap between them.
    rootLayout->setSpacing(Theme::Spacing::space2);
    rootLayout->addLayout(buttonsRow_);
    rootLayout->addLayout(slidersRow);
}

void NowPlayingBar::setTrackAvailable(bool available)
{
    playPauseButton_->setEnabled(available);
    stopButton_->setEnabled(available);
    seekSlider_->setEnabled(available);
    if (!available) {
        lastDurationMs_ = 0;
        seekSlider_->setRange(0, 0);
        seekSlider_->setValue(0);
        elapsedLabel_->setText(QStringLiteral("0:00"));
        durationLabel_->setText(QStringLiteral("0:00"));
    }
}

void NowPlayingBar::setQueueAvailable(bool available)
{
    previousButton_->setEnabled(available);
    nextButton_->setEnabled(available);
}

void NowPlayingBar::setTrackWebUrl(const QString& url)
{
    currentWebUrl_ = url;
    openTrackPageButton_->setEnabled(!url.isEmpty());
}

void NowPlayingBar::setLikeState(bool capabilitySupported, bool liked)
{
    likeSupported_ = capabilitySupported;
    liked_ = liked;
    likeBusy_ = false;
    refreshLikeButton();
}

void NowPlayingBar::setLikeBusy(bool busy)
{
    likeBusy_ = busy;
    refreshLikeButton();
}

void NowPlayingBar::refreshLikeButton()
{
    likeButton_->setEnabled(likeSupported_ && !likeBusy_);
    likeButton_->setChecked(liked_);
    static_cast<IconHoverButton*>(likeButton_)
        ->setIconName(likeBusy_ ? QStringLiteral("refresh") : QStringLiteral("thumb_up"));
}

void NowPlayingBar::setDislikeState(bool capabilitySupported, bool disliked)
{
    dislikeSupported_ = capabilitySupported;
    disliked_ = disliked;
    dislikeBusy_ = false;
    refreshDislikeButton();
}

void NowPlayingBar::setDislikeBusy(bool busy)
{
    dislikeBusy_ = busy;
    refreshDislikeButton();
}

void NowPlayingBar::refreshDislikeButton()
{
    dislikeButton_->setEnabled(dislikeSupported_ && !dislikeBusy_);
    dislikeButton_->setChecked(disliked_);
    static_cast<IconHoverButton*>(dislikeButton_)
        ->setIconName(dislikeBusy_ ? QStringLiteral("refresh") : QStringLiteral("thumb_down"));
}

void NowPlayingBar::setPlaying(bool playing)
{
    playing_ = playing;
    updatePlayPauseIcon();
}

void NowPlayingBar::setLoading(bool loading)
{
    playPauseButton_->setEnabled(!loading);
    static_cast<IconHoverButton*>(playPauseButton_)
        ->setIconName(
            loading ? QStringLiteral("refresh") : (playing_ ? QStringLiteral("pause") : QStringLiteral("play_arrow")));
}

void NowPlayingBar::updatePlayPauseIcon()
{
    static_cast<IconHoverButton*>(playPauseButton_)
        ->setIconName(playing_ ? QStringLiteral("pause") : QStringLiteral("play_arrow"));
}

void NowPlayingBar::setPosition(qint64 positionMs, qint64 durationMs)
{
    if (durationMs != lastDurationMs_) {
        lastDurationMs_ = durationMs;
        seekSlider_->setRange(0, static_cast<int>(durationMs));
        durationLabel_->setText(formatDuration(durationMs));
    }
    elapsedLabel_->setText(formatDuration(positionMs));
    if (!userIsDraggingSeek_) {
        seekSlider_->setValue(static_cast<int>(positionMs));
    }
}

void NowPlayingBar::setVolume(int volume0To100)
{
    QSignalBlocker blocker(volumeSlider_);
    volumeSlider_->setValue(volume0To100);
}

void NowPlayingBar::setTrailingWidget(QWidget* widget) { buttonsRow_->addWidget(widget); }

} // namespace Ui
