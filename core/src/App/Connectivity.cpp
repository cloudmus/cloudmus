#include "Connectivity.h"

#include <QLoggingCategory>
#include <QNetworkInformation>
#include <QPointer>

#include <algorithm>

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcConnectivity, "cloudmus.app.connectivity")
// For the network to settle once it's said to be back.
constexpr int kAfterSystemSaysOnlineMs = 1000;
// Every source, while the system has a network: a service that went away
// quietly shows within this.
constexpr int kPeriodicMs = 30000;
// A failure that a lost network would explain is looked into sooner.
constexpr int kSuspectMs = 5000;
}

Connectivity::Connectivity(SystemNetwork system, QObject* parent)
    : QObject(parent)
{
    periodic_.setInterval(kPeriodicMs);
    connect(&periodic_, &QTimer::timeout, this, &Connectivity::checkAll);
    periodic_.start();
    suspectTimer_.setSingleShot(true);
    suspectTimer_.setInterval(kSuspectMs);
    connect(&suspectTimer_, &QTimer::timeout, this, [this]() {
        const QSet<QString> sources = std::exchange(suspects_, { });
        for (const QString& sourceId : sources)
            runCheck(sourceId).detach();
    });

    if (system == SystemNetwork::Ignore)
        return;
    // Not everywhere: the sources' own checks work without it.
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        qCInfo(lcConnectivity) << "the system doesn't say whether there's a network; backends:"
                               << QNetworkInformation::availableBackends();
        return;
    }
    QNetworkInformation* information = QNetworkInformation::instance();
    const auto follow = [this](QNetworkInformation::Reachability reachability) {
        qCInfo(lcConnectivity) << "the system says" << reachability;
        // Unknown says nothing: only a network that is gone counts.
        setSystemOffline(reachability == QNetworkInformation::Reachability::Disconnected);
    };
    connect(information, &QNetworkInformation::reachabilityChanged, this, follow);
    follow(information->reachability());
}

QStringList Connectivity::offlineSources() const
{
    QStringList ids = offlineSources_.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void Connectivity::setIntervals(int periodicMs, int suspectMs)
{
    periodic_.setInterval(periodicMs);
    suspectTimer_.setInterval(suspectMs);
    if (!paused_)
        periodic_.start();
}

void Connectivity::setPaused(bool paused)
{
    if (paused_ == paused)
        return;
    paused_ = paused;
    if (paused) {
        periodic_.stop();
        return;
    }
    periodic_.start();
    // Woken up: what it missed meanwhile.
    checkAll();
}

void Connectivity::suspect(const QString& sourceId)
{
    if (systemOffline_)
        return;
    suspects_.insert(sourceId);
    if (!suspectTimer_.isActive())
        suspectTimer_.start();
}

void Connectivity::reportOffline(const QString& sourceId, const QString& detail) { setOffline(sourceId, true, detail); }

void Connectivity::reportReached(const QString& sourceId)
{
    suspects_.remove(sourceId);
    setOffline(sourceId, false, QString());
}

void Connectivity::setOffline(const QString& sourceId, bool isOffline, const QString& detail)
{
    const bool was = offline();
    const bool sourceWas = offlineSources_.contains(sourceId);
    if (isOffline) {
        if (!sourceWas)
            offlineSources_.insert(sourceId, QDateTime::currentDateTimeUtc());
        details_.insert(sourceId, detail);
    } else {
        offlineSources_.remove(sourceId);
        details_.remove(sourceId);
    }
    if (sourceWas != isOffline) {
        qCInfo(lcConnectivity) << sourceId << (isOffline ? "is offline" : "is back online") << detail;
        emit stateChanged(sourceId);
        // Without the system's network it isn't back for anyone yet.
        if (!isOffline && !systemOffline_)
            emit sourceOnline(sourceId);
    }
    notify(was);
}

void Connectivity::setSystemOffline(bool isOffline)
{
    if (systemOffline_ == isOffline)
        return;
    const bool was = offline();
    systemOffline_ = isOffline;
    const QStringList sources = sources_ ? sources_() : QStringList();
    if (isOffline) {
        suspects_.clear();
        suspectTimer_.stop();
        pendingOnline_.clear();
    } else {
        // Whatever each source makes of it, now — not in up to 30 seconds.
        pendingOnline_ = QSet<QString>(sources.begin(), sources.end());
        QTimer::singleShot(kAfterSystemSaysOnlineMs, this, &Connectivity::checkAll);
    }
    for (const QString& sourceId : sources)
        emit stateChanged(sourceId);
    notify(was);
}

void Connectivity::checkNow()
{
    if (systemOffline_) {
        // Nothing to ask over no network; the indicator says why.
        return;
    }
    checkAll();
}

void Connectivity::checkAll()
{
    if (systemOffline_ || !sources_)
        return;
    for (const QString& sourceId : sources_())
        runCheck(sourceId).detach();
}

Rpc::Task<bool> Connectivity::checkSource(QString sourceId)
{
    if (systemOffline_)
        co_return false;
    co_await runCheck(sourceId);
    co_return !isOffline(sourceId);
}

Rpc::Task<void> Connectivity::runCheck(QString sourceId)
{
    if (systemOffline_ || checkingSources_.contains(sourceId))
        co_return;
    if (!checker_) {
        emit retryWanted(sourceId);
        co_return;
    }
    checkingSources_.insert(sourceId);
    if (running_++ == 0)
        emit checkingChanged();
    const QPointer<Connectivity> alive(this);
    std::optional<CheckResult> result;
    try {
        result = co_await checker_(sourceId);
    } catch (const std::exception& e) {
        // Asking failed: the source isn't answering, which is no network
        // of its own — leave the state as it was.
        qCDebug(lcConnectivity) << "checking" << sourceId << "failed:" << e.what();
    }
    if (!alive)
        co_return;
    checkingSources_.remove(sourceId);
    if (--running_ == 0)
        emit checkingChanged();
    if (!result) {
        pendingOnline_.remove(sourceId);
        emit retryWanted(sourceId);
        co_return;
    }
    // The system may have said "no network" while the check ran.
    if (systemOffline_)
        co_return;
    const bool wasOffline = offlineSources_.contains(sourceId);
    setOffline(sourceId, !result->online, result->detail);
    // Back with the system's network, and not offline before it went: that
    // too is coming back, for what waited for it (setOffline() tells the rest).
    if (pendingOnline_.remove(sourceId) && result->online && !wasOffline)
        emit sourceOnline(sourceId);
}

void Connectivity::notify(bool wasOffline)
{
    if (wasOffline != offline())
        emit offlineChanged();
}

} // namespace App
