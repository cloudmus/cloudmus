#include "PlaylistHeader.h"

#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "CoverArtCache.h"
#include "GeneratedCoverArt.h"

namespace Ui {

namespace {
constexpr int kCoverSize = 56; // matches NowPlayingBar's toolbar album art
constexpr int kStandardMargin = 12;

// Cover art varies wildly in color, so text legibility can't rely on the
// system palette here (unlike the rest of the UI, and unlike thumbnail
// mode below) — a fixed dark gradient + forced white text over the image
// is the deliberate exception, applied only in full-bleed mode (no real
// cover — see the class doc). Qt style sheet gradients only support linear
// interpolation *between* stops, no easing curve — two stops alone had a
// visible hard edge where the fade kicks in, since the eye reads a linear
// alpha ramp as harsher than it measures. These extra stops are a
// smoothstep curve (3t²-2t³, the standard cheap cubic ease-in-out) sampled
// at t = 0/0.2/0.4/0.6/0.8/1 across the fade's 0.25-1.0 span (starts a
// quarter of the way down, not just the bottom sliver) and scaled to a 95
// peak alpha (half of a fully opaque-ish 190) — approximating a true cubic
// fade with piecewise-linear segments short enough not to read as separate
// steps.
const QString kGradientStyle = QStringLiteral(
    "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 transparent, stop:0.25 transparent, "
    "stop:0.4 rgba(0,0,0,10), stop:0.55 rgba(0,0,0,33), stop:0.7 rgba(0,0,0,62), "
    "stop:0.85 rgba(0,0,0,85), stop:1 rgba(0,0,0,95));");
const QString kWhiteTextStyle = QStringLiteral("color: white; background: transparent;");
const QString kWhiteDescStyle = QStringLiteral("color: rgba(255,255,255,220); background: transparent;");
} // namespace

PlaylistHeader::PlaylistHeader(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
    , coverCache_(coverCache)
{
    // --- full-bleed mode (no real cover): classic banner, gradient scrim, forced white text ---
    buildTextTrio(fullBleedTitleLabel_, fullBleedDescriptionLabel_, fullBleedPlayButton_);
    fullBleedTitleLabel_->setStyleSheet(kWhiteTextStyle);
    fullBleedDescriptionLabel_->setStyleSheet(kWhiteDescStyle);

    fullBleedTextPanel_ = new QWidget;
    fullBleedTextPanel_->setStyleSheet(kGradientStyle);
    fullBleedTextLayout_ = new QVBoxLayout(fullBleedTextPanel_);
    fullBleedTextLayout_->setContentsMargins(kStandardMargin, kStandardMargin, kStandardMargin, kStandardMargin);
    fullBleedTextLayout_->addStretch(0);
    fullBleedTextLayout_->addWidget(fullBleedTitleLabel_);
    fullBleedTextLayout_->addWidget(fullBleedDescriptionLabel_);
    fullBleedTextLayout_->addWidget(fullBleedPlayButton_, 0, Qt::AlignLeft);
    fullBleedTextLayout_->addStretch(0);

    fullBleedCoverLabel_ = new QLabel;
    fullBleedCoverLabel_->setScaledContents(true);
    // QLabel::minimumSizeHint() for a pixmap-holding label reports the
    // pixmap's own (unscaled) size regardless of setScaledContents — cover
    // art can be a few hundred px on a side, and that was propagating up
    // as this whole widget's minimum size, refusing to let the window
    // shrink narrower/shorter than the cover itself. A plain
    // setMinimumSize(0, 0) does NOT override this (Qt treats an all-zero
    // minimumSize as "unset", falling back to minimumSizeHint() again) —
    // it has to be a genuinely non-zero size.
    fullBleedCoverLabel_->setMinimumSize(1, 1);

    fullBleedPage_ = new QWidget;
    fullBleedStack_ = new QStackedLayout(fullBleedPage_);
    fullBleedStack_->setContentsMargins(0, 0, 0, 0);
    fullBleedStack_->setStackingMode(QStackedLayout::StackAll);
    fullBleedStack_->addWidget(fullBleedCoverLabel_);
    fullBleedStack_->addWidget(fullBleedTextPanel_);
    fullBleedStack_->setCurrentWidget(fullBleedTextPanel_);

    // See resizeEvent()/regenerateFullBleedCover() — full-bleed mode only.
    coverRegenerateTimer_ = new QTimer(this);
    coverRegenerateTimer_->setSingleShot(true);
    coverRegenerateTimer_->setInterval(200);
    connect(coverRegenerateTimer_, &QTimer::timeout, this, &PlaylistHeader::regenerateFullBleedCover);

    // --- thumbnail mode (real cover): small aspect-correct thumbnail beside plain themed text ---
    buildTextTrio(thumbnailTitleLabel_, thumbnailDescriptionLabel_, thumbnailPlayButton_);

    thumbnailCoverLabel_ = new QLabel;
    thumbnailCoverLabel_->setScaledContents(true);

    thumbnailPage_ = new QWidget;
    thumbnailPage_->setObjectName(QStringLiteral("playlistHeaderCard"));
    // palette(...) roles, not hardcoded colors — this sits behind plain
    // text beside a small fixed-size thumbnail, not arbitrary full-bleed
    // photo content, so it follows system theme like ToastNotifier/
    // SourcePanel's authCard_ do. A bottom border only, not a full rounded
    // card outline: this header sits flush edge-to-edge above
    // trackListView_ (no side/top margin between them and the splitter),
    // so a boxed-card look doesn't fit — but palette(base) can be
    // (and, in the dark theme, is) visually identical to the list below
    // it, leaving no visible seam between "header" and "tracks" at all
    // without an explicit divider line.
    thumbnailPage_->setStyleSheet(
        QStringLiteral("#playlistHeaderCard { background: palette(base); border-bottom: 1px solid palette(mid); }"));

    // titleLabel_/descriptionLabel_ stay stretched to the full column width
    // in both modes (their own setAlignment(), toggled in setPlaylist(),
    // handles left-vs-centered *text*) — word-wrapped description
    // specifically must not get an alignment flag here instead: an
    // unstretched item is sized to sizeHint(), and a word-wrapped label's
    // sizeHint() width comes from an arbitrary internal Qt heuristic
    // (~256px in testing), which would lock its wrap width to that instead
    // of the column's actual width.
    thumbnailTextLayout_ = new QVBoxLayout;
    thumbnailTextLayout_->addWidget(thumbnailTitleLabel_);
    thumbnailTextLayout_->addWidget(thumbnailDescriptionLabel_);
    thumbnailTextLayout_->addWidget(thumbnailPlayButton_, 0, Qt::AlignLeft);

    auto* thumbnailRow = new QHBoxLayout;
    thumbnailRow->setSpacing(kStandardMargin);
    thumbnailRow->addWidget(thumbnailCoverLabel_);
    thumbnailRow->setAlignment(thumbnailCoverLabel_, Qt::AlignVCenter);
    thumbnailRow->addLayout(thumbnailTextLayout_, 1);

    // Top/bottom stretch toggled in setPlaylist(): a browsable playlist/
    // liked banner is packed to the top (stretch 0, so the row doesn't get
    // pushed anywhere), a radioStation (My Wave) one — which has no track
    // list underneath eating the rest of the window — is vertically
    // centered instead, by widening this widget itself (MainWindow.cpp
    // gives playlistHeader_ the layout stretch trackListView_ would
    // otherwise claim, whenever trackListView_ is hidden) and by the
    // stretch on both sides here centering the row within that extra
    // height.
    thumbnailOuterLayout_ = new QVBoxLayout(thumbnailPage_);
    thumbnailOuterLayout_->setContentsMargins(kStandardMargin, kStandardMargin, kStandardMargin, kStandardMargin);
    thumbnailOuterLayout_->addStretch(0);
    thumbnailOuterLayout_->addLayout(thumbnailRow);
    thumbnailOuterLayout_->addStretch(0);

    // --- mode switch: two fully independent, fully-built pages ---
    pageStack_ = new QStackedLayout(this);
    pageStack_->setContentsMargins(0, 0, 0, 0);
    pageStack_->addWidget(fullBleedPage_);
    pageStack_->addWidget(thumbnailPage_);

    // pixmap() below may return a null placeholder while the cover fetches
    // in the background (see CoverArtCache) — repaint once it's ready.
    // Only relevant to thumbnail mode: full-bleed mode never has a real
    // currentCoverUrl_ to match against (it always uses a generated
    // cover), so this never fires while that mode is active.
    connect(coverCache_, &CoverArtCache::pixmapReady, this, [this](const QString& url) {
        if (url != currentCoverUrl_)
            return;
        applyThumbnailCover(coverCache_->pixmap(url, QSize(1, kCoverSize)));
    });

    hide();
}

void PlaylistHeader::buildTextTrio(QLabel*& title, QLabel*& description, QPushButton*& button)
{
    title = new QLabel;
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);

    description = new QLabel;
    description->setWordWrap(true);

    button = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), tr("Play"));
    connect(button, &QPushButton::clicked, this, &PlaylistHeader::playClicked);
}

void PlaylistHeader::setPlaylist(const Playlist& playlist)
{
    // Cancel a debounced regenerateFullBleedCover() left over from resizing
    // the *previous* playlist's banner — it would otherwise still fire
    // (using currentTitle_/currentCoverUrl_, both already updated below)
    // shortly after this synchronous, already-correctly-sized generate,
    // redoing the same work for nothing.
    coverRegenerateTimer_->stop();
    currentTitle_ = playlist.title;
    currentCoverUrl_ = playlist.coverUrl.value_or(QString());

    const QString description = playlist.description.value_or(QString());
    // radioStation (My Wave) has no track list below this banner (see
    // MainWindow::showPlaylistAsync()) — MainWindow.cpp hands its layout
    // stretch to playlistHeader_ in that case, so there's real extra
    // height to center within; a browsable kind packs to the top instead,
    // staying out of the track list's way.
    const bool hasTrackList = playlist.kind != QStringLiteral("radioStation");
    const Qt::Alignment textAlign = hasTrackList ? Qt::AlignLeft : Qt::AlignHCenter;

    // Both modes' text content stays in sync regardless of which is
    // currently visible — see the class doc.
    fullBleedTitleLabel_->setText(playlist.title);
    fullBleedTitleLabel_->setAlignment(textAlign);
    fullBleedDescriptionLabel_->setText(description);
    fullBleedDescriptionLabel_->setVisible(!description.isEmpty());
    fullBleedDescriptionLabel_->setAlignment(textAlign);
    fullBleedTextLayout_->setAlignment(fullBleedPlayButton_, textAlign);
    fullBleedTextLayout_->setStretch(0, hasTrackList ? 0 : 1);
    fullBleedTextLayout_->setStretch(fullBleedTextLayout_->count() - 1, hasTrackList ? 0 : 1);

    thumbnailTitleLabel_->setText(playlist.title);
    thumbnailTitleLabel_->setAlignment(textAlign);
    thumbnailDescriptionLabel_->setText(description);
    thumbnailDescriptionLabel_->setVisible(!description.isEmpty());
    thumbnailDescriptionLabel_->setAlignment(textAlign);
    thumbnailTextLayout_->setAlignment(thumbnailPlayButton_, textAlign);
    thumbnailOuterLayout_->setStretch(0, hasTrackList ? 0 : 1);
    thumbnailOuterLayout_->setStretch(thumbnailOuterLayout_->count() - 1, hasTrackList ? 0 : 1);

    if (currentCoverUrl_.isEmpty()) {
        pageStack_->setCurrentWidget(fullBleedPage_);
        applyFullBleedCover(generateMeshAuraGradientCover(playlist.title, size()));
    } else {
        pageStack_->setCurrentWidget(thumbnailPage_);
        // A deliberately tiny target width alongside the real target
        // height: CoverArtCache scales via Qt::KeepAspectRatioByExpanding
        // (picks whichever scale factor satisfies *both* target
        // dimensions, i.e. the larger one) — for any real image, the
        // height constraint (kCoverSize) dominates over an ~1px width
        // constraint, so the result comes back already exactly
        // height=kCoverSize with an aspect-correct width, no cropping.
        // See applyThumbnailCover().
        applyThumbnailCover(coverCache_->pixmap(currentCoverUrl_, QSize(1, kCoverSize)));
    }

    // heightForWidth()'s return value just changed (new title/description
    // text, possibly a mode switch) but this widget's own geometry didn't
    // — nothing tells the parent layout its cached size hint for this
    // child is stale without this call. Omitting it is exactly what made
    // the banner's height inconsistent/not-minimal switching between
    // playlists: the parent kept using whichever size hint happened to be
    // cached from before, only catching up whenever some unrelated event
    // (e.g. a window resize) forced a fresh layout pass.
    updateGeometry();
    show();
}

void PlaylistHeader::setPlayButtonVisible(bool visible)
{
    fullBleedPlayButton_->setVisible(visible);
    thumbnailPlayButton_->setVisible(visible);
}

void PlaylistHeader::setPlayBusy(bool busy)
{
    const QIcon icon
        = QIcon::fromTheme(busy ? QStringLiteral("view-refresh") : QStringLiteral("media-playback-start"));
    fullBleedPlayButton_->setEnabled(!busy);
    fullBleedPlayButton_->setIcon(icon);
    thumbnailPlayButton_->setEnabled(!busy);
    thumbnailPlayButton_->setIcon(icon);
}

QSize PlaylistHeader::sizeHint() const
{
    const int w = width() > 0 ? width() : 400;
    return QSize(w, heightForWidth(w));
}

bool PlaylistHeader::hasHeightForWidth() const { return true; }

int PlaylistHeader::heightForWidth(int w) const
{
    // Neither this widget's own top-level layout (pageStack_) nor
    // fullBleedStack_ propagate heightForWidth on their own —
    // QStackedLayout doesn't implement it regardless of stacking mode. The
    // relevant mode's own QVBoxLayout does (plain QBoxLayout) — ask it
    // directly, accounting for how much width it actually gets in each
    // mode.
    if (currentCoverUrl_.isEmpty()) {
        // Full-bleed mode: fullBleedTextPanel_ fills this widget's full
        // width (its container is a StackAll stack that itself fills
        // fullBleedPage_, which fills this widget via pageStack_) — its own
        // layout's contentsMargins already account for the banner padding.
        const int hfw = fullBleedTextLayout_->heightForWidth(w);
        return hfw >= 0 ? hfw : fullBleedTextLayout_->sizeHint().height();
    }
    // Thumbnail mode: thumbnailTextLayout_ only gets what's left of this
    // widget's width after thumbnailOuterLayout_'s own left/right margins,
    // the cover's fixed aspect-correct width, and the row spacing between
    // them — and the result needs thumbnailOuterLayout_'s top/bottom
    // margins added back (thumbnailTextLayout_ has no margins of its own,
    // so heightForWidth() below reports the unpadded content height only).
    const int available = w - 2 * kStandardMargin - thumbnailCoverLabel_->width() - kStandardMargin;
    const int hfw = thumbnailTextLayout_->heightForWidth(qMax(available, 0));
    const int textHeight = hfw >= 0 ? hfw : thumbnailTextLayout_->sizeHint().height();
    return qMax(textHeight, thumbnailCoverLabel_->height()) + 2 * kStandardMargin;
}

void PlaylistHeader::applyFullBleedCover(const QPixmap& pixmap)
{
    if (!pixmap.isNull())
        fullBleedCoverLabel_->setPixmap(pixmap);
    else
        fullBleedCoverLabel_->clear(); // fetch in flight — never actually happens in this mode, kept for symmetry
}

void PlaylistHeader::applyThumbnailCover(const QPixmap& pixmap)
{
    if (pixmap.isNull()) {
        thumbnailCoverLabel_->clear(); // fetch in flight — pixmapReady above repaints once it lands
        return;
    }
    // CoverArtCache's returned pixmap size *is* the correct aspect-correct
    // footprint already (see setPlaylist()'s call site comment) — mirror
    // the label's fixed size to match it exactly rather than letting
    // setScaledContents further stretch/distort it.
    thumbnailCoverLabel_->setFixedSize(pixmap.size());
    thumbnailCoverLabel_->setPixmap(pixmap);
    // heightForWidth() depends on thumbnailCoverLabel_'s own width/height —
    // when this runs from the async pixmapReady path (cache miss during
    // setPlaylist()'s own synchronous call, see the constructor), that
    // dependency just changed well after setPlaylist()'s own
    // updateGeometry() call already ran, so it needs its own here too.
    updateGeometry();
}

void PlaylistHeader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Real cover art (thumbnail mode) isn't resize-driven at all — its
    // size depends only on the source image's own aspect ratio, not this
    // widget's size. currentTitle_ empty means no playlist has been set
    // yet (hide()d, nothing to regenerate for).
    if (currentCoverUrl_.isEmpty() && !currentTitle_.isEmpty())
        coverRegenerateTimer_->start(); // restarts the countdown if already running — see the header's comment
}

void PlaylistHeader::regenerateFullBleedCover() { applyFullBleedCover(generateMeshAuraGradientCover(currentTitle_, size())); }

} // namespace Ui
