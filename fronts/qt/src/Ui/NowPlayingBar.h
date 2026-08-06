#pragma once

#include <QWidget>

#include "Models.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QSlider;

namespace Ui {

class CoverArtCache;
class ClickableArea;

// Lives in the top toolbar, merged with the hamburger menu button: cover +
// title/artist (clickable -> opens Track.webUrl when present, see the
// plan's UI/UX design), transport buttons, seek bar, volume. Icons come
// from the system theme (QIcon::fromTheme), not bundled art.
class NowPlayingBar : public QWidget {
    Q_OBJECT

public:
    explicit NowPlayingBar(CoverArtCache* coverCache, QWidget* parent = nullptr);

    void setTrack(const Track& track);
    void setPlaying(bool playing);
    void setLoading(bool loading);
    void setPosition(qint64 positionMs, qint64 durationMs);
    void setVolume(int volume0To100);

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

protected:
    // titleLabel_/artistLabel_ are given Qt::ElideRight text — sizePolicy
    // Ignored so their sizeHint doesn't force this bar (and the whole
    // window) to stay at least as wide as the current track's full title,
    // but a QLabel doesn't elide its own text automatically at whatever
    // width the layout actually gives it — needs re-eliding by hand
    // whenever that width changes.
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePlayPauseIcon();
    void updateElidedText();

    CoverArtCache* coverCache_;
    ClickableArea* coverAndTitle_ = nullptr;
    QLabel* coverLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* artistLabel_ = nullptr;
    QPushButton* previousButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QSlider* seekSlider_ = nullptr;
    QLabel* elapsedLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    // Transport buttons' row — setTrailingWidget() appends into this, after
    // the stretch already placed there in the constructor.
    QHBoxLayout* buttonsRow_ = nullptr;

    QString currentWebUrl_;
    QString currentCoverUrl_;
    // Full, unelided text — titleLabel_/artistLabel_ only ever show an
    // elided (and thus lossy) copy of these, so setTrack() and the
    // resize-driven re-elide both need the originals kept somewhere.
    QString currentTrackTitle_;
    QString currentArtistNames_;
    bool playing_ = false;
    bool userIsDraggingSeek_ = false;
    qint64 lastDurationMs_ = 0;
};

} // namespace Ui
