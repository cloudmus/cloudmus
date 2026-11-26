#include "LibraryScreen.h"

#include <QJsonObject>
#include <QUrl>

#include <algorithm>

#include "ActivePlaylist.h"
#include "AuthStates.h"
#include "CoverArtCache.h"
#include "NowPlaying.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "SourcePage.h"
#include "Sources.h"
#include "TrackListModel.h"

namespace ViewModel {

namespace {

QString fileUrl(const QString& path) { return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString(); }

// `path`: the folders it's in (Playlist.parentId), for one shown away from
// them — a favorite. `sizeText`: how much its files take, "" when unknown.
// `offerQuestion`: asked before it's opened (Playlist.offer), "" when nothing
// is; `offerAccept`/`offerDecline` its two answers' labels.
const QList<QByteArray> kPlaylistRoles { "playlistId", "title", "trackCount", "sizeText", "kind", "coverUrl", "coverIcon",
    "isActive", "isStarting", "isFavorite", "path", "offerQuestion", "offerAccept", "offerDecline" };

} // namespace

LibraryScreen::LibraryScreen(Sources& sources, ActivePlaylist& activePlaylist, NowPlaying& nowPlaying, Rpc::AuthStates& authStates,
    SourcePage& sourcePage, Rpc::SourceManager& sourceManager, Covers::CoverArtCache& covers, QObject* parent)
    : QObject(parent)
    , sources_(sources)
    , activePlaylist_(activePlaylist)
    , nowPlaying_(nowPlaying)
    , authStates_(authStates)
    , sourcePage_(sourcePage)
    , sourceManager_(sourceManager)
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

QString LibraryScreen::activeSourceLogo() const
{
    const QString sourceId = activePlaylist_.context().sourceId;
    return fileUrl(sources_.logoPath(sourceId.isEmpty() ? currentSourceId_ : sourceId));
}

QString LibraryScreen::currentSourceIcon() const { return fileUrl(sources_.iconPath(currentSourceId_)); }

QString LibraryScreen::currentSourceLogo() const { return fileUrl(sources_.logoPath(currentSourceId_)); }

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

QStringList LibraryScreen::capabilities() const { return sourcePage_.capabilityIds(currentSourceId_); }

void LibraryScreen::selectSource(const QString& sourceId)
{
    // Picked: its turn to run, if it was left unstarted. It may be the one
    // shown already, picked for having no other to show.
    sourcePage_.wake(sourceId);
    picked_ = true;
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

void LibraryScreen::playPlaylist(const QString& playlistId) { playPlaylistOf(currentSourceId_, playlistId); }

void LibraryScreen::playPlaylistOf(const QString& sourceId, const QString& playlistId)
{
    const QList<Playlist> playlists = sources_.playlists(sourceId);
    const auto it = std::find_if(
        playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == playlistId; });
    if (it == playlists.cend())
        return;
    const PlaylistContext& active = activePlaylist_.context();
    // The one playing goes on — unless it's a radio: that starts anew, for
    // new tracks.
    if (nowPlaying_.hasTrack() && active.sourceId == sourceId && active.playlist.id == playlistId
        && !active.isRadio()) {
        nowPlaying_.play();
        emit activated();
        return;
    }
    start(sourceId, *it).detach();
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

void LibraryScreen::answerOffer(const QString& playlistId, bool accept)
{
    const QList<Playlist> playlists = sources_.playlists(currentSourceId_);
    const auto it = std::find_if(
        playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == playlistId; });
    if (it == playlists.cend() || !it->offer)
        return;
    applyOffer(currentSourceId_, accept ? it->offer->accept.values : it->offer->decline.values).detach();
}

Rpc::Task<void> LibraryScreen::applyOffer(QString sourceId, QMap<QString, QJsonValue> values)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    try {
        co_await Rpc::settingsUpdate(*client, UpdateParams { values });
    } catch (const std::exception& e) {
        qWarning("library: %s didn't take the answer: %s", qPrintable(sourceId), e.what());
        co_return;
    }
    // Lists anew, and asks the platform for what it now wants (media/wanted).
    sourceManager_.notifySettingsChanged(sourceId);
}

bool LibraryScreen::keepsDeviceMusic(const QString& sourceId) const
{
    return sourcePage_.capabilities(sourceId).value(QStringLiteral("hostMedia")).toBool();
}

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
        { QStringLiteral("sizeText"), ViewModel::playlistSizeText(playlist) },
        { QStringLiteral("kind"), playlist.kind },
        { QStringLiteral("coverUrl"), fileUrl(covers_.localFile(playlist.coverUrl.value_or(QString()))) },
        { QStringLiteral("coverIcon"), ViewModel::playlistCoverIcon(playlist) },
        { QStringLiteral("isActive"), active.sourceId == currentSourceId_ && active.playlist.id == playlist.id },
        { QStringLiteral("isStarting"), startingPlaylistId_ == playlist.id },
        { QStringLiteral("isFavorite"), sources_.isFavorite(currentSourceId_, playlist.id) },
        { QStringLiteral("path"),
            sources_.model().parentTitles(currentSourceId_, playlist.id).join(QStringLiteral(" / ")) },
        { QStringLiteral("offerQuestion"), playlist.offer ? playlist.offer->question : QString() },
        { QStringLiteral("offerAccept"), playlist.offer ? playlist.offer->accept.label : QString() },
        { QStringLiteral("offerDecline"), playlist.offer ? playlist.offer->decline.label : QString() },
    };
}

void LibraryScreen::rebuild()
{
    const SidebarModel& model = sources_.model();
    // By name, as the model has them; the device's own music first: it plays
    // with no account.
    QList<QModelIndex> sourceIndexes;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(SidebarModel::KindRole).toInt() == int(SidebarModel::Kind::SourceHeader))
            sourceIndexes.append(index);
    }
    std::stable_partition(sourceIndexes.begin(), sourceIndexes.end(), [this](const QModelIndex& index) {
        return keepsDeviceMusic(index.data(SidebarModel::SourceIdRole).toString());
    });
    QStringList sourceIds;
    for (const QModelIndex& index : std::as_const(sourceIndexes))
        sourceIds.append(index.data(SidebarModel::SourceIdRole).toString());

    // The source picked, else the active playlist's, else the first — which,
    // until one is picked, may change as the sources come up.
    const QString active = activePlaylist_.context().sourceId;
    if (!sourceIds.contains(currentSourceId_) || (!picked_ && active.isEmpty()))
        currentSourceId_ = sourceIds.contains(active) ? active : sourceIds.value(0);

    QList<QVariantHash> sourceRows;
    for (const QModelIndex& index : std::as_const(sourceIndexes)) {
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
    const auto listed = [&playlists](const QString& id) {
        return std::any_of(
            playlists.cbegin(), playlists.cend(), [&id](const Playlist& playlist) { return playlist.id == id; });
    };
    for (const Playlist& playlist : playlists) {
        // One inside another is on its parent's page (PlaylistScreen::folderRows()).
        if (playlist.parentId && listed(*playlist.parentId))
            continue;
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
    emit activeSourceLogoChanged();
    emit currentSourceIconChanged();
    emit currentSourceLogoChanged();
    emit authNeededChanged();
    emit authUrlChanged();
    emit authCodeChanged();
    emit authErrorChanged();
    emit canSignOutChanged();
    emit capabilitiesChanged();
}

} // namespace ViewModel
