#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include "Coro.h"
#include "Models.h"
#include "SidebarModel.h"

namespace App {
class PlaylistEditing;
class SourceSession;
} // namespace App
namespace Config {
class Settings;
}
namespace Covers {
class CoverArtCache;
}
namespace Rpc {
class AuthStates;
class RpcClient;
class SourceManager;
} // namespace Rpc

namespace ViewModel {

class Messages;

// The sources and what each one lists — for the sidebar (model()) and the
// sources' pages. Follows the sources' lifecycle (a header row as soon as
// one starts, gone when it stops), lists its playlists when it's ready or
// signed in (and on refresh()), and keeps the sidebar's own state: the
// user's favorites and collapsed rows, both saved in Config::Settings.
class Sources : public QObject {
    Q_OBJECT

public:
    Sources(Rpc::SourceManager& sourceManager, App::SourceSession& sourceSession, Rpc::AuthStates& authStates,
        App::PlaylistEditing& playlistEditing, Covers::CoverArtCache& coverArtCache, Config::Settings& settings,
        Messages& messages, QObject* parent = nullptr);

    // The sidebar's rows — the view shows this model as is.
    SidebarModel& model() { return model_; }

    // A source's playlists (all of them, the ones the sidebar leaves out
    // included), and whether they're being (re)loaded.
    QList<Playlist> playlists(const QString& sourceId) const { return model_.playlistsFor(sourceId); }
    bool isLoading(const QString& sourceId) const { return model_.isSourceLoading(sourceId); }
    QString iconPath(const QString& sourceId) const { return model_.sourceIconPath(sourceId); }

    // Lists the source's playlists again (catalog.listPlaylists).
    void refresh(const QString& sourceId);

    bool isFavorite(const QString& sourceId, const QString& playlistId) const
    {
        return model_.isFavorite(sourceId, playlistId);
    }
    // Puts a playlist in the source's favorites or takes it out; the first
    // change turns the source's own suggestion into the user's list. True
    // when that took a station (or Liked) off the sidebar altogether —
    // regular playlists stay in their group.
    bool toggleFavorite(const QString& sourceId, const QString& playlistId);

    // Rows the user collapsed, by SidebarModel::nodeKey().
    bool isCollapsed(const QString& nodeKey) const { return collapsed_.contains(nodeKey); }
    void setCollapsed(const QString& nodeKey, bool collapsed);

signals:
    // A source's rows were rebuilt, or its loading state changed — views
    // of it (the sidebar selection, its page) catch up.
    void sourceChanged(const QString& sourceId);
    // A source finished listing its playlists.
    void playlistsLoaded(const QString& sourceId, const QList<Playlist>& playlists);
    // A source is gone from the sidebar (stopped, or failed to start).
    void sourceRemoved(const QString& sourceId);

private:
    Rpc::Task<void> load(Rpc::RpcClient* client);
    void removeSource(const QString& sourceId);
    void showAuthState(const QString& sourceId);

    Rpc::SourceManager& sourceManager_;
    Rpc::AuthStates& authStates_;
    App::PlaylistEditing& playlistEditing_;
    Covers::CoverArtCache& coverArtCache_;
    Config::Settings& settings_;
    Messages& messages_;
    SidebarModel model_;
    QSet<QString> collapsed_;
};

} // namespace ViewModel
