#include "SidebarModel.h"

#include <QVariant>

#include <algorithm>
#include <functional>

namespace Ui {

SidebarModel::SidebarModel(QObject* parent)
    : QStandardItemModel(parent)
{
}

QStandardItem* SidebarModel::findOrCreateSourceRoot(const QString& sourceId, const QString& sourceName)
{
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        QStandardItem* item = invisibleRootItem()->child(row);
        if (item->data(SourceIdRole).toString() == sourceId)
            return item;
    }
    auto* item = new QStandardItem(sourceName.toUpper());
    item->setData(static_cast<int>(Kind::SourceHeader), KindRole);
    item->setData(sourceId, SourceIdRole);
    item->setData(sourceIconPaths_.value(sourceId), SourceIconPathRole);
    // Selectable by default (unlike PlaylistsHeader below) — every source
    // opens a SourcePanel when clicked, not just ones with an auth problem.
    // Text weight/color is NavItemDelegate's job now (see MainWindow), not
    // a font baked into the item.
    //
    // Sources are kept sorted by name (History stays first): setSource()
    // rebuilds a source's row on every (re)load, and appending it would
    // shuffle the sidebar order each time a source refreshes.
    int insertRow = invisibleRootItem()->rowCount();
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        const QStandardItem* other = invisibleRootItem()->child(row);
        if (static_cast<Kind>(other->data(KindRole).toInt()) != Kind::SourceHeader)
            continue;
        if (QString::localeAwareCompare(item->text(), other->text()) < 0) {
            insertRow = row;
            break;
        }
    }
    invisibleRootItem()->insertRow(insertRow, item);
    return item;
}

void SidebarModel::setSource(const QString& sourceId, const QString& sourceName, const QList<Playlist>& playlists)
{
    removeSource(sourceId);
    playlists_.insert(sourceId, playlists);
    populate(findOrCreateSourceRoot(sourceId, sourceName), sourceId);
}

void SidebarModel::setFavorites(const QString& sourceId, const std::optional<QStringList>& favorites)
{
    if (favorites.has_value())
        favorites_.insert(sourceId, *favorites);
    else
        favorites_.remove(sourceId);
    QStandardItem* root = itemFromIndex(indexForSource(sourceId));
    if (root == nullptr)
        return;
    root->removeRows(0, root->rowCount());
    populate(root, sourceId);
}

QStringList SidebarModel::favoritesFor(const QString& sourceId) const
{
    const QList<Playlist> playlists = playlists_.value(sourceId);
    const auto userList = favorites_.constFind(sourceId);
    if (userList != favorites_.cend())
        return *userList;
    const bool anyFeatured = std::any_of(
        playlists.cbegin(), playlists.cend(), [](const Playlist& p) { return p.featured.value_or(false); });
    QStringList out;
    for (const Playlist& p : playlists) {
        const bool featured = anyFeatured
            ? p.featured.value_or(false)
            : p.kind == QStringLiteral("liked") || p.kind == QStringLiteral("radioStation");
        if (featured)
            out.append(p.id);
    }
    return out;
}

void SidebarModel::populate(QStandardItem* root, const QString& sourceId)
{
    const QList<Playlist> playlists = playlists_.value(sourceId);
    const QStringList favorites = favoritesFor(sourceId);

    const auto appendItem = [&](QStandardItem* parent, const Playlist& p) {
        Kind kind = Kind::Playlist;
        if (p.kind == QStringLiteral("radioStation"))
            kind = Kind::Wave;
        else if (p.kind == QStringLiteral("liked"))
            kind = Kind::Liked;
        auto* item = new QStandardItem(p.title);
        item->setData(static_cast<int>(kind), KindRole);
        item->setData(sourceId, SourceIdRole);
        item->setData(p.id, PlaylistIdRole);
        item->setData(QVariant::fromValue(p), PlaylistDataRole);
        item->setData(isActive(item), IsActiveRole);
        parent->appendRow(item);
    };

    // Favorites sit directly under the source root, in the user's order;
    // ids the source no longer lists are just skipped, not forgotten — a
    // mix can drop off YouTube's home feed for a day.
    for (const QString& id : favorites) {
        const auto it
            = std::find_if(playlists.cbegin(), playlists.cend(), [&](const Playlist& p) { return p.id == id; });
        if (it != playlists.cend())
            appendItem(root, *it);
    }

    // The rest of the kind: playlist entries are grouped under a lazily
    // created "Playlists" sub-header. Other stations (and Liked, if it's
    // not a favorite) stay off the sidebar — they're on the source's page.
    QStandardItem* playlistsHeader = nullptr;
    for (const Playlist& p : playlists) {
        if (p.kind != QStringLiteral("playlist") || favorites.contains(p.id))
            continue;
        if (playlistsHeader == nullptr) {
            // .toUpper() here, not a paint-time transform — see the
            // design system's label-upper convention (SourcePanel/
            // findOrCreateSourceRoot's sourceName.toUpper() above does
            // the same).
            playlistsHeader = new QStandardItem(tr("Playlists").toUpper());
            playlistsHeader->setData(static_cast<int>(Kind::PlaylistsHeader), KindRole);
            playlistsHeader->setData(sourceId, SourceIdRole);
            playlistsHeader->setSelectable(false);
            root->appendRow(playlistsHeader);
        }
        appendItem(playlistsHeader, p);
    }
}

void SidebarModel::ensureHistoryItem()
{
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        if (static_cast<Kind>(invisibleRootItem()->child(row)->data(KindRole).toInt()) == Kind::History)
            return;
    }
    auto* item = new QStandardItem(tr("History"));
    item->setData(static_cast<int>(Kind::History), KindRole);
    item->setData(isActive(item), IsActiveRole);
    invisibleRootItem()->insertRow(0, item);
}

void SidebarModel::removeSource(const QString& sourceId)
{
    playlists_.remove(sourceId);
    for (int row = invisibleRootItem()->rowCount() - 1; row >= 0; --row) {
        QStandardItem* item = invisibleRootItem()->child(row);
        if (item->data(SourceIdRole).toString() == sourceId) {
            invisibleRootItem()->removeRow(row);
        }
    }
}

void SidebarModel::setSourceAuthProblem(const QString& sourceId, const QString& sourceName, bool hasProblem)
{
    QStandardItem* root = findOrCreateSourceRoot(sourceId, sourceName);
    root->setData(hasProblem, HasAuthProblemRole);
}

void SidebarModel::setSourceLoading(const QString& sourceId, const QString& sourceName, bool loading)
{
    QStandardItem* root = findOrCreateSourceRoot(sourceId, sourceName);
    root->setData(loading, IsLoadingRole);
}

void SidebarModel::setSourceFetchError(const QString& sourceId, const QString& sourceName, bool hasError)
{
    QStandardItem* root = findOrCreateSourceRoot(sourceId, sourceName);
    root->setData(hasError, HasFetchErrorRole);
}

void SidebarModel::setSourceIconPath(const QString& sourceId, const QString& iconPath)
{
    sourceIconPaths_.insert(sourceId, iconPath);
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        QStandardItem* item = invisibleRootItem()->child(row);
        if (item->data(SourceIdRole).toString() == sourceId)
            item->setData(iconPath, SourceIconPathRole);
    }
}

QList<Playlist> SidebarModel::playlistsFor(const QString& sourceId) const { return playlists_.value(sourceId); }

QModelIndex SidebarModel::indexForSource(const QString& sourceId) const
{
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        const QStandardItem* root = invisibleRootItem()->child(row);
        if (static_cast<Kind>(root->data(KindRole).toInt()) == Kind::SourceHeader
            && root->data(SourceIdRole).toString() == sourceId)
            return root->index();
    }
    return QModelIndex();
}

QList<Playlist> SidebarModel::editablePlaylistsFor(const QString& sourceId) const
{
    QList<Playlist> out;
    for (const Playlist& playlist : playlistsFor(sourceId)) {
        if (playlist.editable.value_or(false))
            out.append(playlist);
    }
    return out;
}

void SidebarModel::setPlaylistTrackCount(const QString& sourceId, const QString& playlistId, int trackCount)
{
    const auto cached = playlists_.find(sourceId);
    if (cached != playlists_.end()) {
        for (Playlist& playlist : *cached) {
            if (playlist.id == playlistId)
                playlist.trackCount = trackCount;
        }
    }
    QStandardItem* item = itemFromIndex(indexForPlaylist(sourceId, playlistId));
    if (item == nullptr)
        return;
    Playlist playlist = item->data(PlaylistDataRole).value<Playlist>();
    playlist.trackCount = trackCount;
    item->setData(QVariant::fromValue(playlist), PlaylistDataRole);
}

bool SidebarModel::isSourceLoading(const QString& sourceId) const
{
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        const QStandardItem* root = invisibleRootItem()->child(row);
        if (root->data(SourceIdRole).toString() == sourceId)
            return root->data(IsLoadingRole).toBool();
    }
    return false;
}

bool SidebarModel::isActive(const QStandardItem* item) const
{
    if (activePlaylistId_.isEmpty())
        return false;
    const auto kind = static_cast<Kind>(item->data(KindRole).toInt());
    if (kind == Kind::History)
        return activeSourceId_.isEmpty() && activePlaylistId_ == QStringLiteral("history");
    if (kind != Kind::Wave && kind != Kind::Liked && kind != Kind::Playlist)
        return false;
    return item->data(SourceIdRole).toString() == activeSourceId_
        && item->data(PlaylistIdRole).toString() == activePlaylistId_;
}

void SidebarModel::refreshActiveMarks(QStandardItem* parent)
{
    for (int row = 0; row < parent->rowCount(); ++row) {
        QStandardItem* item = parent->child(row);
        const bool active = isActive(item);
        if (item->data(IsActiveRole).toBool() != active)
            item->setData(active, IsActiveRole);
        refreshActiveMarks(item);
    }
}

void SidebarModel::setActivePlaylist(const QString& sourceId, const QString& playlistId)
{
    activeSourceId_ = sourceId;
    activePlaylistId_ = playlistId;
    refreshActiveMarks(invisibleRootItem());
}

QModelIndex SidebarModel::indexForPlaylist(const QString& sourceId, const QString& playlistId) const
{
    const std::function<QModelIndex(const QStandardItem*)> find = [&](const QStandardItem* parent) -> QModelIndex {
        for (int row = 0; row < parent->rowCount(); ++row) {
            const QStandardItem* item = parent->child(row);
            const auto kind = static_cast<Kind>(item->data(KindRole).toInt());
            if (kind == Kind::History && sourceId.isEmpty() && playlistId == QStringLiteral("history"))
                return item->index();
            if ((kind == Kind::Wave || kind == Kind::Liked || kind == Kind::Playlist)
                && item->data(SourceIdRole).toString() == sourceId
                && item->data(PlaylistIdRole).toString() == playlistId)
                return item->index();
            const QModelIndex nested = find(item);
            if (nested.isValid())
                return nested;
        }
        return QModelIndex();
    };
    return find(invisibleRootItem());
}

} // namespace Ui
