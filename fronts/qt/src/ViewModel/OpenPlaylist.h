#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include "Coro.h"
#include "Models.h"
#include "PlaylistContext.h"

namespace App {
class PlaylistEditing;
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

// A playlist opened to look at, apart from the active one: one of a source's,
// or History, with its tracks. Fetching them, playing from it (which makes it
// the active one), and keeping it in step with edits to it and with History
// growing. For a view to show as it likes: the desktop's sheet (ViewModel::
// Browse), a page on mobile.
class OpenPlaylist : public QObject {
    Q_OBJECT

public:
    struct Row {
        QString sourceId;
        Track track;
        QDateTime playedAt; // History's rows only
    };

    OpenPlaylist(Rpc::SourceManager& sourceManager, Library::TrackStates& trackStates,
        Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history, ActivePlaylist& activePlaylist,
        App::PlaylistEditing& playlistEditing, Messages& messages, QObject* parent = nullptr);

    const PlaylistContext& context() const { return context_; }
    const QList<Row>& rows() const { return rows_; }
    // The tracks are being fetched.
    bool isLoading() const { return loading_; }

    void open(const QString& sourceId, const Playlist& playlist);
    void openHistory();
    void close();

    // Plays it from `row` (making it the active one).
    void playRow(int row);
    // Plays all of it — or starts it, for a radio station.
    void playAll();

    // open(), openHistory() and close() in the steps of someone with state of
    // their own to change in between, for it to be in place when rowsChanged
    // comes: setContext() says nothing, then one of the others tells.
    void setContext(const PlaylistContext& context);
    void load();
    void fillHistory();
    void announceRows() { emit rowsChanged(); }

signals:
    void rowsChanged();
    void loadingChanged(bool loading);

private:
    Rpc::Task<void> fetch(PlaylistContext context);
    void applyEdit(const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount);

    Rpc::SourceManager& sourceManager_;
    Library::TrackStates& trackStates_;
    Covers::CoverArtCache& coverArtCache_;
    History::PlaybackHistory& history_;
    ActivePlaylist& activePlaylist_;
    Messages& messages_;

    PlaylistContext context_;
    QList<Row> rows_;
    bool loading_ = false;
};

} // namespace ViewModel
