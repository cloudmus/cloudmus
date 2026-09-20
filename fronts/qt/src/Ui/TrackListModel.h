#pragma once

#include <QAbstractListModel>
#include <QDateTime>
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
        // Invalid QDateTime (the default) unless set via
        // setMixedSourceTracks() with a valid MixedSourceEntry::playedAt —
        // currently only History populates this. TrackRowDelegate checks
        // QDateTime::isValid() to decide whether to draw it at all.
        PlayedAtRole,
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
    // Patches Track::liked on every row matching (sourceId, trackId) — by
    // id, since a row's source is sourceId_ normally or
    // mixedSourceIds_[row] in a mixed-source (History) list, same as
    // sourceIdAt(). No-op if the track isn't currently displayed. Fixes
    // replaying a liked track from an already-loaded list showing it as
    // unliked (the list was never re-fetched, so it still had the Track
    // copy's original `liked` value from whenever it was fetched).
    void markTrackLiked(const QString& sourceId, const QString& trackId, bool liked);

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
    QVector<QDateTime> playedAt_; // parallel to tracks_, only populated when mixedSource_
    bool mixedSource_ = false;
};

} // namespace Ui
