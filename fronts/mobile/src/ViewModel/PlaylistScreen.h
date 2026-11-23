#pragma once

#include <QObject>
#include <QString>

#include "OpenPlaylist.h"
#include "PlaylistEntries.h"
#include "RowsModel.h"
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
    // How much the playlist's files take, "" when the source doesn't say.
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY sizeTextChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY coverUrlChanged)
    Q_PROPERTY(QString coverIcon READ coverIcon NOTIFY coverIconChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool isRadio READ isRadio NOTIFY isRadioChanged)
    Q_PROPERTY(bool isHistory READ isHistory NOTIFY isHistoryChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite NOTIFY isFavoriteChanged)
    // How many of entries()' first rows are subfolders; the tracks follow.
    Q_PROPERTY(int folderCount READ folderCount NOTIFY folderCountChanged)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY canDownloadChanged)
    // The playlist is queued or being saved (downloadAll()), and how far it
    // has got: 0..1, or -1 while there's nothing to measure it by.
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    // The playlist, a folder, can be deleted with what's in it; and so can
    // tracks of its (docs/protocol.md §7.11).
    Q_PROPERTY(bool canDeletePlaylist READ canDeletePlaylist NOTIFY canDeletePlaylistChanged)
    Q_PROPERTY(bool canDeleteTracks READ canDeleteTracks NOTIFY canDeleteTracksChanged)

public:
    explicit PlaylistScreen(App::Core& core, QObject* parent = nullptr);

    OpenPlaylist& open() { return open_; }
    TrackFilterModel& rows() { return filter_; }
    TrackListModel& allRows() { return tracks_; }
    // The playlists inside this one (Playlist.parentId): a folder's
    // subfolders.
    RowsModel& folderRows() { return folders_; }
    // The subfolders, then the (filtered) tracks, as one list.
    PlaylistEntries& entries() { return entries_; }
    int folderCount() const { return folders_.rowCount(); }

    QString title() const;
    int count() const { return int(open_.rows().size()); }
    QString sizeText() const;
    QString coverUrl() const;
    QString coverIcon() const;
    bool loading() const { return open_.isLoading(); }
    bool isRadio() const { return open_.context().isRadio(); }
    bool isHistory() const { return open_.context().isHistory; }
    bool isFavorite() const;
    bool canDownload() const;
    bool downloading() const { return downloading_; }
    double downloadProgress() const { return downloadProgress_; }
    bool canDeletePlaylist() const;
    bool canDeleteTracks() const;

public slots:
    void openPlaylist(const QString& sourceId, const QString& playlistId);
    void openHistory();
    // Asked first, by the page. `rows`: of the filtered rows; the ones that
    // can't be deleted are left.
    void deleteRows(const QVariantList& rows);
    void deletePlaylist();
    void close();
    // `row`: of the filtered rows.
    void playRow(int row);
    void playAll();
    void setFilter(const QString& text);
    void toggleFavorite();
    // Does nothing while it's being saved already.
    void downloadAll();

signals:
    void titleChanged();
    void countChanged();
    void sizeTextChanged();
    void coverUrlChanged();
    void coverIconChanged();
    void loadingChanged();
    void isRadioChanged();
    void isHistoryChanged();
    void isFavoriteChanged();
    void canDownloadChanged();
    void folderCountChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void canDeletePlaylistChanged();
    void canDeleteTracksChanged();
    // Its source went away, or it was deleted: nothing left to show.
    void closed();

private:
    void refill();
    void markCurrent();
    void announce();
    void rebuildFolders();
    void readDownload();
    void show(const QString& sourceId, const QString& playlistId);

    App::Core& core_;
    OpenPlaylist open_;
    TrackListModel tracks_;
    TrackFilterModel filter_;
    RowsModel folders_;
    PlaylistEntries entries_;
    QString folderNeedle_;
    int shownFolderCount_ = 0;
    bool canDownload_ = false;
    bool downloading_ = false;
    double downloadProgress_ = -1;
};

} // namespace ViewModel
