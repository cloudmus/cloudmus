#include "PlaylistScreens.h"

#include <algorithm>

namespace ViewModel {

PlaylistScreens::PlaylistScreens(App::Core& core, QObject* parent)
    : QObject(parent)
{
    levels_.reserve(kLevels);
    for (int i = 0; i < kLevels; ++i)
        levels_.push_back(std::make_unique<PlaylistScreen>(core));
}

int PlaylistScreens::levelFor(int depth) { return std::clamp(depth, 0, kLevels - 1); }

PlaylistScreen& PlaylistScreens::at(int depth) { return *levels_[size_t(levelFor(depth))]; }

void PlaylistScreens::openFolder(int fromDepth, const QString& playlistId)
{
    const PlaylistContext& context = at(fromDepth).open().context();
    if (context.isHistory || context.sourceId.isEmpty())
        return;
    // Copied: the next level may be this one, past the last.
    const QString sourceId = context.sourceId;
    at(fromDepth + 1).openPlaylist(sourceId, playlistId);
}

void PlaylistScreens::close(int depth) { at(depth).close(); }

} // namespace ViewModel
