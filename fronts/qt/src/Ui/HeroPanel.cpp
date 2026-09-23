#include "HeroPanel.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTimer>

#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"
#include "Icons.h"
#include "Metrics.h"
#include "Radius.h"
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

// New cover grows in from 150%, a leaving one shrinks away to 50% —
// both fading. The exit is shorter and accelerates (InCubic) so the old
// cover gets out of the way while the new one settles (OutCubic).
const TransitionEffect kCoverEnter { 450, QEasingCurve::OutCubic, { 0.0, 1.5 } };
const TransitionEffect kCoverExit { 300, QEasingCurve::InCubic, { 0.0, 0.5 } };
const TransitionEffect kTextEnter { 350, QEasingCurve::OutCubic, { 0.0, 1.0 } };
const TransitionEffect kTextExit { 200, QEasingCurve::InQuad, { 0.0, 1.0 } };
// The old background stays fully opaque underneath (exit to {1, 1}) for
// as long as the new one takes to fade in over it — fading both at once
// would let the window behind show through mid-transition.
const TransitionEffect kBackgroundEnter { 500, QEasingCurve::InOutQuad, { 0.0, 1.0 } };
const TransitionEffect kBackgroundExit { 500, QEasingCurve::Linear, { 1.0, 1.0 } };

const QColor kWhiteText = QColor(255, 255, 255);
const QColor kWhiteSubtext = QColor(255, 255, 255, 220);
} // namespace

HeroPanel::HeroPanel(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
    , coverCache_(coverCache)
    , backgroundLayer_(this, kBackgroundEnter, kBackgroundExit)
    , coverLayer_(this, kCoverEnter, kCoverExit)
    , titleLayer_(this, kTextEnter, kTextExit)
    , subtitleLayer_(this, kTextEnter, kTextExit)
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
    backgroundLayer_.show(generateMeshAuraGradientCover(QString(), size()), /*animate=*/false);
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
    titleText_.clear();
    subtitleText_.clear();
    coverLayer_.hide();
    titleLayer_.hide();
    subtitleLayer_.hide();
    playButton_->hide();
    if (!currentBgSeed_.isEmpty()) {
        currentBgSeed_.clear();
        backgroundLayer_.show(generateMeshAuraGradientCover(QString(), size()));
    }
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
    titleText_ = content.title;
    subtitleText_ = content.subtitle;
    setTextLayer(titleLayer_, titleText_);
    setTextLayer(subtitleLayer_, subtitleText_);

    // Same seed/cover (e.g. the next track off the same album) keeps the
    // existing background and cover as they are instead of re-animating
    // them into an identical copy of themselves.
    if (content.bgSeed != currentBgSeed_) {
        currentBgSeed_ = content.bgSeed;
        backgroundLayer_.show(generateMeshAuraGradientCover(currentBgSeed_, size()));
    }

    if (content.coverUrl != currentCoverUrl_) {
        currentCoverUrl_ = content.coverUrl;
        coverLayer_.hide();
    }
    refreshCoverOverlay();

    // heightForWidth()'s return value just changed (new title/subtitle
    // text) but this widget's own geometry didn't — nothing tells the
    // parent layout its cached size hint for this child is stale without
    // this call. See the old PlaylistHeader's identical comment.
    relayout();
    updateGeometry();
}

void HeroPanel::setTextLayer(LayerTransition<TextLayer>& layer, const QString& text)
{
    const TextLayer* current = layer.current();
    if (current && current->text == text)
        return;
    if (text.isEmpty())
        layer.hide();
    else
        layer.show(TextLayer { text, QRect(), 0 });
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
    // pixmapReady's handler (constructor) calls this again once it lands,
    // so the new cover's entrance starts when there's actually something
    // to show, not when its URL arrives.
    const QPixmap pixmap = coverCache_->pixmap(currentCoverUrl_, QSize(side, side));
    if (!pixmap.isNull()) {
        CoverLayer* current = coverLayer_.current();
        if (current && current->url == currentCoverUrl_)
            current->pixmap = pixmap; // same cover re-rendered for a new size — no animation
        else
            coverLayer_.show(CoverLayer { currentCoverUrl_, pixmap, coverRect_ });
    }
    relayout();
}

void HeroPanel::regenerateSizedLayers()
{
    // Always keep the background matched to the current size, even with
    // no content set (currentBgSeed_ is just empty then — still a valid,
    // deterministic seed) — otherwise the panel shows a stale render from
    // whatever size it happened to be at construction/last content change.
    edgeFade_ = generateMeshAuraEdgeFade(size(), edgeFadeColor_);
    // Crossfades from the stretched old render to the sharp new one.
    backgroundLayer_.show(generateMeshAuraGradientCover(currentBgSeed_, size()));
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

    // Only the current layers follow the new layout; leaving ones finish
    // their exit where they were.
    const Qt::Alignment textAlign = fillMode_ ? Qt::AlignHCenter : Qt::AlignLeft;
    if (CoverLayer* cover = coverLayer_.current())
        cover->rect = coverRect_;
    if (TextLayer* title = titleLayer_.current()) {
        title->rect = titleRect_;
        title->flags = textAlign | Qt::AlignVCenter;
    }
    if (TextLayer* subtitle = subtitleLayer_.current()) {
        subtitle->rect = subtitleRect_;
        subtitle->flags = textAlign | Qt::TextWordWrap;
    }

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

    backgroundLayer_.forEach([&](const QPixmap& pixmap, const LayerState& state) {
        painter.setOpacity(state.opacity);
        painter.drawPixmap(rect(), pixmap);
    });
    painter.setOpacity(1.0);

    // Static, above the animated background layers — see
    // generateMeshAuraEdgeFade(). Stretched until the debounced
    // regenerateSizedLayers() catches up with a resize, same as the
    // background itself. The window color is what used to show through
    // the cover's own translucent edges before it became opaque.
    const QColor edgeColor = palette().color(QPalette::Window);
    if (edgeFade_.isNull() || edgeFadeColor_ != edgeColor) {
        edgeFade_ = generateMeshAuraEdgeFade(size(), edgeColor);
        edgeFadeColor_ = edgeColor;
    }
    painter.drawPixmap(rect(), edgeFade_);

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

    // 2x Radius::md, not the bare token: this cover is large enough that
    // md's own 8px (right for #sourceAuthCard and other panels at their
    // usual size) read as barely-rounded here — doubled rather than
    // reused, since this is now specific to how big this particular cover
    // renders, not the shared panel radius itself.
    constexpr int kCoverRadius = Theme::Radius::md * 2;
    coverLayer_.forEach([&](const CoverLayer& cover, const LayerState& state) {
        if (cover.pixmap.isNull() || !cover.rect.isValid())
            return;
        painter.save();
        applyLayerState(painter, state, QRectF(cover.rect).center());
        QPainterPath clip;
        clip.addRoundedRect(cover.rect, kCoverRadius, kCoverRadius);
        painter.setClipPath(clip);
        painter.drawPixmap(cover.rect, cover.pixmap);
        painter.restore();
    });

    const auto paintText = [&](const QFont& font, const QColor& color) {
        return [&painter, font, color](const TextLayer& text, const LayerState& state) {
            if (!text.rect.isValid())
                return;
            painter.save();
            applyLayerState(painter, state, QRectF(text.rect).center());
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(text.rect, text.flags, text.text);
            painter.restore();
        };
    };
    titleLayer_.forEach(paintText(titleFont_, kWhiteText));
    subtitleLayer_.forEach(paintText(subtitleFont_, kWhiteSubtext));
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
