#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include "Models.h"

namespace Ui {

// Backs the center track-list view. One instance per MainWindow, repointed
// at a new source/track set whenever the sidebar selection changes.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TrackRole = Qt::UserRole + 1,
        SourceIdRole,
    };

    explicit TrackListModel(QObject* parent = nullptr);

    void setTracks(const QString& sourceId, const QList<Track>& tracks);
    void appendTracks(const QList<Track>& tracks);
    void clear();

    const QString& sourceId() const { return sourceId_; }
    const Track& trackAt(int row) const { return tracks_[row]; }
    QList<Track> allTracks() const { return QList<Track>(tracks_.begin(), tracks_.end()); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    QString sourceId_;
    QVector<Track> tracks_;
};

} // namespace Ui
