#include "SidebarModel.h"

#include <QVariant>

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
    invisibleRootItem()->appendRow(item);
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

} // namespace Ui
