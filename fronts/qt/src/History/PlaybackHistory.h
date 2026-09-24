#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include "Models.h"

namespace History {

struct HistoryEntry {
    QString sourceId;
    Track track;
    QDateTime playedAt;
};

// Every track PlaybackController actually starts playing gets recorded here
// (see MainWindow's connection to PlaybackController::trackChanged) and
// persisted to ~/.config/cloudmus/fronts/qt/history.json, most-recent-first,
// capped at kMaxEntries — so the user can scroll back through everything
// they've listened to and replay any of it. Surfaced in the UI as the
// sidebar's "History" entry (MainWindow::showHistory()), a synthetic
// Ui::Playlist backed by this list instead of a real backend playlist.
class PlaybackHistory : public QObject {
    Q_OBJECT

public:
    explicit PlaybackHistory(QObject* parent = nullptr);

    // Most-recent-first.
    void record(const QString& sourceId, const Track& track);
    const QList<HistoryEntry>& entries() const { return entries_; }
    // Patches Track::liked on every entry matching (sourceId, trackId),
    // persists (history.json), and emits changed() if anything matched —
    // so replaying a liked track from History (including after an app
    // restart) shows it as liked instead of whatever it was recorded with.
    void markTrackLiked(const QString& sourceId, const QString& trackId, bool liked);

signals:
    void changed();

private:
    void load();
    void save() const;
    static QString filePath();

    QList<HistoryEntry> entries_;
};

} // namespace History
