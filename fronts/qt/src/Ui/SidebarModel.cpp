#include "SidebarModel.h"

#include <QFont>

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

void SidebarModel::setSource(const QString& sourceId, const QString& sourceName, const QJsonObject& capabilities,
                             const QList<Playlist>& playlists)
{
    removeSource(sourceId);
    QStandardItem* root = findOrCreateSourceRoot(sourceId, sourceName);

    const QJsonObject browse = capabilities.value(QStringLiteral("browse")).toObject();

    if (browse.value(QStringLiteral("radio")).toBool()) {
        auto* item = new QStandardItem(tr("My Wave"));
        item->setData(static_cast<int>(Kind::Wave), KindRole);
        item->setData(sourceId, SourceIdRole);
        root->appendRow(item);
    }
    if (browse.value(QStringLiteral("likedTracks")).toBool()) {
        auto* item = new QStandardItem(tr("Liked Tracks"));
        item->setData(static_cast<int>(Kind::Liked), KindRole);
        item->setData(sourceId, SourceIdRole);
        root->appendRow(item);
    }
    if (browse.value(QStringLiteral("playlists")).toBool() && !playlists.isEmpty()) {
        auto* header = new QStandardItem(tr("Playlists"));
        header->setData(static_cast<int>(Kind::PlaylistsHeader), KindRole);
        header->setData(sourceId, SourceIdRole);
        header->setSelectable(false);
        root->appendRow(header);
        for (const Playlist& p : playlists) {
            auto* item = new QStandardItem(p.title);
            item->setData(static_cast<int>(Kind::Playlist), KindRole);
            item->setData(sourceId, SourceIdRole);
            item->setData(p.id, PlaylistIdRole);
            header->appendRow(item);
        }
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
