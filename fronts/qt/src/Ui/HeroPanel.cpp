#include "HeroPanel.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTimer>

#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"
#include "Icons.h"
#include "Metrics.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui {

namespace {
constexpr int kStandardMargin = Theme::Spacing::space4;
constexpr int kItemSpacing = Theme::Spacing::space2;

// Applied as a multiplier of the base app font's point size, not a fixed
// +Npt bump. 2.0 was the first pass; ×(1/1.2) is the follow-up "a bit
// smaller" adjustment on top of that. Title only — the subtitle
// (description/artist name) uses kSubtitleFontScale instead, reported too
// large at this same scale (a source's description in SourcePanel's
// banner in particular).
constexpr qreal kFontScale = 2.0 / 1.2;
constexpr qreal kSubtitleFontScale = 1.0;
// Fraction of the panel's shorter dimension the real-cover overlay
// renders at (see overlayTargetSide()). Same history: 0.5 → ×1.5 → ×(1/1.2).
constexpr qreal kCoverFraction = 0.75 / 1.2;

const QColor kWhiteText = QColor(255, 255, 255);
const QColor kWhiteSubtext = QColor(255, 255, 255, 220);
} // namespace

HeroPanel::HeroPanel(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
    , coverCache_(coverCache)
{
    // Theme::Typography::font(Display/BodySecondary) as the base for this
    // widget's own proportional up-scaling — the immersive hero keeps its
    // existing kFontScale/kSubtitleFontScale multipliers on top (see their
    // doc comment above) rather than switching to the design system's raw
    // display/body-secondary pixel sizes outright, since those are tuned
    // for compact UI text, not a full-bleed hero title.
    titleFont_ = Theme::font(Theme::TextStyle::Display, font());
    titleFont_.setPixelSize(qRound(titleFont_.pixelSize() * kFontScale));

    subtitleFont_ = Theme::font(Theme::TextStyle::BodySecondary, font());
    subtitleFont_.setPixelSize(qRound(subtitleFont_.pixelSize() * kSubtitleFontScale));

    // The one real child widget — see the class doc for why it isn't
    // drawn in paintEvent() like everything else. Not added to any
    // QLayout; relayout() positions it by hand via setGeometry(), so it
    // can never impose a size constraint on this widget either.
    playButton_ = new QPushButton(
        Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::OnAccent, Theme::Metrics::playGlyphSize), QString(),
        this);
    playButton_->setToolTip(tr("Play"));
    playButton_->setObjectName(QStringLiteral("heroPlayButton")); // 40px — see StyleSheet's radius override
    playButton_->setProperty("variant", "play");
    playButton_->setIconSize(QSize(Theme::Metrics::playGlyphSize, Theme::Metrics::playGlyphSize));
    playButton_->setFixedSize(Theme::Metrics::playButtonSize, Theme::Metrics::playButtonSize);
    connect(playButton_, &QPushButton::clicked, this, &HeroPanel::playClicked);
    playButton_->hide(); // nothing to show until applyContent()

    // The background (always) and the cover overlay (when present) depend
    // on size() — debounced rather than regenerated on every single
    // resizeEvent, since a live window/splitter drag fires a lot of these
    // in a row.
    regenerateTimer_ = new QTimer(this);
    regenerateTimer_->setSingleShot(true);
    regenerateTimer_->setInterval(200);
    connect(regenerateTimer_, &QTimer::timeout, this, &HeroPanel::regenerateSizedLayers);

    // pixmap() below may return a null placeholder while the cover fetches
    // in the background (see CoverArtCache) — repaint once it's ready.
    connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
        if (url != currentCoverUrl_)
            return;
        refreshCoverOverlay();
    });

    setFillMode(false);
    backgroundPixmap_ = generateMeshAuraGradientCover(QString(), size());
}

void HeroPanel::setPlaylist(const Playlist& playlist)
{
    applyContent(Content { playlist.title, playlist.description.value_or(QString()),
        playlist.coverUrl.value_or(QString()), playlist.title, /*isPromo=*/true });
}

void HeroPanel::setNowPlaying(const Track& track)
{
    QString artistNames;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artistNames += QStringLiteral(", ");
        artistNames += track.artists[i].name;
    }

    QString coverUrl = track.coverUrl.value_or(QString());
    if (coverUrl.isEmpty() && track.album.has_value())
        coverUrl = track.album->coverUrl.value_or(QString());

    const QString bgSeed
        = (track.album.has_value() && !track.album->title.isEmpty()) ? track.album->title : track.title;

    applyContent(Content { track.title, artistNames, coverUrl, bgSeed, /*isPromo=*/false });
}

void HeroPanel::clearNowPlaying()
{
    regenerateTimer_->stop();
    isPromo_ = true;
    currentCoverUrl_.clear();
    currentBgSeed_.clear();
    titleText_.clear();
    subtitleText_.clear();
    coverPixmap_ = QPixmap();
    playButton_->hide();
    backgroundPixmap_ = generateMeshAuraGradientCover(QString(), size());
    relayout();
    updateGeometry();
}

void HeroPanel::applyContent(const Content& content)
{
    // Cancel a debounced regenerateSizedLayers() left over from resizing
    // the *previous* content's panel — it would otherwise still fire
    // (using currentBgSeed_/currentCoverUrl_, both already updated below)
    // shortly after this synchronous, already-correctly-sized render,
    // redoing the same work for nothing.
    regenerateTimer_->stop();

    isPromo_ = content.isPromo;
    currentCoverUrl_ = content.coverUrl;
    currentBgSeed_ = content.bgSeed;
    titleText_ = content.title;
    subtitleText_ = content.subtitle;

    backgroundPixmap_ = generateMeshAuraGradientCover(currentBgSeed_, size());

    if (!currentCoverUrl_.isEmpty()) {
        refreshCoverOverlay();
    } else {
        coverPixmap_ = QPixmap();
    }

    // heightForWidth()'s return value just changed (new title/subtitle
    // text) but this widget's own geometry didn't — nothing tells the
    // parent layout its cached size hint for this child is stale without
    // this call. See the old PlaylistHeader's identical comment.
    relayout();
    updateGeometry();
}

void HeroPanel::setPlayButtonVisible(bool visible)
{
    playButtonVisible_ = visible;
    relayout();
}

void HeroPanel::setPlayBusy(bool busy)
{
    playButton_->setEnabled(!busy);
    playButton_->setIcon(Theme::icon(busy ? QStringLiteral("refresh") : QStringLiteral("play_arrow"),
        Theme::IconColor::OnAccent, Theme::Metrics::playGlyphSize));
}

void HeroPanel::setFillMode(bool fill)
{
    fillMode_ = fill;
    relayout();
}

QSize HeroPanel::sizeHint() const
{
    const int w = width() > 0 ? width() : 400;
    return QSize(w, heightForWidth(w));
}

bool HeroPanel::hasHeightForWidth() const { return true; }

int HeroPanel::heightForWidth(int w) const
{
    const int availWidth = qMax(0, w - 2 * kStandardMargin);
    const bool hasCover = !currentCoverUrl_.isEmpty();
    const int coverSide = hasCover ? overlayTargetSide() : 0;
    const bool showButton = isPromo_ && playButtonVisible_;

    const QFontMetrics titleMetrics(titleFont_);
    const QFontMetrics subtitleMetrics(subtitleFont_);
    const int titleHeight = titleText_.isEmpty() ? 0 : titleMetrics.height();
    const int subtitleHeight = subtitleText_.isEmpty()
        ? 0
        : subtitleMetrics.boundingRect(QRect(0, 0, availWidth, INT_MAX), Qt::TextWordWrap, subtitleText_).height();
    // size(), not sizeHint(): the button is fixed at 40x40 (design system's
    // circular play-button spec) via setFixedSize() in the constructor,
    // not laid out by a QLayout that would otherwise respect that
    // constraint on its own — sizeHint() would still report the button's
    // unconstrained icon+padding size.
    const int buttonHeight = showButton ? playButton_->size().height() : 0;

    int total = 0;
    if (hasCover)
        total += coverSide + kItemSpacing;
    if (titleHeight > 0)
        total += titleHeight + kItemSpacing;
    if (subtitleHeight > 0)
        total += subtitleHeight + kItemSpacing;
    total += buttonHeight;
    return total + 2 * kStandardMargin;
}

int HeroPanel::overlayTargetSide() const { return qMax(1, qRound(kCoverFraction * qMin(width(), height()))); }

void HeroPanel::refreshCoverOverlay()
{
    if (currentCoverUrl_.isEmpty())
        return;
    const int side = overlayTargetSide();
    // May return a null placeholder while the fetch is in flight —
    // pixmapReady's handler (constructor) calls this again once it lands.
    coverPixmap_ = coverCache_->pixmap(currentCoverUrl_, QSize(side, side));
    relayout();
}

void HeroPanel::regenerateSizedLayers()
{
    // Always keep the background matched to the current size, even with
    // no content set (currentBgSeed_ is just empty then — still a valid,
    // deterministic seed) — otherwise the panel shows a stale render from
    // whatever size it happened to be at construction/last content change.
    backgroundPixmap_ = generateMeshAuraGradientCover(currentBgSeed_, size());
    if (!currentCoverUrl_.isEmpty())
        refreshCoverOverlay();
    else
        update();
}

void HeroPanel::relayout()
{
    const QRect avail = rect().adjusted(kStandardMargin, kStandardMargin, -kStandardMargin, -kStandardMargin);
    const bool hasCover = !currentCoverUrl_.isEmpty();
    const int coverSide = hasCover ? overlayTargetSide() : 0;
    const bool showButton = isPromo_ && playButtonVisible_;

    const QFontMetrics titleMetrics(titleFont_);
    const QFontMetrics subtitleMetrics(subtitleFont_);
    const int titleHeight = titleText_.isEmpty() ? 0 : titleMetrics.height();
    const int subtitleHeight = subtitleText_.isEmpty()
        ? 0
        : subtitleMetrics.boundingRect(QRect(0, 0, avail.width(), INT_MAX), Qt::TextWordWrap, subtitleText_).height();
    const QSize buttonSize = playButton_->size(); // fixed 40x40 — see heightForWidth()'s comment

    int totalHeight = 0;
    if (hasCover)
        totalHeight += coverSide + kItemSpacing;
    if (titleHeight > 0)
        totalHeight += titleHeight + kItemSpacing;
    if (subtitleHeight > 0)
        totalHeight += subtitleHeight + kItemSpacing;
    if (showButton)
        totalHeight += buttonSize.height();

    int y = fillMode_ ? avail.top() + qMax(0, (avail.height() - totalHeight) / 2) : avail.top();
    const auto centeredX = [&](int itemWidth) {
        return fillMode_ ? avail.left() + qMax(0, (avail.width() - itemWidth) / 2) : avail.left();
    };

    coverRect_ = hasCover ? QRect(centeredX(coverSide), y, coverSide, coverSide) : QRect();
    if (hasCover)
        y += coverSide + kItemSpacing;

    titleRect_ = titleHeight > 0 ? QRect(avail.left(), y, avail.width(), titleHeight) : QRect();
    if (titleHeight > 0)
        y += titleHeight + kItemSpacing;

    subtitleRect_ = subtitleHeight > 0 ? QRect(avail.left(), y, avail.width(), subtitleHeight) : QRect();
    if (subtitleHeight > 0)
        y += subtitleHeight + kItemSpacing;

    if (showButton) {
        playButton_->setGeometry(centeredX(buttonSize.width()), y, buttonSize.width(), buttonSize.height());
        playButton_->show();
    } else {
        playButton_->hide();
    }

    update();
}

void HeroPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!backgroundPixmap_.isNull())
        painter.drawPixmap(rect(), backgroundPixmap_);

    // Deliberately the *inverse* of a photographic vignette: dark at the
    // panel's center, fading to transparent at the edges. The center is
    // where the cover+text column sits (relayout() centers it there in
    // fill mode), so this is what actually keeps them legible over a
    // generated background that can be busy/bright at any given point —
    // an edge-darkening vignette would darken exactly the area that least
    // needs it here.
    QRadialGradient vignette(rect().center(), 0.75 * qMax(width(), height()));
    vignette.setColorAt(0.0, QColor(0, 0, 0, 190));
    vignette.setColorAt(0.45, QColor(0, 0, 0, 120));
    vignette.setColorAt(0.75, QColor(0, 0, 0, 40));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), vignette);

    if (!coverPixmap_.isNull() && coverRect_.isValid())
        painter.drawPixmap(coverRect_, coverPixmap_);

    const Qt::Alignment textAlign = fillMode_ ? Qt::AlignHCenter : Qt::AlignLeft;
    if (!titleText_.isEmpty() && titleRect_.isValid()) {
        painter.setFont(titleFont_);
        painter.setPen(kWhiteText);
        painter.drawText(titleRect_, textAlign | Qt::AlignVCenter, titleText_);
    }
    if (!subtitleText_.isEmpty() && subtitleRect_.isValid()) {
        painter.setFont(subtitleFont_);
        painter.setPen(kWhiteSubtext);
        painter.drawText(subtitleRect_, textAlign | Qt::TextWordWrap, subtitleText_);
    }
}

void HeroPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
    // Unconditional — see regenerateSizedLayers()'s comment on why the
    // background needs to stay live even with no content set.
    regenerateTimer_->start(); // restarts the countdown if already running
}

} // namespace Ui
