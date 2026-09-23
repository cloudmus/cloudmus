#include "SidebarModel.h"

#include <QVariant>

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
    QStandardItem* root = findOrCreateSourceRoot(sourceId, sourceName);

    // kind: radioStation/liked entries sit directly under the source root
    // (matching where the old hardcoded "My Wave"/"Liked Tracks" items
    // used to go); kind: playlist entries are grouped under a lazily
    // created "Playlists" sub-header, same as before.
    QStandardItem* playlistsHeader = nullptr;
    for (const Playlist& p : playlists) {
        Kind kind = Kind::Playlist;
        if (p.kind == QStringLiteral("radioStation"))
            kind = Kind::Wave;
        else if (p.kind == QStringLiteral("liked"))
            kind = Kind::Liked;

        QStandardItem* parent = root;
        if (kind == Kind::Playlist) {
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
            parent = playlistsHeader;
        }

        auto* item = new QStandardItem(p.title);
        item->setData(static_cast<int>(kind), KindRole);
        item->setData(sourceId, SourceIdRole);
        item->setData(p.id, PlaylistIdRole);
        item->setData(QVariant::fromValue(p), PlaylistDataRole);
        item->setData(isActive(item), IsActiveRole);
        parent->appendRow(item);
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

QList<Playlist> SidebarModel::playlistsFor(const QString& sourceId) const
{
    QList<Playlist> out;
    const std::function<void(const QStandardItem*)> collect = [&](const QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            const QStandardItem* item = parent->child(row);
            const auto kind = static_cast<Kind>(item->data(KindRole).toInt());
            if (kind == Kind::Wave || kind == Kind::Liked || kind == Kind::Playlist)
                out.append(item->data(PlaylistDataRole).value<Playlist>());
            collect(item);
        }
    };
    for (int row = 0; row < invisibleRootItem()->rowCount(); ++row) {
        const QStandardItem* root = invisibleRootItem()->child(row);
        if (root->data(SourceIdRole).toString() == sourceId
            && static_cast<Kind>(root->data(KindRole).toInt()) == Kind::SourceHeader)
            collect(root);
    }
    return out;
}

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
