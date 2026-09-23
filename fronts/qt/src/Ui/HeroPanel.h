#pragma once

#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QWidget>

#include "Models.h"
#include "Transition.h"

class QPushButton;
class QResizeEvent;
class QPaintEvent;
class QTimer;

namespace Ui {

class CoverArtCache;

// The central "hero" panel: a tall left-hand pane (in MainWindow, beside
// the track list) that also doubles as a short banner (in SourcePanel).
// Renders one of two content variants:
//  - Promo (setPlaylist()): the currently-browsed playlist's cover/title/
//    description, plus a Play button.
//  - Now playing (setNowPlaying()): the globally currently-playing track's
//    cover/artist/title, no Play button. MainWindow shows this instead of
//    the promo content once anything has ever started playing this
//    session, regardless of which playlist is currently browsed — see
//    MainWindow's PlaybackController::hasCurrentTrack() wiring.
//
// Background, vignette, cover image, and title/subtitle are all drawn
// directly in paintEvent() — not child QLabel widgets in a QLayout.
// Two reasons: (1) a layout's children fight the panel being shrunk below
// their own size hints (a real cover pixmap or long title text refusing
// to compress), which a plain paintEvent() has no equivalent of — it just
// redraws whatever fits at whatever size it's given; (2) every one of
// those elements animates between contents (see Ui::LayerTransition) —
// animating paint parameters (opacity/scale) frame to frame is
// straightforward, animating a QLabel's geometry inside a QLayout
// fighting it the whole time is not. playButton_ is the one exception (needs real click/hover
// handling) — it stays a normal QPushButton child, but positioned by hand
// via setGeometry() in relayout(), not managed by any QLayout, so it
// can't itself impose a size constraint on this widget either.
class HeroPanel : public QWidget {
    Q_OBJECT

public:
    explicit HeroPanel(CoverArtCache* coverCache, QWidget* parent = nullptr);

    void setPlaylist(const Playlist& playlist);
    void setNowPlaying(const Track& track);
    // Reverts to the empty state (background + vignette only, no title/
    // cover/button) — the state this widget starts in. No MainWindow call
    // site under the recommended hasCurrentTrack()-based mode switch (see
    // the plan); kept for API symmetry and as the escape hatch a caller
    // driving the switch off isPlaying() instead would need.
    void clearNowPlaying();

    void setPlayBusy(bool busy);
    // Applies only while displaying promo content — setNowPlaying() always
    // force-hides the button regardless of this flag (see applyContent()).
    void setPlayButtonVisible(bool visible);

    // false (default): pack to natural content height, top-aligned — the
    // SourcePanel banner usage. true: vertically center content and claim
    // all available height — MainWindow's tall splitter pane. Replaces the
    // old Playlist.kind=="radioStation" inference; this widget no longer
    // looks at kind at all.
    void setFillMode(bool fill);

    // Hand-implemented against the same measurement relayout() uses —
    // there's no QLayout here to delegate to.
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;

signals:
    void playClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    // The background (always) and the cover overlay (when present) depend
    // on size() — debounced (regenerateTimer_) rather than regenerated on
    // every single resizeEvent, since a live window/splitter drag fires a
    // lot of these in a row. Restarted unconditionally, even with no
    // content set, so the background never goes stale/unscaled — see
    // regenerateSizedLayers(). relayout() itself (position math, cheap)
    // still runs synchronously on every resize.
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Content {
        QString title;
        QString subtitle; // playlist description, or artist names
        QString coverUrl; // empty if none
        QString bgSeed; // playlist title, or track album/track title
        bool isPromo = true;
    };
    struct CoverLayer {
        QString url;
        QPixmap pixmap;
        QRect rect;
    };
    struct TextLayer {
        QString text;
        QRect rect;
        int flags; // QPainter::drawText() alignment/wrap flags
    };
    void applyContent(const Content& content);
    void regenerateSizedLayers();
    void refreshCoverOverlay();
    int overlayTargetSide() const;
    // Recomputes coverRect_/titleRect_/subtitleRect_ and playButton_'s
    // geometry from the current content + this widget's current size.
    // Shared measurement math with heightForWidth() (see the .cpp) so the
    // two can't disagree about how tall the content block is.
    void relayout();
    // Swaps `layer` to `text` with a crossfade — or leaves it alone if it
    // already shows exactly that text. Geometry is filled in by relayout().
    static void setTextLayer(LayerTransition<TextLayer>& layer, const QString& text);

    CoverArtCache* coverCache_;

    QPushButton* playButton_ = nullptr; // the only real child widget — see the class doc

    QTimer* regenerateTimer_ = nullptr;

    // Each keeps drawing a previous value while it animates out, so the
    // old cover/text stays at its old geometry even after relayout()
    // moves things around for the new content — see LayerTransition.
    LayerTransition<QPixmap> backgroundLayer_;
    // Static edge falloff over the background layers (see paintEvent()).
    QPixmap edgeFade_;
    QColor edgeFadeColor_;
    LayerTransition<CoverLayer> coverLayer_;
    LayerTransition<TextLayer> titleLayer_;
    LayerTransition<TextLayer> subtitleLayer_;

    QRect coverRect_;
    QRect titleRect_;
    QRect subtitleRect_;

    QFont titleFont_;
    QFont subtitleFont_;

    QString titleText_;
    QString subtitleText_;
    QString currentCoverUrl_;
    QString currentBgSeed_;
    bool isPromo_ = true;
    // Last value passed to setPlayButtonVisible(); applied only while
    // isPromo_ (see applyContent()/setPlayButtonVisible()).
    bool playButtonVisible_ = true;
    bool fillMode_ = false;
};

} // namespace Ui
