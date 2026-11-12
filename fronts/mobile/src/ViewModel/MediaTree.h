#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "Debounce.h"

namespace Covers {
class CoverArtCache;
}

namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class LibraryScreen;
class Sources;

// What the system's media browsers list: the car's Media Center, Android Auto.
// The sources, each a folder of its playlists, favorites first.
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

    MediaTree(Sources& sources, Rpc::SourceManager& sourceManager, LibraryScreen& library,
        Covers::CoverArtCache& covers, QObject* parent = nullptr);

    // Empty for what isn't a folder here.
    QList<Item> children(const QString& parentId);
    // The folder or playlist itself; an empty id if there is no such.
    Item item(const QString& id);
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
    // Sources report each cover and listing one by one, and a browser asking
    // again over a car's bus isn't free.
    Timing::Debounce compareLater_;
    QList<Item> lastListed_;
    // The last playlist asked for while its source wasn't up.
    QString waitingToPlay_;
};

} // namespace ViewModel
