#pragma once

#include <QWidget>

class QLineEdit;
class QListView;
class QProgressBar;
class QPushButton;
class QShortcut;
class QStackedWidget;
class QToolButton;

namespace Ui {

class AnimatedPresenter;
class CoverArtCache;
class SourcePanel;
class TrackListModel;
class TrackRowDelegate;

// Full-cover sheet over the main content area (hero + active playlist)
// for looking at anything that isn't the active playlist: another
// playlist, Liked, History, a radio station, or a backend's page. Slides
// in from the left while fading in, and back out the same way (see
// AnimatedPresenter). Closing it — ×, Esc — reveals the active playlist
// again underneath; MainWindow decides what starting a track here means
// (activating this playlist).
//
// Only presentation lives here: MainWindow fills trackModel(), sets the
// header, and reacts to the signals.
class PlaylistSheet : public QWidget {
    Q_OBJECT

public:
    PlaylistSheet(CoverArtCache* coverCache, QWidget* parent);

    // Track list page (a playlist, Liked, History). `coverUrl` may be
    // empty, in which case a cover is generated from `coverSeed`.
    // `canPlayAll` shows the header's Play button. Clears the filter.
    void showTracks(const QString& title, const QString& subtitle, const QString& coverUrl, const QString& coverSeed,
        bool canPlayAll);
    // A continuous radio station: no list, just the header and Play.
    void showRadio(const QString& title, const QString& description, const QString& coverUrl, const QString& coverSeed);
    // A backend's page — sourcePanel(), filled by MainWindow.
    void showSource();
    void setSubtitle(const QString& subtitle);
    void setBusy(bool busy);
    // Repaints the track rows (e.g. the delegate's now-playing highlight moved).
    void updateRows();

    TrackListModel* trackModel() const { return trackModel_; }
    TrackRowDelegate* trackDelegate() const { return trackDelegate_; }
    SourcePanel* sourcePanel() const { return sourcePanel_; }
    QListView* trackView() const { return trackView_; }

    void present();
    void dismiss();
    bool isPresented() const;

signals:
    // Rows are trackModel() rows, already mapped through the filter.
    void trackActivated(int row);
    void trackContextMenuRequested(int row, const QPoint& globalPos);
    void playAllClicked();
    void closeRequested();
    // The slide-out animation finished (not emitted by a no-op dismiss()).
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setHeader(const QString& title, const QString& subtitle, const QString& coverUrl, const QString& coverSeed);
    int sourceRow(const QModelIndex& viewIndex) const;

    class HeaderInfo;
    class FilterProxy;

    HeaderInfo* headerInfo_ = nullptr;
    QPushButton* playAllButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QListView* trackView_ = nullptr;
    QWidget* radioPage_ = nullptr;
    QProgressBar* busyIndicator_ = nullptr;
    SourcePanel* sourcePanel_ = nullptr;
    TrackListModel* trackModel_ = nullptr;
    FilterProxy* filterProxy_ = nullptr;
    TrackRowDelegate* trackDelegate_ = nullptr;
    AnimatedPresenter* presenter_ = nullptr;
    QShortcut* escapeShortcut_ = nullptr;
};

} // namespace Ui
