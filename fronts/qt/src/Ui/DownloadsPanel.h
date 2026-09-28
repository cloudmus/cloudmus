#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QVariantAnimation;

namespace ViewModel {
class Downloads;
class NowPlaying;
} // namespace ViewModel

namespace Ui {

// The downloads panel, popped up from the toolbar's download button while
// downloads are under way (like a browser's): a row per download — its
// title, progress bar, and a cancel button while it's running — a
// playlist's with a second line for the track it's on; above them,
// "Save Playing Track". Closes like a menu (a click outside, Escape) and
// forgets the finished downloads once nothing is left running.
class DownloadsPanel : public QWidget {
    Q_OBJECT

public:
    DownloadsPanel(ViewModel::Downloads& downloads, ViewModel::NowPlaying& nowPlaying, QWidget* parent = nullptr);

    // Opens above `anchor` (the button's top-left, global).
    void popup(const QPoint& anchor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh();
    // Above anchor_, kept on screen — again whenever it grows or shrinks.
    void place();

    ViewModel::Downloads& downloads_;
    ViewModel::NowPlaying& nowPlaying_;
    QPushButton* saveCurrentButton_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* rows_ = nullptr;
    QPoint anchor_;
#ifdef Q_OS_WIN
    // Fades the whole window in and out: Windows doesn't animate a popup
    // the way Linux compositors do, and it would just pop in and vanish.
    QPropertyAnimation* fade_ = nullptr;
    bool fadedOut_ = false;
#endif
};

} // namespace Ui
