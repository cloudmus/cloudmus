#pragma once

#include <QObject>
#include <QString>

class QAction;
class QEvent;
class QMenu;

namespace Covers {
class CoverArtCache;
}
namespace ViewModel {
class NowPlaying;
}

namespace Integration {

// The app's Dock tile (macOS only), following ViewModel::NowPlaying: its
// menu (a right-click on the icon) names the playing track and has
// Play/Pause, Next, Previous, Like and Dislike — macOS adds the window list
// and Quit itself; and, if the user wants it (setShowsCover()), the icon
// becomes the track's cover. The Dock's own tooltip shows the app's name
// only, so this is what macOS has instead of a player's thumbnail.
class DockTile : public QObject {
    Q_OBJECT

public:
    DockTile(ViewModel::NowPlaying& nowPlaying, Covers::CoverArtCache& covers, QObject* parent = nullptr);
    ~DockTile() override;

    QMenu* menu() const { return menu_; }

    // The playing track's cover as the Dock icon (with the logo in its
    // corner) instead of the logo. Takes effect at once on the tile that
    // exists, if any.
    static void setShowsCover(bool value);

protected:
    // The language changed: the menu's labels are read again.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void retranslate();
    void refreshTrack();
    void refreshPlaying();
    void refreshFeedbackActions();
    void refreshCover();
    void onCoverReady(const QString& url);

    ViewModel::NowPlaying& nowPlaying_;
    Covers::CoverArtCache& covers_;
    QMenu* menu_ = nullptr;
    QAction* titleAction_ = nullptr;
    QAction* detailsAction_ = nullptr;
    QAction* trackSeparator_ = nullptr;
    QAction* playPauseAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* feedbackSeparator_ = nullptr;
    QAction* likeAction_ = nullptr;
    QAction* dislikeAction_ = nullptr;
    // The cover being loaded for the icon; empty when none is awaited.
    QString pendingCoverUrl_;
    bool retranslatePending_ = false;
};

} // namespace Integration
