#pragma once

#include <QWidget>

#include "PlayMode.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;

namespace Ui {

// Lives in the bottom toolbar, merged with the hamburger menu button:
// transport buttons, seek bar, volume. No track title/artist/cover here —
// HeroPanel already shows what's playing in the central panel, so it
// isn't duplicated. Icons come from Theme::icon() (bundled Material Icons,
// Rounded/filled style, tinted per design token), not the system icon theme.
//
// Every control here is declarative, not imperatively toggled: its
// enabled state and value are a pure function of what PlaybackController
// reports is currently possible, applied wholesale by setTrackAvailable()/
// setQueueAvailable() rather than each caller remembering to flip the
// right buttons at the right moment. See MainWindow's wiring of
// PlaybackController::currentTrackAvailabilityChanged/queueAvailabilityChanged.
class NowPlayingBar : public QWidget {
    Q_OBJECT

public:
    explicit NowPlayingBar(QWidget* parent = nullptr);

    void setPlaying(bool playing);
    void setLoading(bool loading);
    void setPosition(qint64 positionMs, qint64 durationMs);
    void setVolume(int volume0To100);

    // Stop and seek require a current track. Without one, reset the seek
    // position and labels; Play can still start an available playlist.
    void setTrackAvailable(bool available);
    void setPlaylistAvailable(bool available);
    // Previous/next only make sense with something loaded to navigate —
    // enables/disables both together.
    void setQueueAvailable(bool available);
    // Shuffle and repeat as they're in effect (PlaybackController::
    // shuffleActive()/effectiveRepeatMode()). `radio`: the queue is a
    // radio's — shuffle is then unavailable, and repeat only toggles
    // between off and the current track.
    void setPlayModes(bool shuffle, Playback::RepeatMode repeat, bool radio);
    // Enables the "open track page" button iff url is non-empty (a track
    // without a webUrl, or no current track at all — pass an empty
    // string either way), and is what that button opens on click. Handled
    // entirely inside this class (unlike the transport buttons, which
    // just emit a signal for MainWindow/PlaybackController to act on)
    // since opening a URL needs no playback-state coordination.
    void setTrackWebUrl(const QString& url);

    // Like/dislike are real toggles (feedback.like/.dislike and their
    // .unlike/.undislike counterparts — see protocol/methods.yaml 1.3).
    // setLikeState()/setDislikeState() are the authoritative "here's the
    // real state" setter — called from the shared track state for the
    // current or saved track, and again once a click's RPC
    // call resolves (success: the new state; failure: rolled back to the
    // old one). Always clears busy.
    //
    // capabilitySupported false means the action is unavailable, regardless
    // of the displayed liked/disliked state.
    void setLikeState(bool capabilitySupported, bool liked);
    void setDislikeState(bool capabilitySupported, bool disliked);
    // While a like/dislike RPC call is in flight: disables the button and
    // swaps its glyph to the same "refresh" busy indicator
    // playPauseButton_/HeroPanel's play button already use for loading
    // (see setLoading() below). Leaves the checked state alone — Qt
    // already flipped it to the clicked target before likeClicked/
    // dislikeClicked fired, so the accent tint (IconHoverButton's checked
    // color) already shows which way the pending call is heading.
    void setLikeBusy(bool busy);
    void setDislikeBusy(bool busy);

    // The download button — hidden while downloads are off in Settings.
    // `capabilitySupported`: the playing track can be saved. `active`: some
    // download is under way, drawn as a ring around the button — filled to
    // `progress` (0..1), or spinning while that's unknown (-1); a click
    // then opens the downloads panel instead (see downloadClicked()).
    void setDownloadsVisible(bool visible);
    void setDownloadState(bool capabilitySupported);
    void setDownloadActivity(bool active, double progress);
    // The "playlists" button (add to / remove from the user's playlists):
    // enabled iff the current track's source declares browse.editPlaylists.
    void setPlaylistsState(bool capabilitySupported);

    // Appended to the right end of the transport-button row (top row — see
    // the .cpp), after a stretch that keeps it pinned there. MainWindow
    // hands its hamburger-menu QToolButton in here rather than this class
    // building the menu itself: the menu's actions (Settings/About/Quit)
    // need to reach back into MainWindow anyway (SettingsDialog(settings_,
    // this), quitForReal, ...), so constructing it here wouldn't actually
    // decouple anything, just relocate the coupling.
    void setTrailingWidget(QWidget* widget);

signals:
    void playPauseClicked();
    void nextClicked();
    void previousClicked();
    void stopClicked();
    // The user's requested state — see likeClicked() for why clicked(bool).
    void shuffleClicked(bool on);
    // The next repeat mode in the button's cycle: off → list → track → off,
    // skipping the list for a radio.
    void repeatClicked(Playback::RepeatMode mode);
    void seekRequested(qint64 positionMs);
    void volumeChanged(int volume0To100);
    // Carries the user's requested new state — QPushButton::clicked(bool
    // checked) already reflects it (Qt flips isChecked() before emitting
    // clicked() for a checkable button), so MainWindow doesn't need to
    // separately track "was it liked before."
    void likeClicked(bool liked);
    void dislikeClicked(bool disliked);
    // `anchor`: the button's top-left, global — where the downloads panel
    // opens up from.
    void downloadClicked(QPoint anchor);
    // `anchor`: global position (the button's bottom-left) to open the
    // playlists menu at.
    void playlistsClicked(QPoint anchor);

private:
    void updatePlayAvailability();
    void updatePlayPauseIcon();
    void refreshLikeButton();
    void refreshDislikeButton();
    void refreshDownloadButton();

    QPushButton* previousButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* shuffleButton_ = nullptr;
    QPushButton* repeatButton_ = nullptr;
    QPushButton* openTrackPageButton_ = nullptr;
    QPushButton* likeButton_ = nullptr;
    QPushButton* dislikeButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* playlistsButton_ = nullptr;
    QSlider* seekSlider_ = nullptr;
    QLabel* elapsedLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    // Transport buttons' row — setTrailingWidget() appends into this, after
    // the stretch already placed there in the constructor.
    QHBoxLayout* buttonsRow_ = nullptr;

    bool playing_ = false;
    bool trackAvailable_ = false;
    bool playlistAvailable_ = false;
    bool loading_ = false;
    bool userIsDraggingSeek_ = false;
    qint64 lastDurationMs_ = 0;
    QString currentWebUrl_;
    Playback::RepeatMode repeat_ = Playback::RepeatMode::Off;
    bool radio_ = false;

    // Last known non-busy state, restored by setLikeBusy(false)/
    // setDislikeBusy(false) rather than recomputed — a busy transition
    // doesn't get a fresh capability/liked value handed to it.
    bool likeSupported_ = false;
    bool liked_ = false;
    bool likeBusy_ = false;
    bool dislikeSupported_ = false;
    bool disliked_ = false;
    bool dislikeBusy_ = false;
    bool downloadSupported_ = false;
    bool downloadsActive_ = false;
};

} // namespace Ui
