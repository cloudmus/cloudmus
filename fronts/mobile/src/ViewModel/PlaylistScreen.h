#pragma once

#include <QObject>
#include <QString>

#include "OpenPlaylist.h"
#include "TrackFilterModel.h"
#include "TrackListModel.h"

namespace App {
class Core;
}

namespace ViewModel {

// The Playlist screen: a playlist of the Library's source, or History, opened
// to look at. ViewModel::OpenPlaylist does the work; this gives it the slots,
// plain types and filtered rows that a replica can carry.
class PlaylistScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY coverUrlChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool isRadio READ isRadio NOTIFY isRadioChanged)
    Q_PROPERTY(bool isHistory READ isHistory NOTIFY isHistoryChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite NOTIFY isFavoriteChanged)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY canDownloadChanged)

public:
    explicit PlaylistScreen(App::Core& core, QObject* parent = nullptr);

    OpenPlaylist& open() { return open_; }
    TrackFilterModel& rows() { return filter_; }
    TrackListModel& allRows() { return tracks_; }

    QString title() const;
    int count() const { return int(open_.rows().size()); }
    QString coverUrl() const;
    bool loading() const { return open_.isLoading(); }
    bool isRadio() const { return open_.context().isRadio(); }
    bool isHistory() const { return open_.context().isHistory; }
    bool isFavorite() const;
    bool canDownload() const;

public slots:
    void openPlaylist(const QString& sourceId, const QString& playlistId);
    void openHistory();
    void close();
    // `row`: of the filtered rows.
    void playRow(int row);
    void playAll();
    void setFilter(const QString& text);
    void toggleFavorite();
    void downloadAll();

signals:
    void titleChanged();
    void countChanged();
    void coverUrlChanged();
    void loadingChanged();
    void isRadioChanged();
    void isHistoryChanged();
    void isFavoriteChanged();
    void canDownloadChanged();
    // Its source went away: nothing left to show.
    void closed();

private:
    void refill();
    void markCurrent();
    void announce();

    App::Core& core_;
    OpenPlaylist open_;
    TrackListModel tracks_;
    TrackFilterModel filter_;
};

} // namespace ViewModel
