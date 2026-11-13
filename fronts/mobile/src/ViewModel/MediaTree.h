#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "Debounce.h"

namespace Covers {
class CoverArtCache;
}

namespace Playback {
class PlaybackController;
}

namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class LibraryScreen;
class Sources;

// What the system's media browsers list: the car's Media Center, Android Auto.
// The sources, each a folder of its playlists, favorites first; and what a
// search finds, the queue's tracks among it.
// A playlist is an item to play, not a folder of its tracks. Lives beside the
// core, not over a replica: only the service asks.
class MediaTree : public QObject {
    Q_OBJECT

public:
    struct Item {
        QString id;
        QString title;
        QString subtitle;
        QString coverFile; // a local file, or empty
        bool browsable = false;

        bool operator==(const Item&) const = default;
    };

    static constexpr auto kRootId = "root";
    // "queue/<index>/<source>/<track>": a track of the queue, as search finds
    // it. Keep in step with PlaybackService.QUEUE_ID_PREFIX.
    static constexpr auto kQueuePrefix = "queue/";
    // At most this many of the queue's tracks in what a search finds.
    static constexpr int kMaxQueueFound = 50;

    MediaTree(Sources& sources, Rpc::SourceManager& sourceManager, LibraryScreen& library,
        Covers::CoverArtCache& covers, Playback::PlaybackController& playback, QObject* parent = nullptr);

    // Empty for what isn't a folder here.
    QList<Item> children(const QString& parentId);
    // The folder or playlist itself; an empty id if there is no such.
    Item item(const QString& id);
    // In one list: the queue's tracks whose title and artists have each word
    // of `query` in them, whatever the case (and "ё" as "е"), in the queue's
    // order — Android Auto's search is the same wherever it's opened, the
    // queue too — then the playlists of every source whose title has them,
    // in the order children() lists them. Nothing for a query without words.
    QList<Item> search(const QString& query);
    // Starts a playlist, for an id children() gave. Its source may not be up
    // yet, a saved list being there from the first moment: then once it is.
    void play(const QString& id);

signals:
    // What's listed is different now: a browser should ask again.
    void changed();

private:
    QStringList listedSources() const;
    QList<Item> playlistsOf(const QString& sourceId);
    void compare();

    Sources& sources_;
    Rpc::SourceManager& sourceManager_;
    LibraryScreen& library_;
    Covers::CoverArtCache& covers_;
    Playback::PlaybackController& playback_;
    // Sources report each cover and listing one by one, and a browser asking
    // again over a car's bus isn't free.
    Timing::Debounce compareLater_;
    QList<Item> lastListed_;
    // The last playlist asked for while its source wasn't up.
    QString waitingToPlay_;
};

} // namespace ViewModel
