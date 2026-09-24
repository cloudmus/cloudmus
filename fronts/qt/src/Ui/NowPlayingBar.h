#pragma once

#include <QWidget>

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

    // Play/pause, stop, and the seek slider all only make sense with a
    // current track loaded — enables/disables the three together, and
    // when false, resets the seek slider (and its elapsed/duration
    // labels) back to 0 rather than leaving a stale position on screen
    // for a track that no longer exists.
    void setTrackAvailable(bool available);
    // Previous/next only make sense with something loaded to navigate —
    // enables/disables both together.
    void setQueueAvailable(bool available);
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
    // real state" setter — called from MainWindow::trackChanged (seeded
    // from Track::liked; dislike has no persisted protocol field, so it
    // always starts false on a new track) and again once a click's RPC
    // call resolves (success: the new state; failure: rolled back to the
    // old one). Always clears busy.
    //
    // capabilitySupported false or no current track means "don't even
    // offer this" (enabled = false) regardless of liked/disliked.
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

    // Save to Downloads — no toggle/checked state (unlike like/dislike, a
    // repeat download is a perfectly normal thing to ask for again), just
    // enabled iff the source declares the `download` capability and busy
    // while catalog.downloadTrack is in flight (same "refresh" glyph-swap
    // convention as setLikeBusy()/setDislikeBusy()).
    void setDownloadState(bool capabilitySupported);
    // The "playlists" button (add to / remove from the user's playlists):
    // enabled iff the current track's source declares browse.editPlaylists.
    void setPlaylistsState(bool capabilitySupported);
    void setDownloadBusy(bool busy);

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
    void seekRequested(qint64 positionMs);
    void volumeChanged(int volume0To100);
    // Carries the user's requested new state — QPushButton::clicked(bool
    // checked) already reflects it (Qt flips isChecked() before emitting
    // clicked() for a checkable button), so MainWindow doesn't need to
    // separately track "was it liked before."
    void likeClicked(bool liked);
    void dislikeClicked(bool disliked);
    void downloadClicked();
    // `anchor`: global position (the button's bottom-left) to open the
    // playlists menu at.
    void playlistsClicked(QPoint anchor);

private:
    void updatePlayPauseIcon();
    void refreshLikeButton();
    void refreshDislikeButton();
    void refreshDownloadButton();

    QPushButton* previousButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
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
    bool userIsDraggingSeek_ = false;
    qint64 lastDurationMs_ = 0;
    QString currentWebUrl_;

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
    bool downloadBusy_ = false;
};

} // namespace Ui
