#include "Preload.h"

#include <cmath>

#include "PlaybackController.h"
#include "Preloader.h"
#include "Settings.h"
#include "Throttle.h"
#include "TrackCache.h"

namespace ViewModel {

namespace {

// In Smooth mode: enough for a view to animate between, and still only a
// few repaints a second however fast the bytes come.
constexpr int kSmoothIntervalMs = 250;
// In Stepped mode a track's progress moves in quarters.
constexpr int kSteps = 4;
constexpr qint64 kMegabyte = 1024 * 1024;

Preload::State stateOf(Playback::Preloader::State state)
{
    switch (state) {
        case Playback::Preloader::State::Downloading:
            return Preload::State::Downloading;
        case Playback::Preloader::State::Ready:
            return Preload::State::Ready;
        case Playback::Preloader::State::Failed:
            return Preload::State::Failed;
        case Playback::Preloader::State::Waiting:
            break;
    }
    return Preload::State::Waiting;
}

bool sameTrack(const Preload::Slot& a, const Preload::Slot& b)
{
    return a.sourceId == b.sourceId && a.trackId == b.trackId;
}

bool sameLook(const Preload::Slot& a, const Preload::Slot& b)
{
    return a.state == b.state && a.fraction == b.fraction && a.title == b.title;
}

} // namespace

class Preload::UpcomingModel : public QAbstractListModel {
public:
    explicit UpcomingModel(Preload& owner)
        : QAbstractListModel(&owner)
        , owner_(owner)
    {
    }

    int rowCount(const QModelIndex& parent = { }) const override
    {
        return parent.isValid() ? 0 : int(owner_.tracks_.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= owner_.tracks_.size())
            return { };
        const Slot& slot = owner_.tracks_.at(index.row());
        switch (role) {
            case Qt::DisplayRole:
            case TitleRole:
                return slot.title;
            case StateRole:
                return QVariant::fromValue(slot.state);
            case FractionRole:
                return slot.fraction;
            case Qt::ToolTipRole:
            case DescriptionRole:
                return describe(slot);
            default:
                return { };
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return { { TitleRole, "title" }, { StateRole, "state" }, { FractionRole, "fraction" },
            { DescriptionRole, "description" } };
    }

    void replace(const QVector<Slot>& tracks)
    {
        beginResetModel();
        owner_.tracks_ = tracks;
        endResetModel();
    }

    void update(int row, const Slot& slot)
    {
        owner_.tracks_[row] = slot;
        emit dataChanged(index(row), index(row));
    }

private:
    Preload& owner_;
};

Preload::Preload(Playback::PlaybackController& playback, Config::Settings& settings, QObject* parent)
    : QObject(parent)
    , playback_(playback)
    , settings_(settings)
    , model_(new UpcomingModel(*this))
    , throttle_(new Timing::Throttle(
          kSmoothIntervalMs,
          [this]() {
              const Window now = window();
              apply(now.playing, now.upcoming);
          },
          this))
{
    playback_.setPreloadDepth(settings_.preloadPlaylistTracks(), settings_.preloadRadioTracks());
    playback_.trackCache().setMaxBytes(settings_.preloadCacheMaxMb() * kMegabyte);

    connect(&playback_.preloader(), &Playback::Preloader::progressChanged, this, &Preload::refresh);
    connect(&playback_.preloader(), &Playback::Preloader::trackStateChanged, this, &Preload::trackChanged);
    connect(&playback_.trackCache(), &Playback::TrackCache::changed, this, &Preload::cacheUsedChanged);
    refresh();
}

int Preload::playlistTracks() const { return playback_.playlistPreloadDepth(); }

void Preload::setPlaylistTracks(int tracks)
{
    tracks = qBound(0, tracks, kMaxTracks);
    if (tracks == playlistTracks())
        return;
    settings_.setPreloadPlaylistTracks(tracks);
    playback_.setPreloadDepth(tracks, radioTracks());
    emit settingsChanged();
}

int Preload::radioTracks() const { return playback_.radioPreloadDepth(); }

void Preload::setRadioTracks(int tracks)
{
    tracks = qBound(0, tracks, kMaxTracks);
    if (tracks == radioTracks())
        return;
    settings_.setPreloadRadioTracks(tracks);
    playback_.setPreloadDepth(playlistTracks(), tracks);
    emit settingsChanged();
}

int Preload::cacheMaxMb() const { return int(playback_.trackCache().maxBytes() / kMegabyte); }

void Preload::setCacheMaxMb(int megabytes)
{
    megabytes = qMax(0, megabytes);
    if (megabytes == cacheMaxMb())
        return;
    settings_.setPreloadCacheMaxMb(megabytes);
    playback_.trackCache().setMaxBytes(megabytes * kMegabyte);
    emit settingsChanged();
}

qint64 Preload::cacheUsedBytes() const { return playback_.trackCache().totalBytes(); }

void Preload::clearCache() { playback_.trackCache().clear(); }

QAbstractItemModel* Preload::upcoming() const { return model_; }

int Preload::readyCount() const
{
    return int(
        std::count_if(tracks_.cbegin(), tracks_.cend(), [](const Slot& slot) { return slot.state == State::Ready; }));
}

double Preload::overallFraction() const
{
    if (tracks_.isEmpty())
        return 0;
    // Stepped: a whole track at a time.
    if (progressMode_ == ProgressMode::Stepped)
        return double(readyCount()) / double(tracks_.size());
    double sum = 0;
    for (const Slot& slot : tracks_)
        sum += slot.fraction;
    return sum / double(tracks_.size());
}

QString Preload::summaryText() const
{
    if (tracks_.isEmpty())
        return { };
    QString text = tr("Downloaded ahead: %1 of %2").arg(readyCount()).arg(tracks_.size());
    for (const Slot& slot : tracks_) {
        if (slot.state == State::Downloading) {
            text += QLatin1Char('\n') + describe(slot);
            break;
        }
    }
    return text;
}

QString Preload::describe(const Slot& slot)
{
    const int percent = int(std::floor(slot.fraction * 100));
    switch (slot.state) {
        case State::Ready:
            return tr("%1 — downloaded").arg(slot.title);
        case State::Downloading:
            return tr("%1 — %2%").arg(slot.title).arg(percent);
        case State::Failed:
            return tr("%1 — couldn't be downloaded").arg(slot.title);
        case State::Waiting:
            break;
    }
    return percent > 0 ? tr("%1 — waiting, %2% so far").arg(slot.title).arg(percent)
                       : tr("%1 — waiting").arg(slot.title);
}

Preload::State Preload::stateFor(const QString& sourceId, const QString& trackId) const
{
    if (playback_.preloader().isLocal(sourceId, trackId))
        return State::Waiting;
    return stateOf(playback_.preloader().stateOf(sourceId, trackId));
}

double Preload::fractionFor(const QString& sourceId, const QString& trackId) const
{
    if (playing_.sourceId == sourceId && playing_.trackId == trackId)
        return playing_.fraction;
    for (const Slot& slot : tracks_) {
        if (slot.sourceId == sourceId && slot.trackId == trackId)
            return slot.fraction;
    }
    return stateFor(sourceId, trackId) == State::Ready ? 1 : 0;
}

void Preload::setProgressMode(ProgressMode mode)
{
    if (progressMode_ == mode)
        return;
    progressMode_ = mode;
    throttle_->cancel();
    emit progressModeChanged();
    refresh();
    emit progressChanged(); // overallFraction() counts differently now
}

double Preload::reported(double fraction) const
{
    return progressMode_ == ProgressMode::Stepped ? std::floor(fraction * kSteps) / kSteps : fraction;
}

Preload::Window Preload::window() const
{
    Slot playing;
    QVector<Slot> fresh;
    const QVector<Playback::Preloader::Slot> window = playback_.preloader().window();
    for (qsizetype i = 0; i < window.size(); ++i) {
        const Playback::Preloader::Slot& from = window.at(i);
        // A source's own files: nothing is downloaded, so nothing shows.
        if (from.local)
            continue;
        const Slot slot { from.sourceId, from.trackId, from.title, stateOf(from.state), reported(from.fraction) };
        if (i == 0)
            playing = slot;
        else
            fresh.append(slot);
    }
    return { playing, fresh };
}

void Preload::refresh()
{
    const Window now = window();
    const Slot& playing = now.playing;
    const QVector<Slot>& fresh = now.upcoming;

    // What changed besides how far a download has got is shown at once;
    // so is everything in Stepped mode, where progress rarely changes.
    bool progressOnly = progressMode_ == ProgressMode::Smooth && fresh.size() == tracks_.size()
        && sameTrack(playing, playing_) && playing.state == playing_.state;
    for (qsizetype i = 0; progressOnly && i < fresh.size(); ++i)
        progressOnly = sameTrack(fresh.at(i), tracks_.at(i)) && fresh.at(i).state == tracks_.at(i).state;
    if (progressOnly)
        throttle_->trigger();
    else
        apply(playing, fresh);
}

void Preload::apply(const Slot& playing, const QVector<Slot>& fresh)
{
    if (!sameTrack(playing, playing_) || !sameLook(playing, playing_)) {
        const Slot before = std::exchange(playing_, playing);
        if (!sameTrack(before, playing) && !before.trackId.isEmpty())
            emit trackChanged(before.sourceId, before.trackId);
        if (!playing.trackId.isEmpty())
            emit trackChanged(playing.sourceId, playing.trackId);
    }

    bool sameRows = fresh.size() == tracks_.size();
    for (qsizetype i = 0; sameRows && i < fresh.size(); ++i)
        sameRows = sameTrack(fresh.at(i), tracks_.at(i));
    bool changed = !sameRows;
    if (!sameRows) {
        const QVector<Slot> before = tracks_;
        model_->replace(fresh);
        for (const Slot& slot : before)
            emit trackChanged(slot.sourceId, slot.trackId);
        for (const Slot& slot : fresh)
            emit trackChanged(slot.sourceId, slot.trackId);
    } else {
        for (qsizetype i = 0; i < fresh.size(); ++i) {
            if (sameLook(fresh.at(i), tracks_.at(i)))
                continue;
            model_->update(int(i), fresh.at(i));
            emit trackChanged(fresh.at(i).sourceId, fresh.at(i).trackId);
            changed = true;
        }
    }
    if (changed)
        emit progressChanged();
}

} // namespace ViewModel
