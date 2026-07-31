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
    // For a list spanning more than one backend (currently only History,
    // see MainWindow::showHistory()) — PlaybackController::loadQueue takes
    // one sourceId for the whole queue, so double-clicking a row here plays
    // just that track instead of queuing the rest of the list (see
    // MainWindow::onTrackDoubleClicked / isMixedSource()).
    void setMixedSourceTracks(const QList<QPair<QString, Track>>& sourceIdAndTrack);
    void appendTracks(const QList<Track>& tracks);
    void clear();

    const QString& sourceId() const { return sourceId_; }
    bool isMixedSource() const { return mixedSource_; }
    QString sourceIdAt(int row) const;
    const Track& trackAt(int row) const { return tracks_[row]; }
    QList<Track> allTracks() const { return QList<Track>(tracks_.begin(), tracks_.end()); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    QString sourceId_;
    QVector<Track> tracks_;
    QVector<QString> mixedSourceIds_; // parallel to tracks_, only populated when mixedSource_
    bool mixedSource_ = false;
};

} // namespace Ui
