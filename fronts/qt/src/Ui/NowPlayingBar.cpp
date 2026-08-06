#include "NowPlayingBar.h"

#include <QDesktopServices>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

#include "ClickableArea.h"
#include "CoverArtCache.h"

namespace Ui {

namespace {
QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

NowPlayingBar::NowPlayingBar(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
    , coverCache_(coverCache)
{
    // No setFixedHeight(): the controls column below is two rows now
    // (buttons, then sliders) instead of one, so its natural height varies
    // with the current widget style/font rather than being a number worth
    // hardcoding — Fixed vertical policy alone already means "use
    // sizeHint()'s height as both min and max".
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    coverLabel_ = new QLabel(this);
    coverLabel_->setFixedSize(56, 56);
    coverLabel_->setScaledContents(true);

    titleLabel_ = new QLabel(tr("Nothing playing"), this);
    artistLabel_ = new QLabel(this);
    // Ignored horizontally so a long title/artist's sizeHint can't force
    // this bar (and the whole window) to stay at least that wide — see
    // updateElidedText()/resizeEvent(), which re-elide the *displayed*
    // text by hand to whatever width the layout actually ends up giving
    // these labels.
    titleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    artistLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->addWidget(titleLabel_);
    textLayout->addWidget(artistLabel_);

    coverAndTitle_ = new ClickableArea(this);
    coverAndTitle_->setCursor(Qt::PointingHandCursor);
    auto* coverAndTitleLayout = new QHBoxLayout(coverAndTitle_);
    coverAndTitleLayout->setContentsMargins(8, 8, 8, 8);
    coverAndTitleLayout->addWidget(coverLabel_);
    coverAndTitleLayout->addLayout(textLayout);
    connect(coverAndTitle_, &ClickableArea::clicked, this, [this]() {
        if (!currentWebUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(currentWebUrl_));
    });
    // pixmap() below may return a null placeholder while the cover fetches
    // in the background (see CoverArtCache) — repaint once it's ready.
    connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
        if (url != currentCoverUrl_)
            return;
        QPixmap pixmap = coverCache_->pixmap(url, coverLabel_->size());
        if (!pixmap.isNull())
            coverLabel_->setPixmap(pixmap);
    });

    previousButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-skip-backward")), QString(), this);
    playPauseButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), QString(), this);
    nextButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-skip-forward")), QString(), this);
    stopButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-stop")), QString(), this);
    connect(previousButton_, &QPushButton::clicked, this, &NowPlayingBar::previousClicked);
    connect(playPauseButton_, &QPushButton::clicked, this, &NowPlayingBar::playPauseClicked);
    connect(nextButton_, &QPushButton::clicked, this, &NowPlayingBar::nextClicked);
    connect(stopButton_, &QPushButton::clicked, this, &NowPlayingBar::stopClicked);
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
    auto* controlsColumn = new QVBoxLayout;
    controlsColumn->addLayout(buttonsRow_);
    controlsColumn->addLayout(slidersRow);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 8, 0);
    rootLayout->addWidget(coverAndTitle_);
    rootLayout->addLayout(controlsColumn, 1);
}

void NowPlayingBar::setTrack(const Track& track)
{
    currentTrackTitle_ = track.title;
    QString artistNames;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artistNames += QStringLiteral(", ");
        artistNames += track.artists[i].name;
    }
    currentArtistNames_ = artistNames;
    updateElidedText();
    currentWebUrl_ = track.webUrl.value_or(QString());
    coverAndTitle_->setCursor(currentWebUrl_.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);

    currentCoverUrl_ = track.coverUrl.value_or(QString());
    if (!currentCoverUrl_.isEmpty()) {
        QPixmap pixmap = coverCache_->pixmap(currentCoverUrl_, coverLabel_->size());
        if (!pixmap.isNull())
            coverLabel_->setPixmap(pixmap);
        else
            coverLabel_->clear(); // fetch is in flight — pixmapReady above repaints once it lands
    } else {
        coverLabel_->clear();
    }
}

void NowPlayingBar::setPlaying(bool playing)
{
    playing_ = playing;
    updatePlayPauseIcon();
}

void NowPlayingBar::setLoading(bool loading)
{
    playPauseButton_->setEnabled(!loading);
    playPauseButton_->setIcon(QIcon::fromTheme(
        loading ? QStringLiteral("view-refresh")
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

void NowPlayingBar::updateElidedText()
{
    const QFontMetrics titleMetrics(titleLabel_->font());
    titleLabel_->setText(titleMetrics.elidedText(currentTrackTitle_, Qt::ElideRight, titleLabel_->width()));
    const QFontMetrics artistMetrics(artistLabel_->font());
    artistLabel_->setText(artistMetrics.elidedText(currentArtistNames_, Qt::ElideRight, artistLabel_->width()));
}

void NowPlayingBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateElidedText();
}

} // namespace Ui
