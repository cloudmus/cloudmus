#include "TrackFilterModel.h"

#include "Models.h"
#include "TrackListModel.h"

namespace ViewModel {

void TrackFilterModel::setNeedle(const QString& needle)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    needle_ = needle.trimmed();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    needle_ = needle.trimmed();
    invalidateFilter();
#endif
}

int TrackFilterModel::sourceRow(int row) const
{
    const QModelIndex source = mapToSource(index(row, 0));
    return source.isValid() ? source.row() : -1;
}

bool TrackFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (needle_.isEmpty())
        return true;
    const Track track = sourceModel()->index(sourceRow, 0, sourceParent).data(TrackListModel::TrackRole).value<Track>();
    if (track.title.contains(needle_, Qt::CaseInsensitive))
        return true;
    for (const Artist& artist : track.artists) {
        if (artist.name.contains(needle_, Qt::CaseInsensitive))
            return true;
    }
    return track.album.has_value() && track.album->title.contains(needle_, Qt::CaseInsensitive);
}

} // namespace ViewModel
