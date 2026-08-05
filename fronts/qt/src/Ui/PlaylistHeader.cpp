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
    // Deliberately NOT QSizePolicy::Fixed, tempting as that looks for "this
    // widget's height shouldn't grow with the window": combined with
    // hasHeightForWidth()==true, Fixed makes the layout treat sizeHint() —
    // which depends on *width* here (descriptionLabel_ word-wraps) — as
    // BOTH the minimum and maximum height, recomputed every time the
    // widget's width changes. That couples width and height into a
    // feedback loop (narrower -> more wrapped lines -> taller forced
    // minimum -> fights any further resize), which is what made the whole
    // window balk at being resized/shrunk vertically once a
    // long-description playlist (e.g. My Wave) was showing. Staying at the
    // default Preferred keeps heightForWidth()/sizeHint() driving the
    // *preferred* size without hard-locking min==max — the actual "don't
    // grow with the window" behavior comes from MainWindow.cpp's
    // addWidget(trackListView_, 1): giving the sibling all the stretch
    // means this widget, having none, gets none of the leftover space
    // either, without needing Fixed at all.
    coverLabel_ = new QLabel(this);
    coverLabel_->setScaledContents(true);
    // QLabel::minimumSizeHint() for a pixmap-holding label reports the
    // pixmap's own (unscaled) size regardless of setScaledContents — cover
    // art can be a few hundred px on a side, and that was propagating up
    // through the QStackedLayout below as this whole widget's minimum size,
    // refusing to let the window shrink narrower/shorter than the cover
    // itself. A plain setMinimumSize(0, 0) does NOT override this (Qt
    // treats an all-zero minimumSize as "unset", falling back to
    // minimumSizeHint() again) — it has to be a genuinely non-zero size.
    coverLabel_->setMinimumSize(1, 1);

    titleLabel_ = new QLabel(this);
    QFont titleFont = titleLabel_->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel_->setFont(titleFont);

    descriptionLabel_ = new QLabel(this);
    descriptionLabel_->setWordWrap(true);

    playButton_ = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), tr("Play"), this);
    connect(playButton_, &QPushButton::clicked, this, &PlaylistHeader::playClicked);

    // Two layouts sharing one set of widgets, toggled per setPlaylist() call
    // (see there) rather than rebuilt: a browsable playlist/liked banner is
    // packed to the top-left (indices 0/4 below at stretch 0, so the block
    // doesn't get pushed anywhere), a radioStation (My Wave) one — which has
    // no track list underneath eating the rest of the window — is centered,
    // both by widening this widget itself (MainWindow.cpp gives
    // playlistHeader_ the layout stretch trackListView_ would otherwise
    // claim, whenever trackListView_ is hidden) and by the stretch on both
    // sides here centering the group within that extra height.
    //
    // titleLabel_/descriptionLabel_ stay stretched to the full width in
    // both modes (their own setAlignment(), toggled in setPlaylist(),
    // handles left-vs-centered *text*) — word-wrapped descriptionLabel_
    // specifically must not get an alignment flag here instead: an
    // unstretched item is sized to sizeHint(), and a word-wrapped label's
    // sizeHint() width comes from an arbitrary internal Qt heuristic
    // (~256px in testing), which would lock its wrap width to that instead
    // of the banner's actual width. playButton_ does need its alignment
    // flag toggled (Left/HCenter) — unlike the labels it isn't
    // transparent-background, so left as stretched it would visually become
    // a full-width button in both modes.
    textPanel_ = new QWidget(this);
    textLayout_ = new QVBoxLayout(textPanel_);
    textLayout_->setContentsMargins(12, 12, 12, 12);
    textLayout_->addStretch(0);
    textLayout_->addWidget(titleLabel_);
    textLayout_->addWidget(descriptionLabel_);
    textLayout_->addWidget(playButton_, 0, Qt::AlignLeft);
    textLayout_->addStretch(0);

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

    // radioStation (My Wave) has no track list below this banner (see
    // showPlaylistAsync()) — MainWindow.cpp hands its layout stretch to
    // playlistHeader_ in that case, so top/bottom stretch here actually has
    // room to center the group in; a browsable kind packs to the top-left
    // instead, staying out of the track list's way. See the constructor's
    // comment on this same layout for the full picture.
    const bool hasTrackList = playlist.kind != QStringLiteral("radioStation");
    const Qt::Alignment textAlign = hasTrackList ? Qt::AlignLeft : Qt::AlignHCenter;
    titleLabel_->setAlignment(textAlign);
    descriptionLabel_->setAlignment(textAlign);
    textLayout_->setAlignment(playButton_, textAlign);
    textLayout_->setStretch(0, hasTrackList ? 0 : 1);
    textLayout_->setStretch(textLayout_->count() - 1, hasTrackList ? 0 : 1);

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

void PlaylistHeader::setPlayButtonVisible(bool visible) { playButton_->setVisible(visible); }

QSize PlaylistHeader::sizeHint() const
{
    const int w = width() > 0 ? width() : 400;
    return QSize(w, heightForWidth(w));
}

bool PlaylistHeader::hasHeightForWidth() const { return true; }

int PlaylistHeader::heightForWidth(int w) const
{
    // The outer QStackedLayout (see the constructor's comment on why text
    // is overlaid on the cover via StackAll) doesn't implement
    // heightForWidth itself — unlike QVBoxLayout, which is what's actually
    // needed here since descriptionLabel_ word-wraps. Ask textPanel_'s own
    // QVBoxLayout directly instead of relying on QWidget's usual
    // layout()-delegating default, which would ask the stacked layout and
    // get nothing useful back.
    const int hfw = textPanel_->layout()->heightForWidth(w);
    // -1 is QLayout's own "not width-dependent, use sizeHint() instead"
    // convention — reached whenever descriptionLabel_ is hidden (no
    // visible child wraps, so the layout has nothing width-dependent left
    // to report). Not "zero height": that would collapse the header down
    // to just its margins whenever there's no description.
    return hfw >= 0 ? hfw : textPanel_->layout()->sizeHint().height();
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
