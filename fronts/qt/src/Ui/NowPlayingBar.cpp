#include "NowPlayingBar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace Ui {

namespace {
QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}
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

    previousButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-skip-backward")), QString(), this);
    playPauseButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), QString(), this);
    nextButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-skip-forward")), QString(), this);
    stopButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-stop")), QString(), this);
    connect(previousButton_, &QPushButton::clicked, this, &NowPlayingBar::previousClicked);
    connect(playPauseButton_, &QPushButton::clicked, this, &NowPlayingBar::playPauseClicked);
    connect(nextButton_, &QPushButton::clicked, this, &NowPlayingBar::nextClicked);
    connect(stopButton_, &QPushButton::clicked, this, &NowPlayingBar::stopClicked);
    // Nothing loaded yet at construction — setTrackAvailable()/
    // setQueueAvailable() (driven by PlaybackController's own state, see
    // MainWindow) enable these once there's something to act on.
    previousButton_->setEnabled(false);
    playPauseButton_->setEnabled(false);
    nextButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    // Top row of the controls column below — transport buttons, then
    // whatever setTrailingWidget() appends (MainWindow's hamburger menu
    // button) pinned to the right by the stretch.
    buttonsRow_ = new QHBoxLayout;
    buttonsRow_->addWidget(previousButton_);
    buttonsRow_->addWidget(playPauseButton_);
    buttonsRow_->addWidget(nextButton_);
    buttonsRow_->addWidget(stopButton_);
    buttonsRow_->addStretch(1);

    elapsedLabel_ = new QLabel(QStringLiteral("0:00"), this);
    durationLabel_ = new QLabel(QStringLiteral("0:00"), this);
    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setRange(0, 0);
    seekSlider_->setEnabled(false);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { userIsDraggingSeek_ = true; });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
        userIsDraggingSeek_ = false;
        emit seekRequested(seekSlider_->value());
    });
    volumeSlider_ = new QSlider(Qt::Horizontal, this);
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
    // Matches the transport buttons (also QIcon::fromTheme) instead of an
    // emoji glyph, which looked out of place next to them and depended on
    // the font actually having a color-emoji glyph for it.
    volumeIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("audio-volume-high")).pixmap(16, 16));
    slidersRow->addWidget(volumeIconLabel);
    slidersRow->addWidget(volumeSlider_);

    // Buttons above the sliders instead of everything crammed into one row
    // — that one row left this widget's fixed 72px height mostly empty
    // padding above/below it, since none of these controls are anywhere
    // near that tall on their own.
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 4, 8, 4);
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

void NowPlayingBar::setPlaying(bool playing)
{
    playing_ = playing;
    updatePlayPauseIcon();
}

void NowPlayingBar::setLoading(bool loading)
{
    playPauseButton_->setEnabled(!loading);
    playPauseButton_->setIcon(QIcon::fromTheme(loading
            ? QStringLiteral("view-refresh")
            : (playing_ ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start"))));
}

void NowPlayingBar::updatePlayPauseIcon()
{
    playPauseButton_->setIcon(
        QIcon::fromTheme(playing_ ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
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
