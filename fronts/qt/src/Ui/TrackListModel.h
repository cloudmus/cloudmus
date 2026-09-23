#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVector>

#include "Models.h"

namespace Library {
class TrackStates;
}

namespace Ui {

// Backs the center track-list view. One instance per MainWindow, repointed
// at a new source/track set whenever the sidebar selection changes.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TrackRole = Qt::UserRole + 1,
        SourceIdRole,
        // Invalid QDateTime (the default) unless set via
        // setMixedSourceTracks() with a valid MixedSourceEntry::playedAt —
        // currently only History populates this. TrackRowDelegate checks
        // QDateTime::isValid() to decide whether to draw it at all.
        PlayedAtRole,
        // From Library::TrackStates (see setTrackStates()), not the Track
        // copy: bool, or invalid while unknown.
        LikedRole,
        DislikedRole,
        // When the track was last played at all (any list), UTC; invalid if never.
        LastPlayedRole,
    };

    // One row for setMixedSourceTracks() — see that method's doc comment
    // for why a mixed-source list needs a per-row sourceId. playedAt is
    // separate from Track itself (a protocol type, not a
    // when-the-user-played-it fact) so History::PlaybackHistory stays the
    // one place that knows about play history at all.
    struct MixedSourceEntry {
        QString sourceId;
        Track track;
        QDateTime playedAt;
    };

    explicit TrackListModel(QObject* parent = nullptr);

    void setTracks(const QString& sourceId, const QList<Track>& tracks);
    // For a list spanning more than one backend (currently only History,
    // see MainWindow::showHistory()) — PlaybackController::loadQueue takes
    // one sourceId for the whole queue, so double-clicking a row here plays
    // just that track instead of queuing the rest of the list (see
    // MainWindow::onTrackDoubleClicked / isMixedSource()).
    void setMixedSourceTracks(const QList<MixedSourceEntry>& entries);
    void appendTracks(const QList<Track>& tracks);
    void clear();
    // Where LikedRole/DislikedRole/LastPlayedRole come from — rows
    // repaint whenever it reports a change. Without one those roles are
    // empty. See Library::TrackStates.
    void setTrackStates(Library::TrackStates* states);

    const QString& sourceId() const { return sourceId_; }
    bool isMixedSource() const { return mixedSource_; }
    QString sourceIdAt(int row) const;
    const Track& trackAt(int row) const { return tracks_[row]; }
    QList<Track> allTracks() const { return QList<Track>(tracks_.begin(), tracks_.end()); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    void onStateChanged(const QString& sourceId, const QString& trackId);

    Library::TrackStates* states_ = nullptr;
    QString sourceId_;
    QVector<Track> tracks_;
    QVector<QString> mixedSourceIds_; // parallel to tracks_, only populated when mixedSource_
    QVector<QDateTime> playedAt_; // parallel to tracks_, only populated when mixedSource_
    bool mixedSource_ = false;
};

} // namespace Ui
