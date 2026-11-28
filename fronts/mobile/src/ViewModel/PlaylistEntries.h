#pragma once

#include <QConcatenateTablesProxyModel>
#include <QHash>
#include <QByteArray>

namespace ViewModel {

// A playlist's subfolders and then its tracks, as the rows of one list: the
// page builds only the rows in view, however many folders there are (a
// Repeater over them made every one at once). A folder row has `isFolder`,
// `title`, `coverUrl`, `playlistId`, `trackCount` and `sizeText`; a track row
// has the tracks' roles, and `isFolder` false. A role the row's side hasn't
// is empty.
class PlaylistEntries : public QConcatenateTablesProxyModel {
    Q_OBJECT

public:
    // Both must outlive this. `trackRoles`: the tracks' (a filter over a model
    // has none until its source is set, which can come after this).
    PlaylistEntries(QAbstractItemModel& folders, QAbstractItemModel& tracks, const QHash<int, QByteArray>& trackRoles,
        QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override { return roles_; }

private:
    QAbstractItemModel& folders_;
    QHash<int, QByteArray> roles_;
    // Our role id → the folders' own, for the roles that are theirs.
    QHash<int, int> folderRole_;
    int isFolderRole_ = 0;
};

} // namespace ViewModel
