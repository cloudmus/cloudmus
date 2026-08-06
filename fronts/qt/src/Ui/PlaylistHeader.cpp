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
    // --- shared text content, moves between modes (see setPlaylist()) ---
    titleLabel_ = new QLabel;
    QFont titleFont = titleLabel_->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel_->setFont(titleFont);

    descriptionLabel_ = new QLabel;
    descriptionLabel_->setWordWrap(true);

    playButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), tr("Play"));
    connect(playButton_, &QPushButton::clicked, this, &PlaylistHeader::playClicked);

    // titleLabel_/descriptionLabel_ stay stretched to the full column width
    // (their own setAlignment(), toggled in setPlaylist(), handles
    // left-vs-centered *text*) — word-wrapped descriptionLabel_
    // specifically must not get an alignment flag here instead: an
    // unstretched item is sized to sizeHint(), and a word-wrapped label's
    // sizeHint() width comes from an arbitrary internal Qt heuristic
    // (~256px in testing), which would lock its wrap width to that instead
    // of the column's actual width. playButton_ does need its alignment
    // flag toggled (Left/HCenter) — unlike the labels it isn't
    // transparent-background, so left as stretched it would visually
    // become a full-width button in both modes. The two addStretch()es are
    // only load-bearing in full-bleed mode (centers this column within the
    // whole banner height when there's no track list below — see
    // setPlaylist()); thumbnail mode keeps both at 0 and centers the whole
    // cover+text row instead, one level further out.
    textPanel_ = new QWidget;
    textLayout_ = new QVBoxLayout(textPanel_);
    textLayout_->addStretch(0);
    textLayout_->addWidget(titleLabel_);
    textLayout_->addWidget(descriptionLabel_);
    textLayout_->addWidget(playButton_, 0, Qt::AlignLeft);
    textLayout_->addStretch(0);

    // --- full-bleed mode (no real cover) ---
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
    // textPanel_ itself is added/removed here dynamically by setPlaylist()
    // depending on mode, not fixed at construction time.

    // See resizeEvent()/regenerateFullBleedCover() — full-bleed mode only.
    coverRegenerateTimer_ = new QTimer(this);
    coverRegenerateTimer_->setSingleShot(true);
    coverRegenerateTimer_->setInterval(200);
    connect(coverRegenerateTimer_, &QTimer::timeout, this, &PlaylistHeader::regenerateFullBleedCover);

    // --- thumbnail mode (real cover) ---
    thumbnailCoverLabel_ = new QLabel;
    thumbnailCoverLabel_->setScaledContents(true);

    thumbnailPage_ = new QWidget;
    thumbnailPage_->setObjectName(QStringLiteral("playlistHeaderCard"));
    // palette(...) role, not a hardcoded color — this sits behind plain
    // text beside a small fixed-size thumbnail, not arbitrary full-bleed
    // photo content, so it follows system theme like ToastNotifier/
    // SourcePanel's authCard_ do.
    thumbnailPage_->setStyleSheet(
        QStringLiteral("#playlistHeaderCard { background: palette(base); border-radius: 8px; }"));

    thumbnailRow_ = new QHBoxLayout;
    thumbnailRow_->setSpacing(kStandardMargin);
    thumbnailRow_->addWidget(thumbnailCoverLabel_);
    thumbnailRow_->setAlignment(thumbnailCoverLabel_, Qt::AlignVCenter);
    // textPanel_ added here dynamically by setPlaylist(), stretch 1 to
    // claim the remaining row width.

    thumbnailOuterLayout_ = new QVBoxLayout(thumbnailPage_);
    thumbnailOuterLayout_->setContentsMargins(kStandardMargin, kStandardMargin, kStandardMargin, kStandardMargin);
    thumbnailOuterLayout_->addStretch(0);
    thumbnailOuterLayout_->addLayout(thumbnailRow_);
    thumbnailOuterLayout_->addStretch(0);

    // --- mode switch ---
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

    titleLabel_->setText(playlist.title);
    const QString description = playlist.description.value_or(QString());
    descriptionLabel_->setText(description);
    descriptionLabel_->setVisible(!description.isEmpty());

    // radioStation (My Wave) has no track list below this banner (see
    // MainWindow::showPlaylistAsync()) — MainWindow.cpp hands its layout
    // stretch to playlistHeader_ in that case, so there's real extra
    // height to center within; a browsable kind packs to the top instead,
    // staying out of the track list's way.
    const bool hasTrackList = playlist.kind != QStringLiteral("radioStation");
    const Qt::Alignment textAlign = hasTrackList ? Qt::AlignLeft : Qt::AlignHCenter;
    titleLabel_->setAlignment(textAlign);
    descriptionLabel_->setAlignment(textAlign);
    textLayout_->setAlignment(playButton_, textAlign);

    const bool hasRealCover = !currentCoverUrl_.isEmpty();
    if (hasRealCover) {
        if (textPanel_->parentWidget() != thumbnailPage_) {
            fullBleedStack_->removeWidget(textPanel_);
            textLayout_->setContentsMargins(0, 0, 0, 0); // thumbnailOuterLayout_ already pads the whole row
            textPanel_->setStyleSheet(QString());
            titleLabel_->setStyleSheet(QString());
            descriptionLabel_->setStyleSheet(QString());
            thumbnailRow_->addWidget(textPanel_, 1);
        }
        // Centering here happens one level out (the whole cover+text row,
        // via thumbnailOuterLayout_) rather than within textLayout_ itself
        // — see the constructor's comment on textLayout_'s stretches.
        textLayout_->setStretch(0, 0);
        textLayout_->setStretch(textLayout_->count() - 1, 0);
        thumbnailOuterLayout_->setStretch(0, hasTrackList ? 0 : 1);
        thumbnailOuterLayout_->setStretch(thumbnailOuterLayout_->count() - 1, hasTrackList ? 0 : 1);

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
    } else {
        if (textPanel_->parentWidget() != fullBleedPage_) {
            thumbnailRow_->removeWidget(textPanel_);
            textLayout_->setContentsMargins(kStandardMargin, kStandardMargin, kStandardMargin, kStandardMargin);
            textPanel_->setStyleSheet(kGradientStyle);
            titleLabel_->setStyleSheet(kWhiteTextStyle);
            descriptionLabel_->setStyleSheet(kWhiteDescStyle);
            fullBleedStack_->addWidget(textPanel_);
        }
        fullBleedStack_->setCurrentWidget(textPanel_); // raise above fullBleedCoverLabel_
        textLayout_->setStretch(0, hasTrackList ? 0 : 1);
        textLayout_->setStretch(textLayout_->count() - 1, hasTrackList ? 0 : 1);

        pageStack_->setCurrentWidget(fullBleedPage_);
        applyFullBleedCover(generateMeshAuraGradientCover(playlist.title, size()));
    }

    show();
}

void PlaylistHeader::setPlayButtonVisible(bool visible) { playButton_->setVisible(visible); }

void PlaylistHeader::setPlayBusy(bool busy)
{
    playButton_->setEnabled(!busy);
    playButton_->setIcon(
        QIcon::fromTheme(busy ? QStringLiteral("view-refresh") : QStringLiteral("media-playback-start")));
}

QSize PlaylistHeader::sizeHint() const
{
    const int w = width() > 0 ? width() : 400;
    return QSize(w, heightForWidth(w));
}

bool PlaylistHeader::hasHeightForWidth() const { return true; }

int PlaylistHeader::heightForWidth(int w) const
{
    // Neither this widget's own top-level layout (pageStack_) nor, in
    // full-bleed mode, fullBleedStack_ propagate heightForWidth on their
    // own — QStackedLayout doesn't implement it regardless of stacking
    // mode. textPanel_'s own QVBoxLayout does (it's a plain QBoxLayout,
    // same as the rest of this method relies on) — ask it directly in
    // both modes, just accounting for how much width it actually gets in
    // each.
    if (currentCoverUrl_.isEmpty()) {
        // Full-bleed mode: textPanel_ fills this widget's full width (see
        // the constructor — its container is a StackAll stack that itself
        // fills fullBleedPage_, which fills this widget via pageStack_).
        const int hfw = textPanel_->layout()->heightForWidth(w);
        return hfw >= 0 ? hfw : textPanel_->layout()->sizeHint().height();
    }
    // Thumbnail mode: textPanel_ only gets what's left of this widget's
    // width after thumbnailOuterLayout_'s own left/right margins, the
    // cover's fixed aspect-correct width, and the row spacing between them
    // — and the result needs thumbnailOuterLayout_'s top/bottom margins
    // added back (textLayout_'s own margins are zero in this mode — see
    // setPlaylist() — so heightForWidth() below reports the unpadded
    // content height only).
    const int available = w - 2 * kStandardMargin - thumbnailCoverLabel_->width() - kStandardMargin;
    const int hfw = textPanel_->layout()->heightForWidth(qMax(available, 0));
    const int textHeight = hfw >= 0 ? hfw : textPanel_->layout()->sizeHint().height();
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
