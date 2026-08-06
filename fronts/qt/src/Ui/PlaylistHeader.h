#pragma once

#include <QPixmap>
#include <QWidget>

#include "Models.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QStackedLayout;
class QResizeEvent;
class QTimer;

namespace Ui {

class CoverArtCache;

// Shown above the track list when a sidebar playlist/liked/radioStation
// item is selected (also embedded as SourcePanel's hero). Two visual modes,
// chosen per setPlaylist() call based on whether the playlist has a real
// cover URL — each mode is a fully independent, fully-built-at-construction
// page (own cover label, own title/description/Play button), switched via
// pageStack_ rather than sharing/reparenting widgets between them: an
// earlier version tried reparenting one shared text panel back and forth
// between the two modes' layouts, which turned out to leave stale
// heightForWidth results behind across the reparent (the text would
// disappear/collapse in one of the two modes) — not worth chasing further
// when two small, fully-independent widget sets sidestep the whole problem.
//  - No real cover (a generated placeholder — see GeneratedCoverArt.h): the
//    classic full-bleed banner — cover stretched to fill the whole widget,
//    dark gradient scrim, forced white text overlaid on top. A generated
//    cover is synthetic, not a real photo, so stretching it to an
//    arbitrary size isn't "distorting" anything the way a real photo
//    would be.
//  - A real cover: a small thumbnail beside the text instead, height-
//    matched to NowPlayingBar's toolbar album art with the image's own
//    aspect ratio preserved (no cropping/stretching), on a plain theme-
//    following card background.
// setPlaylist() keeps both modes' text content in sync regardless of which
// is currently visible (cheap, and avoids a stale-content flash if the
// mode changes again right after); setPlayButtonVisible()/setPlayBusy()
// likewise act on both Play buttons together.
class PlaylistHeader : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistHeader(CoverArtCache* coverCache, QWidget* parent = nullptr);

    void setPlaylist(const Playlist& playlist);
    void setPlayBusy(bool busy);
    // Hidden for History (see MainWindow::showHistory()): there's no single
    // sourceId to hand PlaybackController::loadQueue for a list that spans
    // backends, so "play the whole thing" doesn't apply there.
    void setPlayButtonVisible(bool visible);

    // Height tracks content in both modes — see the .cpp for why
    // QStackedLayout (used both as this widget's own top-level layout, to
    // switch modes, and internally within full-bleed mode to overlay text
    // on the cover) needs these three overridden by hand rather than just
    // working via the ordinary layout machinery.
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;

signals:
    void playClicked();

protected:
    // Full-bleed mode only (see the class doc) — a generated cover is
    // rendered for whatever size() was at setPlaylist()/last regenerate
    // time; a real window resize needs a fresh one at the new size or it's
    // left stretched blurry/banded in the meantime. Debounced
    // (coverRegenerateTimer_) rather than regenerating on every single
    // resizeEvent — a live window/splitter drag fires a lot of these in a
    // row. Thumbnail mode's cover is a fixed-height, aspect-correct-width
    // image that doesn't depend on this widget's own size at all, so it
    // never needs this.
    void resizeEvent(QResizeEvent* event) override;

private:
    // Builds one title/description/Play-button trio (font, word-wrap,
    // playClicked connection — everything identical between the two
    // modes), used once per mode in the constructor.
    void buildTextTrio(QLabel*& title, QLabel*& description, QPushButton*& button);
    void applyFullBleedCover(const QPixmap& pixmap);
    void applyThumbnailCover(const QPixmap& pixmap);
    void regenerateFullBleedCover();

    CoverArtCache* coverCache_;

    // Switches this widget's entire content between fullBleedPage_ and
    // thumbnailPage_ — see setPlaylist().
    QStackedLayout* pageStack_ = nullptr;

    QWidget* fullBleedPage_ = nullptr;
    QLabel* fullBleedCoverLabel_ = nullptr;
    QWidget* fullBleedTextPanel_ = nullptr;
    QVBoxLayout* fullBleedTextLayout_ = nullptr;
    QLabel* fullBleedTitleLabel_ = nullptr;
    QLabel* fullBleedDescriptionLabel_ = nullptr;
    QPushButton* fullBleedPlayButton_ = nullptr;
    // StackAll: fullBleedCoverLabel_ and fullBleedTextPanel_ both occupy
    // the full page rect; fullBleedTextPanel_ is raised above so its
    // (transparent-background) title/description/button paint over the
    // cover.
    QStackedLayout* fullBleedStack_ = nullptr;
    QTimer* coverRegenerateTimer_ = nullptr;

    QWidget* thumbnailPage_ = nullptr;
    QLabel* thumbnailCoverLabel_ = nullptr;
    QVBoxLayout* thumbnailTextLayout_ = nullptr;
    QLabel* thumbnailTitleLabel_ = nullptr;
    QLabel* thumbnailDescriptionLabel_ = nullptr;
    QPushButton* thumbnailPlayButton_ = nullptr;
    QVBoxLayout* thumbnailOuterLayout_ = nullptr;

    QString currentCoverUrl_;
    // Only needed for regenerateFullBleedCover() — fullBleedTitleLabel_'s
    // own text would work too, but this keeps that a display concern
    // instead of overloading it as generation input as well.
    QString currentTitle_;
};

} // namespace Ui
