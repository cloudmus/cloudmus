#pragma once

#include <QStandardItemModel>
#include <QString>

#include "Models.h"

namespace Ui {

// Two-level tree, source -> category, populated per-source from
// catalog.listPlaylists (which includes kind: radioStation/liked entries
// for My Wave/Liked Tracks, not just real playlists — see
// docs/protocol.md §7.1) — same model fronts/tui/cloudmus_tui/app.py's
// sidebar already uses. Built on QStandardItemModel rather than a fully
// custom QAbstractItemModel: the data is small and simple enough that
// hand-rolling one wouldn't earn its keep.
class SidebarModel : public QStandardItemModel {
    Q_OBJECT

public:
    // Scoped enum: an unscoped `Playlist` enumerator here would shadow the
    // generated global `::Playlist` struct used right below in this same
    // class scope.
    enum class Kind {
        SourceHeader,
        Wave,
        Liked,
        PlaylistsHeader,
        Playlist,
    };
    enum Role {
        KindRole = Qt::UserRole + 1,
        SourceIdRole,
        PlaylistIdRole,
        // The full Playlist (title/description/coverUrl/kind/...), so
        // MainWindow can show the playlist header without a second RPC
        // round-trip — see fronts/qt/AGENTS.md/the plan on Playlist.kind.
        PlaylistDataRole,
    };

    explicit SidebarModel(QObject* parent = nullptr);

    // One entry per Ui::Kind, driven entirely by catalog.listPlaylists —
    // a source's "My Wave"/"Liked Tracks" (kind: radioStation/liked) are
    // just playlists with a special kind now, not synthesized from
    // capability flags. See docs/protocol.md §7.1.
    void setSource(const QString& sourceId, const QString& sourceName, const QList<Playlist>& playlists);
    void removeSource(const QString& sourceId);

private:
    QStandardItem* findOrCreateSourceRoot(const QString& sourceId, const QString& sourceName);
};

} // namespace Ui
