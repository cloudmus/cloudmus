#include "PlaylistEntries.h"

namespace ViewModel {

PlaylistEntries::PlaylistEntries(
    QAbstractItemModel& folders, QAbstractItemModel& tracks, const QHash<int, QByteArray>& trackRoles, QObject* parent)
    : QConcatenateTablesProxyModel(parent)
    , folders_(folders)
{
    roles_ = trackRoles;
    // Ours past the tracks' own ids, for what only a folder has.
    int next = Qt::UserRole + 1000;
    isFolderRole_ = next++;
    roles_.insert(isFolderRole_, "isFolder");
    const QHash<int, QByteArray> folderRoles = folders.roleNames();
    for (auto it = folderRoles.cbegin(); it != folderRoles.cend(); ++it) {
        const int shared = roles_.key(it.value(), -1);
        if (shared >= 0) {
            folderRole_.insert(shared, it.key());
            continue;
        }
        roles_.insert(next, it.value());
        folderRole_.insert(next++, it.key());
    }
    addSourceModel(&folders);
    addSourceModel(&tracks);
}

QVariant PlaylistEntries::data(const QModelIndex& index, int role) const
{
    const QModelIndex source = mapToSource(index);
    if (!source.isValid())
        return { };
    const bool folder = source.model() == &folders_;
    if (role == isFolderRole_)
        return folder;
    if (folder) {
        const auto own = folderRole_.constFind(role);
        return own == folderRole_.cend() ? QVariant() : folders_.data(source, own.value());
    }
    // A role only a folder has: nothing, rather than another's.
    if (role >= Qt::UserRole + 1000)
        return { };
    return QConcatenateTablesProxyModel::data(index, role);
}

} // namespace ViewModel
