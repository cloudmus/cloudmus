#include "Preloader.h"

#include <QFile>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QUrl>

#include "NetworkPool.h"
#include "RpcClient.h"
#include "StreamDownload.h"
#include "TrackCache.h"

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcPreload, "cloudmus.playback.preload")

// How many times a track whose URL stopped being accepted mid-download
// (403/410) gets a freshly resolved one.
constexpr int kMaxFreshUrls = 2;

} // namespace

Preloader::Preloader(TrackCache& cache, QObject* parent)
    : QObject(parent)
    , cache_(cache)
    , network_(new NetworkPool(this))
{
}

Preloader::~Preloader() = default;

void Preloader::setWindow(const QVector<Item>& items)
{
    QVector<QString> order;
    for (const Item& item : items) {
        const QString key = TrackCache::keyFor(item.sourceId, item.trackId);
        if (order.contains(key))
            continue;
        order.append(key);
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            Entry entry;
            entry.item = item;
            const QString cached = cache_.path(key);
            if (!cached.isEmpty()) {
                entry.state = State::Ready;
                entry.readyPath = cached;
            }
            entries_.insert(key, entry);
            continue;
        }
        it->item.title = item.title;
        it->item.resolve = item.resolve;
        it->item.local = item.local;
        if (!item.stream)
            continue;
        it->item.stream = item.stream;
        // A fresh URL is another chance.
        if (it->state == State::Failed)
            restart(key);
    }
    for (const QString& key : entries_.keys()) {
        if (!order.contains(key) && !demanded_.contains(key))
            dropEntry(key);
    }
    order_ = order;
    pinWanted();
    schedule();
    emit progressChanged();
}

void Preloader::pinWanted()
{
    QSet<QString> wanted(order_.cbegin(), order_.cend());
    wanted.unite(QSet<QString>(demanded_.cbegin(), demanded_.cend()));
    cache_.setPinned(wanted);
}

void Preloader::restart(const QString& key)
{
    Entry& entry = entries_[key];
    // From the start: whatever was fetched before the failure is dropped.
    delete entry.download;
    entry.download = nullptr;
    QFile::remove(cache_.partPath(key));
    entry.freshUrls = 0;
    setState(key, State::Waiting);
}

void Preloader::retryFailed(const QString& sourceId)
{
    bool any = false;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->state != State::Failed || it->item.sourceId != sourceId)
            continue;
        restart(it.key());
        any = true;
    }
    if (!any)
        return;
    schedule();
    emit progressChanged();
}

QVector<QString> Preloader::sequence() const
{
    QVector<QString> result;
    if (!order_.isEmpty())
        result.append(order_.first());
    for (const QString& key : demanded_) {
        if (!result.contains(key))
            result.append(key);
    }
    for (qsizetype i = 1; i < order_.size(); ++i) {
        if (!result.contains(order_.at(i)))
            result.append(order_.at(i));
    }
    return result;
}

void Preloader::demand(const Item& item)
{
    const QString key = TrackCache::keyFor(item.sourceId, item.trackId);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        Entry entry;
        entry.item = item;
        entry.item.resolve = true;
        const QString cached = cache_.path(key);
        if (!cached.isEmpty()) {
            entry.state = State::Ready;
            entry.readyPath = cached;
        }
        entries_.insert(key, entry);
    } else if (it->state == State::Failed) {
        it->item.stream = item.stream ? item.stream : it->item.stream;
        restart(key); // asked for again: another try
    }
    if (!demanded_.contains(key))
        demanded_.append(key);
    pinWanted();
    schedule();
    emit progressChanged();
}

void Preloader::release(const QString& sourceId, const QString& trackId)
{
    const QString key = TrackCache::keyFor(sourceId, trackId);
    if (!demanded_.removeOne(key))
        return;
    pinWanted();
    if (!order_.contains(key)) {
        cache_.remove(key);
        dropEntry(key);
    }
    schedule();
    emit progressChanged();
}

Preloader::Transfer Preloader::transferOf(const QString& sourceId, const QString& trackId) const
{
    const auto it = entries_.constFind(TrackCache::keyFor(sourceId, trackId));
    if (it == entries_.constEnd() || it->download == nullptr)
        return { };
    return { it->download->bytesAvailable(), it->download->totalLength() };
}

void Preloader::scheduleLater()
{
    if (schedulePending_)
        return;
    schedulePending_ = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            schedulePending_ = false;
            schedule();
        },
        Qt::QueuedConnection);
}

void Preloader::schedule()
{
    // Over a copy: what a signal emitted on the way leads to may change
    // the window.
    const QVector<QString> order = sequence();
    // A source's own file takes no part in the queue for the link: only
    // where it is gets found out, which is quick and costs the link nothing.
    for (const QString& key : order) {
        const auto it = entries_.constFind(key);
        if (it != entries_.constEnd() && it->item.local && it->state == State::Waiting
            && (it->item.stream || it->item.resolve))
            start(key);
    }
    QString active;
    for (const QString& key : order) {
        const auto it = entries_.constFind(key);
        if (it == entries_.constEnd() || it->state == State::Ready || it->state == State::Failed)
            continue;
        // Found out above, and nothing to wait for.
        if (it->item.local)
            continue;
        if (it->state == State::Downloading) {
            active = key;
            break;
        }
        // Its stream is still to come (the playing track's, from
        // playback.play): until then nothing else takes the link.
        if (!it->item.stream && it->download == nullptr && !it->item.resolve)
            break;
        if (start(key)) {
            active = key;
            break;
        }
    }
    for (const QString& key : entries_.keys()) {
        const auto it = entries_.find(key);
        if (key != active && it != entries_.end() && it->state == State::Downloading && !it->item.local)
            suspend(*it);
    }
}

bool Preloader::start(const QString& key)
{
    Entry& entry = entries_[key];
    if (entry.download != nullptr) {
        qCDebug(lcPreload) << "resuming" << entry.item.title << "at byte" << entry.download->bytesAvailable();
        setState(key, State::Downloading);
        entry.download->start();
        return true;
    }
    if (entry.item.stream) {
        begin(key, *entry.item.stream);
        return true;
    }
    if (resolve(key))
        return true;
    giveUpOn(key);
    return false;
}

bool Preloader::resolve(const QString& key)
{
    Entry& entry = entries_[key];
    std::optional<Resolution> resolution
        = resolver_ ? resolver_(entry.item.sourceId, entry.item.trackId) : std::nullopt;
    if (!resolution)
        return false;
    qCDebug(lcPreload) << "resolving" << entry.item.title;
    entry.resolving = true;
    entry.cancelResolve = std::move(resolution->cancel);
    const int generation = ++entry.resolveGeneration;
    setState(key, State::Downloading);
    resolveAsync(key, generation, std::move(resolution->task)).detach();
    return true;
}

Rpc::Task<void> Preloader::resolveAsync(QString key, int generation, Rpc::Task<StreamDescriptor> task)
{
    const QPointer<Preloader> alive(this);
    std::optional<StreamDescriptor> stream;
    try {
        stream = co_await std::move(task);
    } catch (const Rpc::RpcCallException& e) {
        qCDebug(lcPreload) << "resolving a track failed:" << e.what();
        const QJsonObject& data = e.error().data;
        // The source says it can never be played: tell, so it is skipped
        // rather than tried again whenever it comes up.
        if (alive && data.value(QStringLiteral("reason")).toString() == QStringLiteral("unavailable")) {
            const auto it = entries_.find(key);
            if (it != entries_.end())
                emit trackUnavailable(it->item.sourceId, it->item.trackId);
        }
    } catch (const std::exception& e) {
        qCDebug(lcPreload) << "resolving a track failed:" << e.what();
    }
    if (alive)
        onResolved(key, generation, stream);
}

void Preloader::onResolved(const QString& key, int generation, const std::optional<StreamDescriptor>& stream)
{
    const auto it = entries_.find(key);
    if (it == entries_.end() || !it->resolving || it->resolveGeneration != generation)
        return;
    it->resolving = false;
    it->cancelResolve = nullptr;
    if (stream) {
        // Copied: a slot may change entries_ before begin() looks it up.
        const QString sourceId = it->item.sourceId;
        const QString trackId = it->item.trackId;
        emit streamResolved(sourceId, trackId, *stream);
        begin(key, *stream);
    } else {
        giveUpOn(key);
    }
}

void Preloader::begin(const QString& key, const StreamDescriptor& stream)
{
    Entry& entry = entries_[key];
    const QUrl url(stream.url);
    if (url.isLocalFile()) {
        // Nothing to download: the source's own file is the track.
        entry.readyPath = url.toLocalFile();
        setState(key, State::Ready);
        emit ready(entry.item.sourceId, entry.item.trackId);
        scheduleLater();
        return;
    }
    const std::optional<QNetworkProxy> route = routeProvider_ ? routeProvider_(entry.item.sourceId) : std::nullopt;
    if (entry.download == nullptr) {
        entry.download = new StreamDownload(*network_, cache_.partPath(key), this);
        connect(entry.download, &StreamDownload::completed, this, [this, key]() { onCompleted(key); });
        connect(entry.download, &StreamDownload::failed, this, [this, key](bool expired) { onFailed(key, expired); });
        connect(entry.download, &StreamDownload::progressed, this, &Preloader::progressChanged);
    }
    qCDebug(lcPreload) << "downloading" << entry.item.title << "from byte" << entry.download->bytesAvailable();
    entry.download->setSource(StreamSource { url, route.value_or(QNetworkProxy(QNetworkProxy::DefaultProxy)),
        stream.headers.value_or(QMap<QString, QString> { }) });
    setState(key, State::Downloading);
    entry.download->start();
}

void Preloader::stopResolving(Entry& entry)
{
    if (!entry.resolving)
        return;
    entry.resolving = false;
    ++entry.resolveGeneration;
    if (const auto cancel = std::exchange(entry.cancelResolve, nullptr))
        cancel();
}

void Preloader::suspend(Entry& entry)
{
    stopResolving(entry);
    if (entry.download != nullptr)
        entry.download->pause();
    qCDebug(lcPreload) << "holding back" << entry.item.title;
    setState(TrackCache::keyFor(entry.item.sourceId, entry.item.trackId), State::Waiting);
}

void Preloader::onCompleted(const QString& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    StreamDownload* download = it->download;
    // Lets go of the file first: Windows can't rename one that's open.
    download->relocate(download->filePath());
    const QString cached = cache_.commit(key);
    if (!cached.isEmpty())
        download->relocate(cached);
    it->readyPath = download->filePath();
    qCDebug(lcPreload) << "downloaded" << it->item.title << download->totalLength() << "bytes";
    const Item item = it->item;
    setState(key, State::Ready);
    emit ready(item.sourceId, item.trackId);
    emit progressChanged();
    scheduleLater();
}

void Preloader::onFailed(const QString& key, bool expired)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    if (expired && it->freshUrls < kMaxFreshUrls) {
        ++it->freshUrls;
        if (resolve(key))
            return;
    }
    giveUpOn(key);
}

void Preloader::giveUpOn(const QString& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    qCDebug(lcPreload) << "giving up on" << it->item.title;
    if (it->download != nullptr)
        it->download->giveUp();
    const Item item = it->item;
    setState(key, State::Failed);
    emit failed(item.sourceId, item.trackId);
    emit progressChanged();
    scheduleLater();
}

void Preloader::setState(const QString& key, State state)
{
    const auto it = entries_.find(key);
    if (it == entries_.end() || it->state == state)
        return;
    it->state = state;
    const Item item = it->item;
    emit trackStateChanged(item.sourceId, item.trackId);
    emit progressChanged(); // window() carries the state too
}

void Preloader::dropEntry(const QString& key)
{
    Entry entry = entries_.take(key);
    stopResolving(entry);
    if (entry.download != nullptr) {
        delete entry.download;
        // Committed to the cache, it is no longer there to remove.
        QFile::remove(cache_.partPath(key));
    }
    if (entry.state != State::Waiting)
        emit trackStateChanged(entry.item.sourceId, entry.item.trackId);
}

QVector<Preloader::Slot> Preloader::window() const
{
    QVector<Slot> result;
    result.reserve(order_.size());
    for (const QString& key : order_) {
        const auto it = entries_.constFind(key);
        if (it == entries_.constEnd())
            continue;
        Slot slot { it->item.sourceId, it->item.trackId, it->item.title, it->state, 0, it->item.local };
        if (it->state == State::Ready)
            slot.fraction = 1;
        else if (it->download != nullptr && it->download->totalLength() > 0)
            slot.fraction = double(it->download->bytesAvailable()) / double(it->download->totalLength());
        result.append(slot);
    }
    return result;
}

Preloader::State Preloader::stateOf(const QString& sourceId, const QString& trackId) const
{
    const QString key = TrackCache::keyFor(sourceId, trackId);
    const auto it = entries_.constFind(key);
    if (it != entries_.constEnd())
        return it->state;
    return cache_.contains(key) ? State::Ready : State::Waiting;
}

bool Preloader::isLocal(const QString& sourceId, const QString& trackId) const
{
    const auto it = entries_.constFind(TrackCache::keyFor(sourceId, trackId));
    return it != entries_.constEnd() && it->item.local;
}

bool Preloader::isCached(const QString& sourceId, const QString& trackId) const
{
    return cache_.contains(TrackCache::keyFor(sourceId, trackId));
}

QString Preloader::cachedFile(const QString& sourceId, const QString& trackId) const
{
    return cache_.location(TrackCache::keyFor(sourceId, trackId));
}

QString Preloader::readyPath(const QString& sourceId, const QString& trackId)
{
    const QString key = TrackCache::keyFor(sourceId, trackId);
    const QString cached = cache_.path(key); // counts as a use
    if (!cached.isEmpty())
        return cached;
    const auto it = entries_.constFind(key);
    return it != entries_.constEnd() && it->state == State::Ready ? it->readyPath : QString();
}

StreamDownload* Preloader::download(const QString& sourceId, const QString& trackId) const
{
    const auto it = entries_.constFind(TrackCache::keyFor(sourceId, trackId));
    if (it == entries_.constEnd() || it->state == State::Ready || it->state == State::Failed)
        return nullptr;
    return it->download;
}

void Preloader::discard(const QString& sourceId, const QString& trackId)
{
    const QString key = TrackCache::keyFor(sourceId, trackId);
    cache_.remove(key);
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    stopResolving(*it);
    if (it->download != nullptr) {
        delete it->download;
        it->download = nullptr;
        QFile::remove(cache_.partPath(key));
    }
    it->readyPath.clear();
    it->item.stream.reset();
    it->freshUrls = 0;
    setState(key, State::Waiting);
    emit progressChanged();
}

} // namespace Playback
