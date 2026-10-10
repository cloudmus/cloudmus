#pragma once

#include <QHash>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

#include "Coro.h"
#include "Models.h"

namespace Playback {

class NetworkPool;
class StreamDownload;
class TrackCache;

// Downloads the track that plays and the ones after it into the TrackCache,
// strictly one at a time and in play order: a track is only started once
// everything before it is there, so nothing competes with the track the
// listener is waiting for.
//
// It is told what's wanted as a whole — setWindow(): the playing track
// first, then the upcoming ones — and works out the rest itself. A track
// that leaves the window stops downloading; one that stays keeps what it
// has; whatever is complete stays in the cache whether wanted or not.
//
// Tracks the listener asked to save are fetched the same way (demand()):
// after the playing one and before the upcoming ones, so saving neither
// holds up what plays nor competes with it.
class Preloader : public QObject {
    Q_OBJECT

public:
    struct Item {
        QString sourceId;
        QString trackId;
        QString title;
        // Where to fetch it from, when already known (the playing track's
        // comes from playback.play).
        std::optional<StreamDescriptor> stream;
        // Without a stream: ask the resolver for one. The playing track is
        // not resolved here — until its stream arrives nothing downloads.
        bool resolve = true;
        // A source's own file, as of a source that has no network: there is
        // nothing to download, and nothing to show of it as preloaded. It is
        // still resolved, to be queued in mpv for a start without a gap.
        bool local = false;
    };

    enum class State {
        Waiting,
        Downloading, // resolving its URL counts too
        Ready,
        Failed,
    };

    // One wanted track, as it stands.
    struct Slot {
        QString sourceId;
        QString trackId;
        QString title;
        State state = State::Waiting;
        double fraction = 0; // downloaded, 0..1
        bool local = false; // see Item::local
    };

    // A stream being resolved, and how to tell the source to stop.
    struct Resolution {
        Rpc::Task<StreamDescriptor> task;
        std::function<void()> cancel;
    };
    // None: the source can't resolve a track that isn't about to play.
    using Resolver = std::function<std::optional<Resolution>(const QString& sourceId, const QString& trackId)>;
    using RouteProvider = std::function<std::optional<QNetworkProxy>(const QString& sourceId)>;

    explicit Preloader(TrackCache& cache, QObject* parent = nullptr);
    ~Preloader() override;

    void setResolver(Resolver resolver) { resolver_ = std::move(resolver); }
    void setRouteProvider(RouteProvider provider) { routeProvider_ = std::move(provider); }

    void setWindow(const QVector<Item>& items);
    // The window as it stands, in order.
    QVector<Slot> window() const;
    // Any track, wanted or not.
    State stateOf(const QString& sourceId, const QString& trackId) const;

    // The track is one of a source's own files (Item::local).
    bool isLocal(const QString& sourceId, const QString& trackId) const;

    // The whole track is on disk.
    bool isCached(const QString& sourceId, const QString& trackId) const;
    // The file to play the whole track from — the cached one, or a local
    // source's own; empty if there is none (yet).
    QString readyPath(const QString& sourceId, const QString& trackId);
    // The download of a wanted track that has one under way or paused.
    StreamDownload* download(const QString& sourceId, const QString& trackId) const;
    // Forgets a track that turned out unplayable, cached file included.
    void discard(const QString& sourceId, const QString& trackId);
    // Another try for a source's wanted tracks that failed — it wasn't
    // there to ask when they were first wanted, and is now.
    void retryFailed(const QString& sourceId);

    // A track to fetch because it is to be saved, whether or not it is in
    // the window. Told by ready() or failed() as for any other; asking for
    // one that is there already tells nothing — see stateOf().
    void demand(const Item& item);
    // The demand is over — the file was saved, or no longer wanted. A track
    // outside the window is dropped (cached file included): saving a
    // playlist mustn't fill the playback cache with it.
    void release(const QString& sourceId, const QString& trackId);

    // How much of a track has been fetched, and of how much; total -1
    // while the length is not known.
    struct Transfer {
        qint64 received = 0;
        qint64 total = -1;
    };
    Transfer transferOf(const QString& sourceId, const QString& trackId) const;

signals:
    void ready(const QString& sourceId, const QString& trackId);
    void failed(const QString& sourceId, const QString& trackId);
    // window() changed: the window itself, a state, or just the progress.
    void progressChanged();
    // stateOf() changed for this track.
    void trackStateChanged(const QString& sourceId, const QString& trackId);

private:
    struct Entry {
        Item item;
        State state = State::Waiting;
        StreamDownload* download = nullptr;
        QString readyPath;
        // Asked for a URL, not answered yet.
        bool resolving = false;
        std::function<void()> cancelResolve;
        int resolveGeneration = 0;
        int freshUrls = 0; // resolved again after the URL expired
    };

    // The keys in the order they are fetched: the playing track, what was
    // demanded, then the rest of the window.
    QVector<QString> sequence() const;
    void pinWanted();
    // A fresh start for a track that failed (a stream known now, or asked for).
    void restart(const QString& key);
    void schedule();
    void scheduleLater();
    // False: it can't even be started — failed on the spot.
    bool start(const QString& key);
    // False: nobody to ask.
    bool resolve(const QString& key);
    void giveUpOn(const QString& key);
    Rpc::Task<void> resolveAsync(QString key, int generation, Rpc::Task<StreamDescriptor> task);
    void onResolved(const QString& key, int generation, const std::optional<StreamDescriptor>& stream);
    void begin(const QString& key, const StreamDescriptor& stream);
    void suspend(Entry& entry);
    void onCompleted(const QString& key);
    void onFailed(const QString& key, bool expired);
    void setState(const QString& key, State state);
    void dropEntry(const QString& key);
    void stopResolving(Entry& entry);

    TrackCache& cache_;
    NetworkPool* network_ = nullptr;
    Resolver resolver_;
    RouteProvider routeProvider_;
    QVector<QString> order_; // keys, in play order
    QVector<QString> demanded_; // keys, in the order they were asked for
    QHash<QString, Entry> entries_;
    bool schedulePending_ = false;
};

} // namespace Playback
