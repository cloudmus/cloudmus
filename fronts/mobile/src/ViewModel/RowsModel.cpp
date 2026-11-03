#include "RowsModel.h"

namespace ViewModel {

RowsModel::RowsModel(const QList<QByteArray>& roles, QObject* parent)
    : QAbstractListModel(parent)
{
    int role = Qt::UserRole + 1;
    for (const QByteArray& name : roles)
        roles_.insert(role++, name);
}

void RowsModel::setRows(const QList<QVariantHash>& rows)
{
    if (rows.size() != rows_.size()) {
        beginResetModel();
        rows_ = rows;
        endResetModel();
        return;
    }
    for (int row = 0; row < rows.size(); ++row) {
        if (rows[row] != rows_[row]) {
            rows_[row] = rows[row];
            emit dataChanged(index(row), index(row));
        }
    }
}

int RowsModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(rows_.size()); }

QVariant RowsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size())
        return { };
    return rows_[index.row()].value(QString::fromLatin1(roles_.value(role)));
}

} // namespace ViewModel
