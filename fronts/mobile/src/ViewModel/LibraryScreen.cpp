#include "LibraryScreen.h"

#include <QJsonObject>
#include <QUrl>

#include <algorithm>

#include "ActivePlaylist.h"
#include "AuthStates.h"
#include "CoverArtCache.h"
#include "NowPlaying.h"
#include "SourcePage.h"
#include "Sources.h"

namespace ViewModel {

namespace {

QString fileUrl(const QString& path) { return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString(); }

const QList<QByteArray> kPlaylistRoles { "playlistId", "title", "trackCount", "kind", "coverUrl", "isActive",
    "isStarting", "isFavorite" };

} // namespace

LibraryScreen::LibraryScreen(Sources& sources, ActivePlaylist& activePlaylist, NowPlaying& nowPlaying, Rpc::AuthStates& authStates,
    SourcePage& sourcePage, Covers::CoverArtCache& covers, QObject* parent)
    : QObject(parent)
    , sources_(sources)
    , activePlaylist_(activePlaylist)
    , nowPlaying_(nowPlaying)
    , authStates_(authStates)
    , sourcePage_(sourcePage)
    , covers_(covers)
    , sourceRows_({ "sourceId", "name", "iconUrl", "needsSignIn", "isLoading", "hasError", "isCurrent" })
    , favoriteRows_(kPlaylistRoles)
    , otherRows_(kPlaylistRoles)
{
    connect(&sources_, &Sources::sourceChanged, this, &LibraryScreen::rebuild);
    connect(&sources_, &Sources::sourceRemoved, this, &LibraryScreen::rebuild);
    connect(&activePlaylist_, &ActivePlaylist::contextChanged, this, &LibraryScreen::rebuild);
    connect(&activePlaylist_, &ActivePlaylist::activated, this, &LibraryScreen::activated);
    connect(&authStates_, &Rpc::AuthStates::changed, this, &LibraryScreen::rebuild);
    connect(&covers_, &Covers::CoverArtCache::pixmapReady, this, &LibraryScreen::rebuild);
    rebuild();
}

QString LibraryScreen::currentSourceName() const { return sources_.model().sourceName(currentSourceId_); }

bool LibraryScreen::loading() const { return !currentSourceId_.isEmpty() && sources_.isLoading(currentSourceId_); }

QString LibraryScreen::activeSourceName() const
{
    const QString sourceId = activePlaylist_.context().sourceId;
    return sources_.model().sourceName(sourceId.isEmpty() ? currentSourceId_ : sourceId);
}

QString LibraryScreen::activeSourceIcon() const
{
    const QString sourceId = activePlaylist_.context().sourceId;
    return fileUrl(sources_.iconPath(sourceId.isEmpty() ? currentSourceId_ : sourceId));
}

bool LibraryScreen::authNeeded() const { return authStates_.state(currentSourceId_).hasProblem; }

QString LibraryScreen::authUrl() const
{
    return authStates_.state(currentSourceId_).prompt.value(QStringLiteral("url")).toString();
}

QString LibraryScreen::authCode() const
{
    return authStates_.state(currentSourceId_).prompt.value(QStringLiteral("code")).toString();
}

QString LibraryScreen::authError() const { return authStates_.state(currentSourceId_).errorMessage; }

bool LibraryScreen::canSignOut() const { return sourcePage_.canSignOut(currentSourceId_); }

void LibraryScreen::selectSource(const QString& sourceId)
{
    // Picked: its turn to run, if it was left unstarted. It may be the one
    // shown already, picked for having no other to show.
    sourcePage_.wake(sourceId);
    if (sourceId == currentSourceId_)
        return;
    currentSourceId_ = sourceId;
    rebuild();
}

void LibraryScreen::selectActiveSource()
{
    const QString sourceId = activePlaylist_.context().sourceId;
    if (!sourceId.isEmpty())
        selectSource(sourceId);
}

void LibraryScreen::playPlaylist(const QString& playlistId)
{
    const QList<Playlist> playlists = sources_.playlists(currentSourceId_);
    const auto it = std::find_if(
        playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == playlistId; });
    if (it == playlists.cend())
        return;
    const PlaylistContext& active = activePlaylist_.context();
    if (nowPlaying_.hasTrack() && active.sourceId == currentSourceId_ && active.playlist.id == playlistId) {
        nowPlaying_.play();
        emit activated();
        return;
    }
    start(currentSourceId_, *it).detach();
}

Rpc::Task<void> LibraryScreen::start(QString sourceId, Playlist playlist)
{
    startingPlaylistId_ = playlist.id;
    rebuild();
    // Whatever comes of it: a failure is told through ViewModel::Messages.
    co_await activePlaylist_.activateAndPlay(sourceId, playlist);
    if (startingPlaylistId_ == playlist.id) {
        startingPlaylistId_.clear();
        rebuild();
    }
}

void LibraryScreen::toggleFavorite(const QString& playlistId) { sources_.toggleFavorite(currentSourceId_, playlistId); }

void LibraryScreen::refresh()
{
    if (!currentSourceId_.isEmpty())
        sources_.refresh(currentSourceId_);
}

void LibraryScreen::signOut()
{
    if (!currentSourceId_.isEmpty())
        sourcePage_.signOut(currentSourceId_);
}

void LibraryScreen::signIn()
{
    if (sourcePage_.wake(currentSourceId_))
        return; // signs in as it comes up
    if (!currentSourceId_.isEmpty())
        sourcePage_.signIn(currentSourceId_);
}

QVariantHash LibraryScreen::playlistRow(const Playlist& playlist)
{
    const PlaylistContext& active = activePlaylist_.context();
    return {
        { QStringLiteral("playlistId"), playlist.id },
        { QStringLiteral("title"), playlist.title },
        { QStringLiteral("trackCount"), playlist.trackCount },
        { QStringLiteral("kind"), playlist.kind },
        { QStringLiteral("coverUrl"), fileUrl(covers_.localFile(playlist.coverUrl.value_or(QString()))) },
        { QStringLiteral("isActive"), active.sourceId == currentSourceId_ && active.playlist.id == playlist.id },
        { QStringLiteral("isStarting"), startingPlaylistId_ == playlist.id },
        { QStringLiteral("isFavorite"), sources_.isFavorite(currentSourceId_, playlist.id) },
    };
}

void LibraryScreen::rebuild()
{
    const SidebarModel& model = sources_.model();
    QStringList sourceIds;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(SidebarModel::KindRole).toInt() == int(SidebarModel::Kind::SourceHeader))
            sourceIds.append(index.data(SidebarModel::SourceIdRole).toString());
    }

    // The source picked, else the active playlist's, else the first.
    if (!sourceIds.contains(currentSourceId_)) {
        const QString active = activePlaylist_.context().sourceId;
        currentSourceId_ = sourceIds.contains(active) ? active : sourceIds.value(0);
    }

    QList<QVariantHash> sourceRows;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(SidebarModel::KindRole).toInt() != int(SidebarModel::Kind::SourceHeader))
            continue;
        const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();
        sourceRows.append({
            { QStringLiteral("sourceId"), sourceId },
            { QStringLiteral("name"), model.sourceName(sourceId) },
            { QStringLiteral("iconUrl"), fileUrl(sources_.iconPath(sourceId)) },
            { QStringLiteral("needsSignIn"), index.data(SidebarModel::HasAuthProblemRole).toBool() },
            { QStringLiteral("isLoading"), index.data(SidebarModel::IsLoadingRole).toBool() },
            { QStringLiteral("hasError"), index.data(SidebarModel::HasFetchErrorRole).toBool() },
            { QStringLiteral("isCurrent"), sourceId == currentSourceId_ },
        });
    }
    sourceRows_.setRows(sourceRows);

    const QList<Playlist> playlists = sources_.playlists(currentSourceId_);
    QList<QVariantHash> favorites;
    // In the user's order.
    for (const QString& id : model.favoritesFor(currentSourceId_)) {
        const auto it = std::find_if(
            playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == id; });
        if (it != playlists.cend())
            favorites.append(playlistRow(*it));
    }
    favoriteRows_.setRows(favorites);
    QList<QVariantHash> others;
    for (const Playlist& playlist : playlists) {
        if (!sources_.isFavorite(currentSourceId_, playlist.id))
            others.append(playlistRow(playlist));
    }
    otherRows_.setRows(others);

    // Each property's own signal, for a replica; they're cheap to repeat.
    emit currentSourceIdChanged();
    emit currentSourceNameChanged();
    emit loadingChanged();
    emit activeSourceNameChanged();
    emit activeSourceIconChanged();
    emit authNeededChanged();
    emit authUrlChanged();
    emit authCodeChanged();
    emit authErrorChanged();
    emit canSignOutChanged();
}

} // namespace ViewModel
