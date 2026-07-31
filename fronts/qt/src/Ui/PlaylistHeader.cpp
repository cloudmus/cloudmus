#include "PlaylistHeader.h"

#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "CoverArtCache.h"

namespace Ui {

namespace {
constexpr int kBannerHeight = 140;

// Cover art varies wildly in color, so text legibility can't rely on the
// system palette here (unlike the rest of the UI) — a fixed dark gradient
// + forced white text over the image is the deliberate exception, applied
// only while a cover is actually showing (see setPlaylist()).
const QString kGradientStyle = QStringLiteral(
    "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 transparent, stop:0.55 transparent, "
    "stop:1 rgba(0,0,0,190));");
const QString kWhiteTextStyle = QStringLiteral("color: white; background: transparent;");
const QString kWhiteDescStyle = QStringLiteral("color: rgba(255,255,255,220); background: transparent;");
} // namespace

PlaylistHeader::PlaylistHeader(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
    , coverCache_(coverCache)
{
    setFixedHeight(kBannerHeight);

    coverLabel_ = new QLabel(this);
    coverLabel_->setScaledContents(true);

    titleLabel_ = new QLabel(this);
    QFont titleFont = titleLabel_->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel_->setFont(titleFont);

    descriptionLabel_ = new QLabel(this);
    descriptionLabel_->setWordWrap(true);

    playButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), tr("Play"), this);
    connect(playButton_, &QPushButton::clicked, this, &PlaylistHeader::playClicked);

    textPanel_ = new QWidget(this);
    auto* textLayout = new QVBoxLayout(textPanel_);
    textLayout->setContentsMargins(12, 12, 12, 12);
    textLayout->addStretch(1);
    textLayout->addWidget(titleLabel_);
    textLayout->addWidget(descriptionLabel_);
    textLayout->addWidget(playButton_, 0, Qt::AlignLeft);

    // StackAll: both coverLabel_ and textPanel_ occupy the full banner
    // rect; setCurrentWidget raises textPanel_ above the cover so its
    // (transparent-background) title/description/button paint over it.
    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(coverLabel_);
    stack->addWidget(textPanel_);
    stack->setCurrentWidget(textPanel_);

    // pixmap() below may return a null placeholder while the cover fetches
    // in the background (see CoverArtCache) — repaint once it's ready.
    connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
        if (url != currentCoverUrl_)
            return;
        applyCover(coverCache_->pixmap(url, size()));
    });

    hide();
}

void PlaylistHeader::setPlaylist(const Playlist& playlist)
{
    titleLabel_->setText(playlist.title);
    const QString description = playlist.description.value_or(QString());
    descriptionLabel_->setText(description);
    descriptionLabel_->setVisible(!description.isEmpty());

    currentCoverUrl_ = playlist.coverUrl.value_or(QString());
    if (!currentCoverUrl_.isEmpty()) {
        coverLabel_->show();
        textPanel_->setStyleSheet(kGradientStyle);
        titleLabel_->setStyleSheet(kWhiteTextStyle);
        descriptionLabel_->setStyleSheet(kWhiteDescStyle);
        applyCover(coverCache_->pixmap(currentCoverUrl_, size()));
    } else {
        coverLabel_->hide();
        coverLabel_->clear();
        // No cover: fall back to plain palette-based styling instead of
        // forcing white-on-transparent text over nothing.
        textPanel_->setStyleSheet(QString());
        titleLabel_->setStyleSheet(QString());
        descriptionLabel_->setStyleSheet(QString());
    }

    show();
}

void PlaylistHeader::setPlayBusy(bool busy)
{
    playButton_->setEnabled(!busy);
    playButton_->setIcon(
        QIcon::fromTheme(busy ? QStringLiteral("view-refresh") : QStringLiteral("media-playback-start")));
}

void PlaylistHeader::applyCover(const QPixmap& pixmap)
{
    if (!pixmap.isNull())
        coverLabel_->setPixmap(pixmap);
    else
        coverLabel_->clear(); // fetch in flight — pixmapReady above repaints once it lands
}

} // namespace Ui
