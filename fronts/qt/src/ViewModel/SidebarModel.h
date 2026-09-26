#pragma once

#include <QHash>
#include <QStandardItemModel>
#include <QString>
#include <QStringList>

#include <optional>

#include "Models.h"

namespace ViewModel {

// Two-level tree, source -> category, populated per-source from
// catalog.listPlaylists (which includes kind: radioStation/liked entries
// for My Wave/Liked Tracks/personal mixes, not just real playlists — see
// docs/protocol.md §7.1). Only the source's favorites sit directly under
// its root, plus a "Playlists" group for its other kind: playlist entries;
// other stations stay off the sidebar and are reached from the source's
// page — a source can have a dozen of them. Built on QStandardItemModel rather than a fully
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
        // Unlike everything else here, not tied to any one source's
        // SourceIdRole — playback history spans all of them. See
        // ensureHistoryItem() and MainWindow::showHistory().
        History,
    };
    enum Role {
        KindRole = Qt::UserRole + 1,
        SourceIdRole,
        PlaylistIdRole,
        // The full Playlist (title/description/coverUrl/kind/...), so
        // MainWindow can show the playlist header without a second RPC
        // round-trip — see fronts/qt/AGENTS.md/the plan on Playlist.kind.
        PlaylistDataRole,
        // A source header row's status flags — NavItemDelegate combines
        // them into the one status icon drawn at the row's right edge
        // (auth problem or fetch error: warning; else loading: refresh).
        HasAuthProblemRole,
        IsLoadingRole,
        HasFetchErrorRole,
        // Absolute path to the source's monochrome icon SVG (the backend
        // manifest's "icon"), drawn tinted at paint time — see
        // setSourceIconPath().
        SourceIconPathRole,
        // True on the one row whose playlist is the active (playing) one —
        // NavItemDelegate marks it with a ▶ at the right edge.
        IsActiveRole,
        // A Theme::icon() glyph name, tinted at paint time like the rest —
        // for rows outside this model that reuse NavItemDelegate (the
        // Settings dialog's sidebar).
        ThemeIconRole,
    };

    explicit SidebarModel(QObject* parent = nullptr);

    // One entry per Ui::Kind, driven entirely by catalog.listPlaylists —
    // a source's "My Wave"/"Liked Tracks" (kind: radioStation/liked) are
    // just playlists with a special kind now, not synthesized from
    // capability flags. See docs/protocol.md §7.1.
    void setSource(const QString& sourceId, const QString& sourceName, const QList<Playlist>& playlists);
    void removeSource(const QString& sourceId);

    // The ids the user put at the source's top level (Config::Settings::
    // favorites()), or nullopt for the source's own suggestion — see
    // favoritesFor(). Rebuilds the source's rows if it's listed already.
    void setFavorites(const QString& sourceId, const std::optional<QStringList>& favorites);
    // The favorites in effect, in order: the user's own list if they made
    // one, else every Playlist.featured entry, else — from a source that
    // marks none — every liked/radioStation entry (docs/protocol.md §6).
    QStringList favoritesFor(const QString& sourceId) const;
    bool isFavorite(const QString& sourceId, const QString& playlistId) const
    {
        return favoritesFor(sourceId).contains(playlistId);
    }

    // Toggles the warning icon on a source's header row in place, without
    // touching its playlist children — unlike setSource(), safe to call
    // from an auth notification handler that races with a concurrent
    // setSource() rebuild (see MainWindow::updateSourceAuthIndicator, called
    // again after every setSource() for exactly this reason). Creates the
    // header row if it doesn't exist yet (an auth/prompt can arrive before
    // the first setSource() call finishes). Every source's header row is
    // selectable regardless of hasProblem — see findOrCreateSourceRoot();
    // this only ever toggles the icon.
    void setSourceAuthProblem(const QString& sourceId, const QString& sourceName, bool hasProblem);

    // Toggles a source's header-row loading icon in place, same shape and
    // same setSource()-survival caveat as setSourceAuthProblem() above —
    // MainWindow calls this around every (re)fetch of a source's playlists,
    // including the initial one and a context menu's "Force Refresh".
    void setSourceLoading(const QString& sourceId, const QString& sourceName, bool loading);

    // Toggles a source's header-row error icon in place, same shape as
    // setSourceAuthProblem()/setSourceLoading() above — MainWindow calls
    // this with `true` when a catalog.listPlaylists fetch times out for an
    // already-authenticated source (an unexpected failure, unlike the
    // routine "not yet authenticated" rejection, which stays silent) and
    // with `false` once a subsequent fetch succeeds.
    void setSourceFetchError(const QString& sourceId, const QString& sourceName, bool hasError);

    // Remembered per source id, not just set on the current header row:
    // setSource() rebuilds that row from scratch, and findOrCreateSourceRoot()
    // re-applies the path to every row it creates.
    void setSourceIconPath(const QString& sourceId, const QString& iconPath);

    // All of a source's playlists (Wave/Liked/regular, the order it listed
    // them in — including ones the sidebar doesn't show), its icon path
    // and whether its playlists are being (re)loaded — for its page.
    QList<Playlist> playlistsFor(const QString& sourceId) const;
    // Updates a playlist row's stored trackCount after an add/remove.
    void setPlaylistTrackCount(const QString& sourceId, const QString& playlistId, int trackCount);
    QString sourceIconPath(const QString& sourceId) const { return sourceIconPaths_.value(sourceId); }
    bool isSourceLoading(const QString& sourceId) const;

    // Marks the active playlist's row (clearing the previous one). History
    // is addressed as (empty sourceId, "history"); an empty playlistId
    // clears the mark. Remembered, so rows recreated by setSource() pick
    // it up again.
    void setActivePlaylist(const QString& sourceId, const QString& playlistId);
    // The row for (sourceId, playlistId) — same addressing as above — or
    // an invalid index if it isn't in the sidebar (yet).
    QModelIndex indexForPlaylist(const QString& sourceId, const QString& playlistId) const;
    // A source's header row, or an invalid index.
    QModelIndex indexForSource(const QString& sourceId) const;

    // A stable name for an expandable row — a source's header or its
    // "Playlists" group — that survives setSource() rebuilding the rows
    // and a restart; empty for other rows. For remembering what's collapsed.
    static QString nodeKey(const QModelIndex& index);

    // Inserts the top-level "History" row once, ahead of every source root
    // (idempotent — a no-op if already present).
    void ensureHistoryItem();

private:
    QStandardItem* findOrCreateSourceRoot(const QString& sourceId, const QString& sourceName);
    void populate(QStandardItem* root, const QString& sourceId);

    bool isActive(const QStandardItem* item) const;
    void refreshActiveMarks(QStandardItem* parent);

    QHash<QString, QString> sourceIconPaths_;
    QHash<QString, QList<Playlist>> playlists_;
    QHash<QString, QStringList> favorites_; // only sources with the user's own list
    QString activeSourceId_;
    QString activePlaylistId_;
};

} // namespace ViewModel
