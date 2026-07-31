#pragma once

#include <QWidget>

#include "Models.h"

class QLabel;
class QPushButton;
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

signals:
    void playPauseClicked();
    void nextClicked();
    void previousClicked();
    void stopClicked();
    void seekRequested(qint64 positionMs);
    void volumeChanged(int volume0To100);

private:
    void updatePlayPauseIcon();

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

    QString currentWebUrl_;
    QString currentCoverUrl_;
    bool playing_ = false;
    bool userIsDraggingSeek_ = false;
    qint64 lastDurationMs_ = 0;
};

} // namespace Ui
