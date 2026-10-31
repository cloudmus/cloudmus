#pragma once

#include <QAbstractListModel>

namespace Playback {
class QueuePlayer;
}

namespace ViewModel {

// The tracks of what plays, as rows for a list view.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        DurationMsRole,
        CoverUrlRole,
        IsCurrentRole,
    };

    explicit TrackListModel(Playback::QueuePlayer& player, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = { }) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    Playback::QueuePlayer& player_;
    int current_ = -1;
};

// The playlist being played: its rows and starting a track from it. Named
// after the desktop ViewModel::ActivePlaylist; slots for the same reason as
// NowPlaying's.
class ActivePlaylist : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)

public:
    explicit ActivePlaylist(Playback::QueuePlayer& player, QObject* parent = nullptr);

    TrackListModel& tracks() { return tracks_; }
    int count() const;
    QString title() const;

public slots:
    void play(int row);

private:
    Playback::QueuePlayer& player_;
    TrackListModel tracks_;
};

} // namespace ViewModel
