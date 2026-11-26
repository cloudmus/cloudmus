#include "MediaTree.h"

#include <algorithm>

#include "CoverArtCache.h"
#include "LibraryScreen.h"
#include "PlaybackController.h"
#include "SourceManager.h"
#include "Sources.h"
#include "TrackListModel.h"

namespace ViewModel {

namespace {

constexpr int kCompareDelayMs = 1000;

const QString kSourcePrefix = QStringLiteral("source/");
const QString kPlaylistPrefix = QStringLiteral("playlist/");

// "playlist/<source>/<playlist>": a source's id has no slash, a playlist's may.
QString playlistMediaId(const QString& sourceId, const QString& playlistId)
{
    return kPlaylistPrefix + sourceId + QLatin1Char('/') + playlistId;
}

struct PlaylistRef {
    QString sourceId;
    QString playlistId;
};

PlaylistRef parsePlaylistId(const QString& id)
{
    if (!id.startsWith(kPlaylistPrefix))
        return { };
    const qsizetype slash = id.indexOf(QLatin1Char('/'), kPlaylistPrefix.size());
    if (slash < 0)
        return { };
    return { id.mid(kPlaylistPrefix.size(), slash - kPlaylistPrefix.size()), id.mid(slash + 1) };
}

// What a query is matched on: one case, and "ё" as "е", which most people
// type for it.
QString folded(const QString& text)
{
    QString result = text.toCaseFolded();
    result.replace(QChar(0x0451), QChar(0x0435));
    return result;
}

// MediaTree's own, translated: the car shows it as is.
QString translatedTrackCount(int count)
{
    if (count <= 0)
        return { };
    return count == 1 ? MediaTree::tr("1 track") : MediaTree::tr("%1 tracks").arg(count);
}

} // namespace

MediaTree::MediaTree(Sources& sources, Rpc::SourceManager& sourceManager, LibraryScreen& library,
    Covers::CoverArtCache& covers, Playback::PlaybackController& playback, QObject* parent)
    : QObject(parent)
    , sources_(sources)
    , sourceManager_(sourceManager)
    , library_(library)
    , covers_(covers)
    , playback_(playback)
    , compareLater_(kCompareDelayMs, [this]() { compare(); })
{
    connect(&sources_, &Sources::sourceChanged, this, [this]() { compareLater_.trigger(); });
    connect(&sources_, &Sources::sourceRemoved, this, [this]() { compareLater_.trigger(); });
    connect(&covers_, &Covers::CoverArtCache::pixmapReady, this, [this]() { compareLater_.trigger(); });
    connect(&sourceManager_, &Rpc::SourceManager::sourceReady, this, [this](Rpc::RpcClient* client) {
        if (parsePlaylistId(waitingToPlay_).sourceId == client->sourceId())
            play(waitingToPlay_);
    });
}

QStringList MediaTree::listedSources() const
{
    // One without playlists has nothing to open: signed out, or not up yet.
    const SidebarModel& model = sources_.model();
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(SidebarModel::KindRole).toInt() != int(SidebarModel::Kind::SourceHeader))
            continue;
        const QString id = index.data(SidebarModel::SourceIdRole).toString();
        if (!sources_.playlists(id).isEmpty())
            ids.append(id);
    }
    return ids;
}

QList<MediaTree::Item> MediaTree::playlistsOf(const QString& sourceId)
{
    const QList<Playlist> playlists = sources_.playlists(sourceId);
    const auto toItem = [&](const Playlist& playlist) {
        const QString count = translatedTrackCount(playlist.trackCount);
        const QString size = ViewModel::playlistSizeText(playlist);
        return Item { playlistMediaId(sourceId, playlist.id), playlist.title,
            count.isEmpty() || size.isEmpty() ? count : count + QStringLiteral(" · ") + size,
            covers_.localFile(playlist.coverUrl.value_or(QString())), false };
    };
    QList<Item> items;
    // In the user's order. Not one that asks first (Playlist.offer): a car's
    // list has no way to ask, and it's answered on the phone.
    for (const QString& id : sources_.model().favoritesFor(sourceId)) {
        const auto it = std::find_if(
            playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == id; });
        if (it != playlists.cend() && !it->offer)
            items.append(toItem(*it));
    }
    for (const Playlist& playlist : playlists) {
        if (playlist.offer)
            continue;
        // The top level only, as the Library: a folder's subfolders would
        // make a car's list endless.
        const bool inside = playlist.parentId
            && std::any_of(playlists.cbegin(), playlists.cend(),
                [&](const Playlist& other) { return other.id == *playlist.parentId; });
        if (!inside && !sources_.isFavorite(sourceId, playlist.id))
            items.append(toItem(playlist));
    }
    return items;
}

QList<MediaTree::Item> MediaTree::children(const QString& parentId)
{
    if (parentId == QLatin1String(kRootId)) {
        // Folders even for one source: Media Center (Android 9's car media)
        // makes tabs of the root's folders and shows nothing else there, so
        // playlists straight in the root left it empty.
        QList<Item> items;
        for (const QString& id : listedSources())
            items.append({ kSourcePrefix + id, sources_.model().sourceName(id), QString(), QString(), true });
        return items;
    }
    if (parentId.startsWith(kSourcePrefix))
        return playlistsOf(parentId.mid(kSourcePrefix.size()));
    return { };
}

MediaTree::Item MediaTree::item(const QString& id)
{
    if (id == QLatin1String(kRootId))
        return { id, QStringLiteral("CloudMus"), QString(), QString(), true };
    if (id.startsWith(kSourcePrefix)) {
        const QString sourceId = id.mid(kSourcePrefix.size());
        if (listedSources().contains(sourceId))
            return { id, sources_.model().sourceName(sourceId), QString(), QString(), true };
        return { };
    }
    const QList<Item> playlists = playlistsOf(parsePlaylistId(id).sourceId);
    const auto it = std::find_if(playlists.cbegin(), playlists.cend(), [&](const Item& item) { return item.id == id; });
    return it != playlists.cend() ? *it : Item { };
}

QList<MediaTree::Item> MediaTree::search(const QString& query)
{
    const QStringList words = folded(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return { };
    const auto matches = [&](const QString& text) {
        const QString haystack = folded(text);
        return std::all_of(
            words.cbegin(), words.cend(), [&](const QString& word) { return haystack.contains(word); });
    };
    QList<Item> found;
    const QList<Playback::QueueEntry>& queue = playback_.queue();
    for (int i = 0; i < queue.size() && found.size() < kMaxQueueFound; ++i) {
        const Track& track = queue.at(i).track;
        QStringList artists;
        for (const Artist& artist : track.artists)
            artists.append(artist.name);
        const QString byline = artists.join(QStringLiteral(", "));
        if (!matches(track.title + QLatin1Char(' ') + byline))
            continue;
        const QString cover
            = track.coverUrl.value_or(track.album ? track.album->coverUrl.value_or(QString()) : QString());
        found.append({ QLatin1String(kQueuePrefix) + QString::number(i) + QLatin1Char('/') + queue.at(i).sourceId
                + QLatin1Char('/') + track.id,
            track.title, byline, covers_.localFile(cover), false });
    }
    for (const QString& sourceId : listedSources()) {
        for (const Item& playlist : playlistsOf(sourceId)) {
            if (matches(playlist.title))
                found.append(playlist);
        }
    }
    return found;
}

void MediaTree::play(const QString& id)
{
    const PlaylistRef ref = parsePlaylistId(id);
    if (ref.sourceId.isEmpty())
        return;
    const Rpc::RpcClient* client = sourceManager_.client(ref.sourceId);
    if (client == nullptr || !client->available()) {
        waitingToPlay_ = id;
        return;
    }
    waitingToPlay_.clear();
    library_.playPlaylistOf(ref.sourceId, ref.playlistId);
}

void MediaTree::compare()
{
    // The root and every source's list, flat: any of them changing has a
    // browser ask again, and only then.
    QList<Item> listed = children(QLatin1String(kRootId));
    for (const QString& id : listedSources())
        listed += playlistsOf(id);
    if (listed == lastListed_)
        return;
    lastListed_ = listed;
    emit changed();
}

} // namespace ViewModel
