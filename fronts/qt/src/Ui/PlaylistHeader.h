#pragma once

#include <QPixmap>
#include <QWidget>

#include "Models.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QResizeEvent;
class QTimer;

namespace Ui {

class CoverArtCache;

// Shown above the track list when a sidebar playlist/liked/radioStation
// item is selected: cover as a full-bleed background banner, title +
// description over it, and a Play button. Selecting a sidebar item only
// updates this (and, for a browsable kind, the track list) — it never
// starts playback by itself. That's what makes a kind: radioStation entry
// (My Wave) not auto-play on click: playback only starts from this Play
// button (or, for a browsable kind, double-clicking a track row) — see
// docs/protocol.md's Playlist.kind note and the plan.
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

    // Height tracks content (title + optional description + Play button)
    // instead of a fixed banner size — see the .cpp for why QStackedLayout
    // (used to overlay text on the cover) needs these three overridden by
    // hand rather than just working via the ordinary layout machinery.
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;

signals:
    void playClicked();

protected:
    // Only matters for a generated cover (currentCoverUrl_ empty) — it was
    // rendered for whatever size() was at setPlaylist() time, and
    // setScaledContents just stretches that pixmap for any size after, which
    // looks fine for a couple pixels of drift but visibly blurry/banded
    // after a real resize (e.g. the window growing/shrinking, or
    // MainWindow::setTrackListVisible() handing this widget a lot more
    // height). Debounced (coverRegenerateTimer_) rather than regenerating on
    // every single resizeEvent — a live window/splitter drag fires a lot of
    // these in a row, and each regenerate does real work (blur, per-pixel
    // noise).
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyCover(const QPixmap& pixmap);
    void regenerateCover();

    CoverArtCache* coverCache_;
    QLabel* coverLabel_ = nullptr;
    QWidget* textPanel_ = nullptr;
    QVBoxLayout* textLayout_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QTimer* coverRegenerateTimer_ = nullptr;

    QString currentCoverUrl_;
    // Only needed for regenerateCover() — titleLabel_->text() would work
    // too, but this keeps that a display concern instead of overloading it
    // as generation input as well.
    QString currentTitle_;
};

} // namespace Ui
