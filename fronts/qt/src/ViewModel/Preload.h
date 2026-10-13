#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVector>

namespace Timing {
class Throttle;
}

namespace Config {
class Settings;
}
namespace Playback {
class PlaybackController;
}

namespace ViewModel {

// Preloading as a view shows it: the tracks being downloaded ahead of the
// playing one and how far each has got, the cache they go to, and the
// settings for both. Everything a view needs is here ready-made — the
// counts, the summary text, how often progress is worth repainting — so a
// view only decides how it looks.
class Preload : public QObject {
    Q_OBJECT
    Q_PROPERTY(int playlistTracks READ playlistTracks WRITE setPlaylistTracks NOTIFY settingsChanged)
    Q_PROPERTY(int radioTracks READ radioTracks WRITE setRadioTracks NOTIFY settingsChanged)
    Q_PROPERTY(int cacheMaxMb READ cacheMaxMb WRITE setCacheMaxMb NOTIFY settingsChanged)
    Q_PROPERTY(qint64 cacheUsedBytes READ cacheUsedBytes NOTIFY cacheUsedChanged)
    Q_PROPERTY(QAbstractItemModel* upcoming READ upcoming CONSTANT)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY progressChanged)
    Q_PROPERTY(double overallFraction READ overallFraction NOTIFY progressChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY progressChanged)
    Q_PROPERTY(ProgressMode progressMode READ progressMode WRITE setProgressMode NOTIFY progressModeChanged)

public:
    enum class State {
        Waiting,
        Downloading,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    // How finely download progress is reported. Smooth: as it goes, a few
    // times a second, for a view to animate between. Stepped: only when it
    // crosses a quarter of a track — a handful of repaints per track, for
    // when repainting costs more than it's worth (on battery, say).
    enum class ProgressMode {
        Smooth,
        Stepped,
    };
    Q_ENUM(ProgressMode)

    // One track downloaded ahead.
    struct Slot {
        QString sourceId;
        QString trackId;
        QString title;
        State state = State::Waiting;
        double fraction = 0; // as reported in the current ProgressMode
    };

    enum Role {
        TitleRole = Qt::UserRole + 1,
        StateRole,
        FractionRole,
        // The slot's tooltip: the title and how far it has got.
        DescriptionRole,
    };

    static constexpr int kMaxTracks = 50;

    Preload(Playback::PlaybackController& playback, Config::Settings& settings, QObject* parent = nullptr);

    // Settings — applied and saved at once.
    int playlistTracks() const;
    void setPlaylistTracks(int tracks);
    int radioTracks() const;
    void setRadioTracks(int tracks);
    int cacheMaxMb() const;
    void setCacheMaxMb(int megabytes);

    qint64 cacheUsedBytes() const;
    // Everything but the playing track and the ones about to play.
    Q_INVOKABLE void clearCache();

    // The tracks after the playing one that are downloaded ahead, in play
    // order: one row each, with the roles above.
    QAbstractItemModel* upcoming() const;
    const QVector<Slot>& tracks() const { return tracks_; }

    // The same as a whole, for when there are too many to show one by one.
    int total() const { return int(tracks_.size()); }
    int readyCount() const;
    double overallFraction() const;
    // "3 of 12 tracks downloaded ahead"; empty when nothing is wanted.
    QString summaryText() const;

    // A track anywhere — in a track list, say: is it downloaded, or how
    // far along.
    State stateFor(const QString& sourceId, const QString& trackId) const;
    double fractionFor(const QString& sourceId, const QString& trackId) const;

    ProgressMode progressMode() const { return progressMode_; }
    void setProgressMode(ProgressMode mode);

signals:
    void settingsChanged();
    void cacheUsedChanged();
    // upcoming() or the totals changed.
    void progressChanged();
    // stateFor()/fractionFor() changed for this track.
    void trackChanged(const QString& sourceId, const QString& trackId);
    void progressModeChanged();

private:
    class UpcomingModel;

    // The preloader's window as this model shows it.
    struct Window {
        Slot playing;
        QVector<Slot> upcoming;
    };
    Window window() const;
    void refresh();
    void apply(const Slot& playing, const QVector<Slot>& fresh);
    double reported(double fraction) const;
    static QString describe(const Slot& slot);

    Playback::PlaybackController& playback_;
    Config::Settings& settings_;
    UpcomingModel* model_ = nullptr;
    QVector<Slot> tracks_;
    // The playing track: not one of upcoming(), but asked about by
    // stateFor()/fractionFor() like any other.
    Slot playing_;
    ProgressMode progressMode_ = ProgressMode::Smooth;
    // Holds back progress-only updates in Smooth mode.
    Timing::Throttle* throttle_ = nullptr;
};

} // namespace ViewModel
