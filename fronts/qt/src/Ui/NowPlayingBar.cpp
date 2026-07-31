#include "NowPlayingBar.h"

#include <QDesktopServices>
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
    setFixedHeight(72);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    coverLabel_ = new QLabel(this);
    coverLabel_->setFixedSize(56, 56);
    coverLabel_->setScaledContents(true);

    titleLabel_ = new QLabel(tr("Nothing playing"), this);
    artistLabel_ = new QLabel(this);
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
    auto* transportLayout = new QHBoxLayout;
    transportLayout->addWidget(previousButton_);
    transportLayout->addWidget(playPauseButton_);
    transportLayout->addWidget(nextButton_);
    transportLayout->addWidget(stopButton_);

    elapsedLabel_ = new QLabel(QStringLiteral("0:00"), this);
    durationLabel_ = new QLabel(QStringLiteral("0:00"), this);
    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setRange(0, 0);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { userIsDraggingSeek_ = true; });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
        userIsDraggingSeek_ = false;
        emit seekRequested(seekSlider_->value());
    });
    auto* seekLayout = new QHBoxLayout;
    seekLayout->addWidget(elapsedLabel_);
    seekLayout->addWidget(seekSlider_);
    seekLayout->addWidget(durationLabel_);

    volumeSlider_ = new QSlider(Qt::Horizontal, this);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setFixedWidth(100);
    connect(volumeSlider_, &QSlider::valueChanged, this, &NowPlayingBar::volumeChanged);
    auto* volumeLayout = new QHBoxLayout;
    volumeLayout->addWidget(
        new QLabel(QString::fromUtf8("\xF0\x9F\x94\x8A"), this)); // 🔊, harmless if the font lacks it
    volumeLayout->addWidget(volumeSlider_);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 8, 0);
    rootLayout->addWidget(coverAndTitle_);
    rootLayout->addLayout(transportLayout);
    rootLayout->addLayout(seekLayout, 1);
    rootLayout->addLayout(volumeLayout);
}

void NowPlayingBar::setTrack(const Track& track)
{
    titleLabel_->setText(track.title);
    QString artistNames;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artistNames += QStringLiteral(", ");
        artistNames += track.artists[i].name;
    }
    artistLabel_->setText(artistNames);
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

} // namespace Ui
