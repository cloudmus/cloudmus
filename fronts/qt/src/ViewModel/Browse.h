#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "Models.h"
#include "OpenPlaylist.h"
#include "PlaylistContext.h"

namespace App {
class PlaylistEditing;
}
namespace Config {
class Settings;
}
namespace Covers {
class CoverArtCache;
}
namespace History {
class PlaybackHistory;
}
namespace Library {
class TrackStates;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class ActivePlaylist;
class Messages;
class Sources;

// What's open over the desktop's main area (the sheet): a playlist — one of a
// source's, or History, see ViewModel::OpenPlaylist — a source's page, or
// nothing. Opening and closing, its source going away, and the sidebar's
// selection: remembered across restarts and brought back once its source has
// listed its playlists.
class Browse : public QObject {
    Q_OBJECT

public:
    enum class Page {
        None,
        Playlist, // context() — a source's playlist or History
        Source, // sourceId()
    };

    using Row = OpenPlaylist::Row;

    Browse(Rpc::SourceManager& sourceManager, Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache,
        History::PlaybackHistory& history, Config::Settings& settings, Sources& sources, ActivePlaylist& activePlaylist,
        App::PlaylistEditing& playlistEditing, Messages& messages, QObject* parent = nullptr);

    Page page() const { return page_; }
    const PlaylistContext& context() const { return open_.context(); }
    const QString& sourceId() const { return sourceId_; }
    const QList<Row>& rows() const { return open_.rows(); }
    // The playlist's tracks are being fetched.
    bool isLoading() const { return open_.isLoading(); }

    void openPlaylist(const QString& sourceId, const Playlist& playlist);
    void openHistory();
    void openSource(const QString& sourceId);
    void close();

    // Plays the open playlist from `row` (making it the active one).
    void playRow(int row);
    // Plays all of it — or starts it, for a radio station.
    void playAll();

    // What the sidebar has selected: the open page, else the active
    // playlist — "history", "source:<id>", "playlist:<sourceId>:<id>",
    // or empty.
    QString selectionKey() const;

signals:
    void pageChanged();
    void rowsChanged();
    void loadingChanged(bool loading);

private:
    void setPage(Page page, const QString& sourceId);
    void remember();
    void restoreFrom(const QString& sourceId, const QList<Playlist>& playlists);

    Config::Settings& settings_;
    ActivePlaylist& activePlaylist_;

    OpenPlaylist open_;
    Page page_ = Page::None;
    QString sourceId_;
    // The last run's selection (a selectionKey()), until restored or
    // replaced by something the user opens.
    QString pending_;
};

} // namespace ViewModel
