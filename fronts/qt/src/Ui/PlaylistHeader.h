#pragma once

#include <QPixmap>
#include <QWidget>

#include "Models.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QStackedLayout;
class QResizeEvent;
class QTimer;

namespace Ui {

class CoverArtCache;

// Shown above the track list when a sidebar playlist/liked/radioStation
// item is selected (also embedded as SourcePanel's hero). Two visual modes,
// chosen per setPlaylist() call based on whether the playlist has a real
// cover URL:
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
// titleLabel_/descriptionLabel_/playButton_ are shared between both modes
// (reparented into whichever mode's container is active, restyled to
// match, in setPlaylist()) rather than duplicated.
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
    void applyFullBleedCover(const QPixmap& pixmap);
    void applyThumbnailCover(const QPixmap& pixmap);
    void regenerateFullBleedCover();

    CoverArtCache* coverCache_;

    // Shared between both modes — see the class doc. textPanel_ is moved
    // between fullBleedStack_ and thumbnailRow_ (and restyled to match) in
    // setPlaylist().
    QWidget* textPanel_ = nullptr;
    QVBoxLayout* textLayout_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;

    // Switches this widget's entire content between fullBleedPage_ and
    // thumbnailPage_ — see setPlaylist().
    QStackedLayout* pageStack_ = nullptr;

    QWidget* fullBleedPage_ = nullptr;
    QLabel* fullBleedCoverLabel_ = nullptr;
    // StackAll: fullBleedCoverLabel_ and textPanel_ (while it's this mode's
    // guest) both occupy the full page rect; textPanel_ is raised above so
    // its (transparent-background) title/description/button paint over
    // the cover.
    QStackedLayout* fullBleedStack_ = nullptr;
    QTimer* coverRegenerateTimer_ = nullptr;

    QWidget* thumbnailPage_ = nullptr;
    QLabel* thumbnailCoverLabel_ = nullptr;
    QHBoxLayout* thumbnailRow_ = nullptr;
    QVBoxLayout* thumbnailOuterLayout_ = nullptr;

    QString currentCoverUrl_;
    // Only needed for regenerateFullBleedCover() — titleLabel_->text()
    // would work too, but this keeps that a display concern instead of
    // overloading it as generation input as well.
    QString currentTitle_;
};

} // namespace Ui
