#include "Sources.h"

#include <QJsonObject>
#include <QLoggingCategory>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Messages.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcSources, "cloudmus.viewmodel.sources")
}

Sources::Sources(Rpc::SourceManager& sourceManager, App::SourceSession& sourceSession, Rpc::AuthStates& authStates,
    App::PlaylistEditing& playlistEditing, Covers::CoverArtCache& coverArtCache, Config::Settings& settings,
    Messages& messages, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , authStates_(authStates)
    , playlistEditing_(playlistEditing)
    , coverArtCache_(coverArtCache)
    , settings_(settings)
    , messages_(messages)
{
    model_.ensureHistoryItem();
    const QStringList collapsed = settings_.sidebarCollapsed();
    collapsed_ = QSet<QString>(collapsed.cbegin(), collapsed.cend());

    // Every backend gets its header row as soon as its process is spawned,
    // not once it has listed its playlists — a backend that's slow to
    // start or still signing in must stay visible, shown as loading.
    connect(&sourceManager_, &Rpc::SourceManager::sourceStarting, this, [this](const Rpc::BackendManifest& manifest) {
        model_.setSourceIconPath(manifest.id, manifest.iconPath);
        model_.setFavorites(manifest.id, settings_.favorites(manifest.id));
        model_.setSource(manifest.id, manifest.name, { });
        model_.setSourceLoading(manifest.id, manifest.name, true);
        showAuthState(manifest.id);
        emit sourceChanged(manifest.id);
    });
    connect(&sourceManager_, &Rpc::SourceManager::sourceStopped, this, [this](const QString& sourceId) {
        authStates_.remove(sourceId);
        removeSource(sourceId);
    });
    connect(&sourceManager_, &Rpc::SourceManager::sourceUnavailable, this,
        [this](const QString& manifestId, const QString& name) {
            removeSource(manifestId);
            messages_.error(tr("%1 is unavailable").arg(name));
        });
    // Listed as soon as it's up, again once signed in or reconfigured.
    connect(&sourceSession, &App::SourceSession::sourceReady, this,
        [this](Rpc::RpcClient* client) { load(client).detach(); });
    connect(
        &sourceSession, &App::SourceSession::signedIn, this, [this](Rpc::RpcClient* client) { load(client).detach(); });
    connect(&sourceManager_, &Rpc::SourceManager::settingsChanged, this, &Sources::refresh);
    connect(&authStates_, &Rpc::AuthStates::changed, this, &Sources::showAuthState);
    // auth.logout (from the source's Settings page) leaves the playlists
    // listed until something reloads them; they're no longer the user's to see.
    connect(&authStates_, &Rpc::AuthStates::signedOut, this, [this](const QString& sourceId) {
        if (Rpc::RpcClient* client = sourceManager_.client(sourceId)) {
            model_.setSource(sourceId, client->sourceName(), { });
            playlistEditing_.setPlaylists(sourceId, { });
            showAuthState(sourceId);
            emit sourceChanged(sourceId);
        }
    });
    connect(&playlistEditing_, &App::PlaylistEditing::playlistEdited, this,
        [this](const QString& sourceId, const Track&, const QString& playlistId, bool, int trackCount) {
            model_.setPlaylistTrackCount(sourceId, playlistId, trackCount);
        });
}

void Sources::refresh(const QString& sourceId)
{
    if (Rpc::RpcClient* client = sourceManager_.client(sourceId))
        load(client).detach();
}

Rpc::Task<void> Sources::load(Rpc::RpcClient* client)
{
    const QString sourceId = client->sourceId();
    model_.setSourceLoading(sourceId, client->sourceName(), true);
    emit sourceChanged(sourceId);
    const QJsonObject browse = client->capabilities().value(QStringLiteral("browse")).toObject();
    const bool shouldFetch = browse.value(QStringLiteral("playlists")).toBool()
        || browse.value(QStringLiteral("likedTracks")).toBool() || browse.value(QStringLiteral("radio")).toBool();
    QList<Playlist> playlists;
    // Why the fetch failed, for the user. Left empty for the auth range
    // (1000–1099, docs/protocol.md §9): a not-yet-signed-in source rejects
    // this routinely at startup, and its sign-in prompt already says so.
    QString fetchError;
    if (shouldFetch) {
        try {
            ListPlaylistsResult result = co_await Rpc::catalogListPlaylists(*client);
            playlists = result.playlists;
        } catch (const Rpc::RpcCallException& e) {
            const int code = e.error().code;
            if (code < 1000 || code > 1099)
                fetchError = code == -1 ? tr("timed out loading playlists") : QString::fromUtf8(e.what());
            qCWarning(lcSources) << "catalog.listPlaylists failed for" << sourceId << ":" << e.what();
        } catch (const std::exception& e) {
            // std::exception, not Rpc::RpcCallException — critically also
            // catches Rpc::ProtocolParseError (a well-formed response whose
            // *content* violates the protocol schema, e.g. a field of the
            // wrong JSON type). That distinction is exactly what caused a
            // real bug: a backend returning Playlist.trackCount as a JSON
            // string for some entries threw ProtocolParseError, which this
            // catch didn't match, so the exception propagated straight out
            // of this .detach()'d coroutine (silently discarded — see
            // Coro.h's promise_type::unhandled_exception) and skipped the
            // model_.setSource() call below entirely — the source never
            // appeared as a sidebar row at all, not just missing its
            // playlists.
            //
            // Reported to the user below, like an RpcCallException — a
            // silently empty source looks the same as one with no playlists.
            fetchError = QString::fromUtf8(e.what());
            qCWarning(lcSources) << "catalog.listPlaylists failed for" << sourceId << ":" << e.what();
        }
    }
    for (const Playlist& playlist : playlists)
        coverArtCache_.assignSource(sourceId, playlist.coverUrl.value_or(QString()));
    model_.setSource(sourceId, client->sourceName(), playlists);
    playlistEditing_.setPlaylists(sourceId, playlists);
    // setSource() just recreated this source's header row from scratch,
    // dropping any warning/loading/error icon it had — reapply from the
    // cached state. Needed because this coroutine and the sign-in kicked
    // off alongside it in App::SourceSession race: an auth/prompt can
    // arrive and set the icon before this RPC round-trip finishes, in which
    // case this call would otherwise silently wipe it back off.
    showAuthState(sourceId);
    model_.setSourceLoading(sourceId, client->sourceName(), false);
    model_.setSourceFetchError(sourceId, client->sourceName(), !fetchError.isEmpty());
    if (!fetchError.isEmpty())
        messages_.error(tr("%1: %2").arg(client->sourceName(), fetchError));
    emit sourceChanged(sourceId);
    emit playlistsLoaded(sourceId, playlists);
}

bool Sources::toggleFavorite(const QString& sourceId, const QString& playlistId)
{
    QStringList favorites = model_.favoritesFor(sourceId);
    const bool removed = favorites.removeOne(playlistId);
    if (!removed)
        favorites.append(playlistId);
    settings_.setFavorites(sourceId, favorites);
    model_.setFavorites(sourceId, favorites);
    emit sourceChanged(sourceId);
    if (!removed)
        return false;
    const QList<Playlist> all = playlists(sourceId);
    const auto it = std::find_if(all.cbegin(), all.cend(), [&](const Playlist& p) { return p.id == playlistId; });
    return it != all.cend() && it->kind != QStringLiteral("playlist");
}

void Sources::setCollapsed(const QString& nodeKey, bool collapsed)
{
    if (nodeKey.isEmpty() || collapsed_.contains(nodeKey) == collapsed)
        return;
    if (collapsed)
        collapsed_.insert(nodeKey);
    else
        collapsed_.remove(nodeKey);
    settings_.setSidebarCollapsed(QStringList(collapsed_.cbegin(), collapsed_.cend()));
}

void Sources::removeSource(const QString& sourceId)
{
    model_.removeSource(sourceId);
    playlistEditing_.setPlaylists(sourceId, { });
    emit sourceRemoved(sourceId);
}

void Sources::showAuthState(const QString& sourceId)
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    model_.setSourceAuthProblem(sourceId, sourceName, authStates_.state(sourceId).hasProblem);
}

} // namespace ViewModel
