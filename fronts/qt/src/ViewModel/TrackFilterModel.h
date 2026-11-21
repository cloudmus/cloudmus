#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace ViewModel {

// A TrackListModel's rows that match what was typed: a case-insensitive
// substring of the title, an artist's name or the album. Lives where the rows
// are: over a replica, a filter would pull every row through the socket.
class TrackFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // The row of the source model behind a row here; -1 if there's none.
    int sourceRow(int row) const;

public slots:
    void setNeedle(const QString& needle);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString needle_;
};

} // namespace ViewModel
