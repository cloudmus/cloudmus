#pragma once

#include <QPixmap>
#include <QWidget>

#include "Models.h"

class QLabel;
class QPushButton;

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

signals:
    void playClicked();

private:
    void applyCover(const QPixmap& pixmap);

    CoverArtCache* coverCache_;
    QLabel* coverLabel_ = nullptr;
    QWidget* textPanel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;

    QString currentCoverUrl_;
};

} // namespace Ui
