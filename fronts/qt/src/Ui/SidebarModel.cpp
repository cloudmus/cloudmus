#include "SidebarModel.h"

#include <QFont>
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
    item->setSelectable(false);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
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
                playlistsHeader = new QStandardItem(tr("Playlists"));
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

void SidebarModel::removeSource(const QString& sourceId)
{
    for (int row = invisibleRootItem()->rowCount() - 1; row >= 0; --row) {
        QStandardItem* item = invisibleRootItem()->child(row);
        if (item->data(SourceIdRole).toString() == sourceId) {
            invisibleRootItem()->removeRow(row);
        }
    }
}

} // namespace Ui
