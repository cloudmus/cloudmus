#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

#include "PlaylistScreen.h"

namespace App {
class Core;
}

namespace ViewModel {

// A Playlist screen for each page of the stack it can be on: a folder's
// subfolder opens as a page over its parent's, and the parent's own screen
// keeps its rows (and the page its place in them) for when it's back on top.
// The levels are made up front, so that each can be offered to another
// process under a name of its own; one deeper than that reuses the last.
class PlaylistScreens : public QObject {
    Q_OBJECT

public:
    static constexpr int kLevels = 8;

    explicit PlaylistScreens(App::Core& core, QObject* parent = nullptr);

    // `depth` clamped to the levels there are.
    PlaylistScreen& at(int depth);
    static int levelFor(int depth);

public slots:
    // A folder of the playlist at `fromDepth`, opened at the level over it.
    void openFolder(int fromDepth, const QString& playlistId);
    // The page at `depth` is gone: its screen lets go of its rows.
    void close(int depth);

private:
    std::vector<std::unique_ptr<PlaylistScreen>> levels_;
};

} // namespace ViewModel
