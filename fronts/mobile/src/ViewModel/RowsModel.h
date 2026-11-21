#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QVariantHash>

namespace ViewModel {

// Rows of plain values, for lists small enough to rebuild whole: a new list
// of the same length only tells of the rows that differ, so a cover arriving
// doesn't throw a view's scroll position away. Roles are fixed: a replica
// learns them when it connects.
class RowsModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit RowsModel(const QList<QByteArray>& roles, QObject* parent = nullptr);

    void setRows(const QList<QVariantHash>& rows);

    int rowCount(const QModelIndex& parent = { }) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return roles_; }

private:
    QHash<int, QByteArray> roles_;
    QList<QVariantHash> rows_;
};

} // namespace ViewModel
