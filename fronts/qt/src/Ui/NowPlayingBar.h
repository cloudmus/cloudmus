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

private:
    void updatePlayPauseIcon();

    QPushButton* previousButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* openTrackPageButton_ = nullptr;
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
};

} // namespace Ui
