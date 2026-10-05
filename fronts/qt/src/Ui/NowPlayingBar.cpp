#include "NowPlayingBar.h"

#include <QDesktopServices>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "Icons.h"
#include "Metrics.h"
#include "Registry.h"
#include "Spacing.h"
#include "TabOrder.h"
#include "ThemedSlider.h"
#include "Tokens.h"
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
        Theme::followTheme(this, [this]() { refreshIcon(); });
        // Checked buttons (like/dislike) need their glyph re-tinted on
        // toggle too — checked state can be set programmatically (see
        // setLikeState()/setDislikeState()) without a hover/leave event
        // ever firing.
        connect(this, &QPushButton::toggled, this, [this](bool) { refreshIcon(); });
    }

    // Playback-state-driven icon changes (play/pause/refresh) go through
    // this, not setIcon() directly, so a subsequent hover/leave doesn't
    // reset the glyph back to whatever name the button was constructed
    // with.
    void setIconName(const QString& name)
    {
        iconName_ = name;
        refreshIcon();
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        refreshIcon();
        QPushButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        refreshIcon();
        QPushButton::leaveEvent(event);
    }
    // Enabling/disabling fires no enter/leave/toggled.
    void changeEvent(QEvent* event) override
    {
        QPushButton::changeEvent(event);
        if (event->type() == QEvent::EnabledChange)
            refreshIcon();
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
    // A disabled button never lights up under the mouse (Qt still sends it
    // enter/leave) and shows no accent, even when checked.
    void refreshIcon()
    {
        const int side = Theme::Metrics::iconGlyphSize;
        QIcon icon = Theme::icon(iconName_, underMouse() ? hoverColor() : restColor(), side);
        // Used while disabled, whatever the hover/checked state above: given
        // explicitly so Qt doesn't substitute its own grey.
        icon.addPixmap(Theme::icon(iconName_, Theme::IconColor::Disabled, side).pixmap(side), QIcon::Disabled);
        setIcon(icon);
    }

    QString iconName_;
    Scheme scheme_;
};

// The download button, with a ring around it while downloads are under way
// — filled to their progress like a browser's, or a spinning arc while
// there's nothing to measure yet.
class DownloadButton : public IconHoverButton {
public:
    explicit DownloadButton(QWidget* parent)
        : IconHoverButton(QStringLiteral("file_download"), Scheme::Neutral, parent)
    {
        spin_ = new QVariantAnimation(this);
        spin_->setStartValue(0.0);
        spin_->setEndValue(360.0);
        spin_->setDuration(1100);
        spin_->setLoopCount(-1);
        connect(spin_, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    }

    void setRing(bool active, double progress)
    {
        active_ = active;
        progress_ = progress;
        const bool spinning = active && progress < 0;
        if (spinning && spin_->state() != QAbstractAnimation::Running)
            spin_->start();
        else if (!spinning)
            spin_->stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        IconHoverButton::paintEvent(event);
        if (!active_)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        constexpr qreal kWidth = 2.5;
        const QRectF ring = QRectF(rect()).adjusted(kWidth / 2 + 1, kWidth / 2 + 1, -kWidth / 2 - 1, -kWidth / 2 - 1);
        const Theme::Palette& pal = Theme::palette();
        painter.setPen(QPen(pal.border, kWidth));
        painter.drawEllipse(ring);
        QPen arc(pal.accent, kWidth);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        // Qt's angles: 1/16 degree, counter-clockwise from 3 o'clock.
        if (progress_ < 0) {
            const int start = int((90.0 - spin_->currentValue().toReal()) * 16);
            painter.drawArc(ring, start, -90 * 16);
        } else {
            painter.drawArc(ring, 90 * 16, -int(qBound(0.0, progress_, 1.0) * 360 * 16));
        }
    }

private:
    QVariantAnimation* spin_;
    bool active_ = false;
    double progress_ = -1;
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
    playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
    nextButton_ = new IconHoverButton(QStringLiteral("skip_next"), IconHoverButton::Scheme::Neutral, this);
    stopButton_ = new IconHoverButton(QStringLiteral("stop"), IconHoverButton::Scheme::Neutral, this);
    // Play modes — checked (accent glyph) while on, like like/dislike.
    // Repeat has three states in one button; its glyph tells the list
    // (repeat) from the track (repeat_one).
    shuffleButton_ = new IconHoverButton(QStringLiteral("shuffle"), IconHoverButton::Scheme::Neutral, this);
    shuffleButton_->setCheckable(true);
    repeatButton_ = new IconHoverButton(QStringLiteral("repeat"), IconHoverButton::Scheme::Neutral, this);
    repeatButton_->setCheckable(true);
    setPlayModes(false, Playback::RepeatMode::Off, false);
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
    likeButton_ = new IconHoverButton(QStringLiteral("favorite_border"), IconHoverButton::Scheme::Neutral, this);
    likeButton_->setObjectName(QStringLiteral("likeButton"));
    likeButton_->setCheckable(true);
    dislikeButton_ = new IconHoverButton(QStringLiteral("heart_off_outline"), IconHoverButton::Scheme::Neutral, this);
    dislikeButton_->setCheckable(true);
    // Not checkable, unlike like/dislike — a repeat download is a normal
    // thing to ask for again, not a state to toggle off.
    downloadButton_ = new DownloadButton(this);
    connect(previousButton_, &QPushButton::clicked, this, &NowPlayingBar::previousClicked);
    connect(playPauseButton_, &QPushButton::clicked, this, &NowPlayingBar::playPauseClicked);
    connect(nextButton_, &QPushButton::clicked, this, &NowPlayingBar::nextClicked);
    connect(stopButton_, &QPushButton::clicked, this, &NowPlayingBar::stopClicked);
    connect(shuffleButton_, &QPushButton::clicked, this, &NowPlayingBar::shuffleClicked);
    connect(repeatButton_, &QPushButton::clicked, this, [this]() {
        using Playback::RepeatMode;
        RepeatMode next = RepeatMode::Off;
        if (repeat_ == RepeatMode::Off)
            next = radio_ ? RepeatMode::One : RepeatMode::All;
        else if (repeat_ == RepeatMode::All)
            next = RepeatMode::One;
        // Qt already flipped the checked state; setPlayModes() (called
        // back with what took effect) sets the real one.
        emit repeatClicked(next);
    });
    connect(openTrackPageButton_, &QPushButton::clicked, this, [this]() {
        if (!currentWebUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(currentWebUrl_));
    });
    // clicked(), not toggled(): clicked() only fires from real user
    // interaction, so MainWindow's setLikeState()/setDislikeState() calls
    // (setChecked() under the hood) never loop back into another RPC call.
    connect(likeButton_, &QPushButton::clicked, this, &NowPlayingBar::likeClicked);
    connect(dislikeButton_, &QPushButton::clicked, this, &NowPlayingBar::dislikeClicked);
    connect(downloadButton_, &QPushButton::clicked, this,
        [this]() { emit downloadClicked(downloadButton_->mapToGlobal(QPoint(0, 0))); });
    playlistsButton_ = new IconHoverButton(QStringLiteral("playlist_add"), IconHoverButton::Scheme::Neutral, this);
    playlistsButton_->setToolTip(tr("Add to playlist"));
    connect(playlistsButton_, &QPushButton::clicked, this,
        [this]() { emit playlistsClicked(playlistsButton_->mapToGlobal(playlistsButton_->rect().bottomLeft())); });
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
    downloadButton_->setEnabled(false);
    playlistsButton_->setEnabled(false);
    refreshToolTips();
    // Top row of the controls column below — the buttons in groups, then
    // whatever setTrailingWidget() appends (MainWindow's hamburger menu
    // button) pinned to the right by the stretch:
    //   transport | play modes | rating | the track's other actions
    buttonsRow_ = new QHBoxLayout;
    const auto addSeparator = [this]() {
        // A vertical line, not just spacing, so each group reads as its own.
        auto* separator = new QFrame(this);
        separator->setObjectName(QStringLiteral("transportSeparator"));
        separator->setFrameShape(QFrame::VLine);
        // Plain, not Sunken: a sunken/raised bevel is drawn from palette
        // light/dark roles regardless of QSS `color`, which would silently
        // ignore Theme::StyleSheet's #transportSeparator rule and keep
        // whatever 3D bevel the native style draws — this design system's
        // flat depth model (tone + hairline border, no bevels/shadows)
        // needs a plain line that actually takes that color.
        separator->setFrameShadow(QFrame::Plain);
        buttonsRow_->addSpacing(6);
        buttonsRow_->addWidget(separator);
        buttonsRow_->addSpacing(6);
    };
    buttonsRow_->addWidget(previousButton_);
    buttonsRow_->addWidget(playPauseButton_);
    buttonsRow_->addWidget(nextButton_);
    buttonsRow_->addWidget(stopButton_);
    addSeparator();
    buttonsRow_->addWidget(shuffleButton_);
    buttonsRow_->addWidget(repeatButton_);
    addSeparator();
    buttonsRow_->addWidget(likeButton_);
    buttonsRow_->addWidget(dislikeButton_);
    addSeparator();
    buttonsRow_->addWidget(playlistsButton_);
    buttonsRow_->addWidget(downloadButton_);
    buttonsRow_->addWidget(openTrackPageButton_);
    buttonsRow_->addStretch(1);

    elapsedLabel_ = new QLabel(QStringLiteral("0:00"), this);
    durationLabel_ = new QLabel(QStringLiteral("0:00"), this);
    // Tabular figures so the transport bar's width doesn't jitter as the
    // digits change during playback.
    elapsedLabel_->setFont(Theme::tabularFont(Theme::TextStyle::Caption));
    durationLabel_->setFont(Theme::tabularFont(Theme::TextStyle::Caption));
    // Elapsed hugs the slider from the left, duration from the right.
    elapsedLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    durationLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fitTimeLabels(0);
    // The seek handle stays visible at all times so the current playback
    // position reads at a glance; the volume one only shows on hover.
    auto* seekSlider = new ThemedSlider(ThemedSlider::Scheme::Accent, ThemedSlider::HandleVisibility::Always, this);
    // The slider's range is the track's duration in ms — show where the drag would seek to.
    seekSlider->setValueBubble([](int positionMs) { return formatDuration(positionMs); });
    seekSlider_ = seekSlider;
    seekSlider_->setObjectName(QStringLiteral("seekSlider"));
    seekSlider_->setRange(0, 0);
    seekSlider_->setEnabled(false);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { userIsDraggingSeek_ = true; });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
        userIsDraggingSeek_ = false;
        emit seekRequested(seekSlider_->value());
    });
    auto* volumeSlider = new ThemedSlider(ThemedSlider::Scheme::Neutral, ThemedSlider::HandleVisibility::OnHover, this);
    volumeSlider->setValueBubble([](int volume) { return QStringLiteral("%1%").arg(volume); });
    volumeSlider_ = volumeSlider;
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
    Theme::followTheme(volumeIconLabel, [volumeIconLabel]() {
        volumeIconLabel->setPixmap(
            Theme::icon(QStringLiteral("volume_up"), Theme::IconColor::InkSecondary, 16).pixmap(16, 16));
    });
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
    chainTabOrder(this);
}

void NowPlayingBar::setTrackAvailable(bool available)
{
    trackAvailable_ = available;
    updatePlayAvailability();
    stopButton_->setEnabled(available);
    seekSlider_->setEnabled(available);
    if (!available) {
        lastDurationMs_ = 0;
        seekSlider_->setRange(0, 0);
        seekSlider_->setValue(0);
        seekSlider_->setBufferedValue(-1);
        elapsedLabel_->setText(QStringLiteral("0:00"));
        durationLabel_->setText(QStringLiteral("0:00"));
        timeLabelWidth_ = 0;
        fitTimeLabels(0);
    }
}

void NowPlayingBar::setPlaylistAvailable(bool available)
{
    playlistAvailable_ = available;
    updatePlayAvailability();
}

void NowPlayingBar::updatePlayAvailability()
{
    playPauseButton_->setEnabled(!loading_ && (trackAvailable_ || playlistAvailable_));
}

void NowPlayingBar::setQueueAvailable(bool available)
{
    previousButton_->setEnabled(available);
    nextButton_->setEnabled(available);
}

void NowPlayingBar::setPlayModes(bool shuffle, Playback::RepeatMode repeat, bool radio)
{
    using Playback::RepeatMode;
    repeat_ = repeat;
    radio_ = radio;
    shuffleButton_->setEnabled(!radio);
    shuffleButton_->setChecked(shuffle && !radio);
    shuffleButton_->setToolTip(radio ? tr("Shuffle isn't available for radio")
            : shuffle                ? tr("Shuffle: on")
                                     : tr("Shuffle: off"));
    repeatButton_->setChecked(repeat != RepeatMode::Off);
    static_cast<IconHoverButton*>(repeatButton_)
        ->setIconName(repeat == RepeatMode::One ? QStringLiteral("repeat_one") : QStringLiteral("repeat"));
    repeatButton_->setToolTip(repeat == RepeatMode::All ? tr("Repeat: list")
            : repeat == RepeatMode::One                 ? tr("Repeat: track")
                                                        : tr("Repeat: off"));
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
    refreshToolTips();
    static_cast<IconHoverButton*>(likeButton_)
        ->setIconName(likeBusy_ ? QStringLiteral("refresh")
                : liked_        ? QStringLiteral("favorite") // filled once liked, outline otherwise
                                : QStringLiteral("favorite_border"));
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
    refreshToolTips();
    static_cast<IconHoverButton*>(dislikeButton_)
        ->setIconName(dislikeBusy_ ? QStringLiteral("refresh")
                : disliked_        ? QStringLiteral("heart_off") // filled once disliked, outline otherwise
                                   : QStringLiteral("heart_off_outline"));
}

void NowPlayingBar::setPlaylistsState(bool capabilitySupported) { playlistsButton_->setEnabled(capabilitySupported); }

void NowPlayingBar::setDownloadsVisible(bool visible) { downloadButton_->setVisible(visible); }

void NowPlayingBar::setDownloadState(bool capabilitySupported)
{
    downloadSupported_ = capabilitySupported;
    refreshDownloadButton();
}

void NowPlayingBar::setDownloadActivity(bool active, double progress)
{
    downloadsActive_ = active;
    static_cast<DownloadButton*>(downloadButton_)->setRing(active, progress);
    refreshDownloadButton();
}

void NowPlayingBar::refreshDownloadButton()
{
    // While downloads run it opens their panel — clickable whatever plays.
    downloadButton_->setEnabled(downloadSupported_ || downloadsActive_);
    refreshToolTips();
}

void NowPlayingBar::setPlaying(bool playing)
{
    playing_ = playing;
    updatePlayPauseIcon();
}

void NowPlayingBar::setLoading(bool loading)
{
    loading_ = loading;
    updatePlayAvailability();
    static_cast<IconHoverButton*>(playPauseButton_)
        ->setIconName(
            loading ? QStringLiteral("refresh") : (playing_ ? QStringLiteral("pause") : QStringLiteral("play_arrow")));
}

void NowPlayingBar::fitTimeLabels(qint64 longestMs)
{
    // Tabular figures aren't guaranteed to be really equal-width in every
    // font: measure the longest time's shape with its widest digit.
    const QFontMetrics metrics(elapsedLabel_->font());
    QChar widest = QLatin1Char('0');
    for (char digit = '1'; digit <= '9'; ++digit) {
        if (metrics.horizontalAdvance(QLatin1Char(digit)) > metrics.horizontalAdvance(widest))
            widest = QLatin1Char(digit);
    }
    QString shape = formatDuration(longestMs);
    for (QChar& c : shape) {
        if (c.isDigit())
            c = widest;
    }
    const int width = metrics.horizontalAdvance(shape) + 1;
    // Only grows within a track; setPosition() resets it for a new one.
    if (width <= timeLabelWidth_)
        return;
    timeLabelWidth_ = width;
    elapsedLabel_->setFixedWidth(width);
    durationLabel_->setFixedWidth(width);
}

void NowPlayingBar::setHotkeys(Hotkeys::Registry& hotkeys)
{
    hotkeys_ = &hotkeys;
    connect(hotkeys_, &Hotkeys::Registry::bindingsChanged, this, &NowPlayingBar::refreshToolTips);
    refreshToolTips();
}

void NowPlayingBar::refreshToolTips()
{
    using Hotkeys::Action;
    const auto tip = [this](const QString& text, Action action) {
        return hotkeys_ != nullptr ? hotkeys_->toolTip(text, action) : text;
    };
    previousButton_->setToolTip(tip(tr("Previous"), Action::Previous));
    playPauseButton_->setToolTip(tip(playing_ ? tr("Pause") : tr("Play"), Action::PlayPause));
    nextButton_->setToolTip(tip(tr("Next"), Action::Next));
    stopButton_->setToolTip(tip(tr("Stop"), Action::Stop));
    likeButton_->setToolTip(tip(liked_ ? tr("Unlike") : tr("Like"), Action::Like));
    dislikeButton_->setToolTip(tip(disliked_ ? tr("Remove Dislike") : tr("Dislike"), Action::Dislike));
    // While downloads run the button opens their panel, not a download.
    downloadButton_->setToolTip(downloadsActive_ ? tr("Downloads") : tip(tr("Save to Downloads"), Action::Download));
}

void NowPlayingBar::updatePlayPauseIcon()
{
    refreshToolTips();
    static_cast<IconHoverButton*>(playPauseButton_)
        ->setIconName(playing_ ? QStringLiteral("pause") : QStringLiteral("play_arrow"));
}

void NowPlayingBar::setPosition(qint64 positionMs, qint64 durationMs)
{
    if (durationMs != lastDurationMs_) {
        lastDurationMs_ = durationMs;
        seekSlider_->setRange(0, static_cast<int>(durationMs));
        durationLabel_->setText(formatDuration(durationMs));
        timeLabelWidth_ = 0;
    }
    // A stream without a known duration can outgrow it.
    fitTimeLabels(qMax(durationMs, positionMs));
    elapsedLabel_->setText(formatDuration(positionMs));
    if (!userIsDraggingSeek_) {
        seekSlider_->setValue(static_cast<int>(positionMs));
    }
}

void NowPlayingBar::setBuffered(qint64 bufferedMs)
{
    seekSlider_->setBufferedValue(bufferedMs < 0 ? -1 : static_cast<int>(bufferedMs));
}

void NowPlayingBar::setVolume(int volume0To100)
{
    QSignalBlocker blocker(volumeSlider_);
    volumeSlider_->setValue(volume0To100);
}

void NowPlayingBar::setTrailingWidget(QWidget* widget)
{
    buttonsRow_->addWidget(widget);
    chainTabOrder(this);
}

} // namespace Ui
