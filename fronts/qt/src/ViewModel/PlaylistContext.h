#pragma once

#include <QString>

#include "Models.h"

namespace ViewModel {

// A playlist as the app shows it: one of a source's, or History. What the
// main area has active (ViewModel::ActivePlaylist) and what the sheet has
// open are both one of these.
struct PlaylistContext {
    QString sourceId; // empty for History
    Playlist playlist;
    bool isHistory = false;
    // False for ad-hoc contexts (a radio started from a track) that can't
    // be restored at startup — they aren't saved to settings.
    bool persistent = true;

    bool isValid() const { return isHistory || !playlist.id.isEmpty(); }
    bool isRadio() const { return playlist.kind == QStringLiteral("radioStation"); }
    bool sameAs(const PlaylistContext& other) const
    {
        return isHistory == other.isHistory && sourceId == other.sourceId && playlist.id == other.playlist.id;
    }
};

} // namespace ViewModel
