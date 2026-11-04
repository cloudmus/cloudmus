#include "Connectivity.h"

#include <QLoggingCategory>
#include <QNetworkInformation>

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcConnectivity, "cloudmus.app.connectivity")
// For the network to settle once it's said to be back.
constexpr int kAfterSystemSaysOnlineMs = 1000;
}

Connectivity::Connectivity(QObject* parent)
    : QObject(parent)
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Connectivity::retry);

    // Not everywhere: the retries work without it.
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        qCInfo(lcConnectivity) << "the system doesn't say whether there's a network; backends:"
                               << QNetworkInformation::availableBackends();
        return;
    }
    QNetworkInformation* information = QNetworkInformation::instance();
    const auto follow = [this](QNetworkInformation::Reachability reachability) {
        qCInfo(lcConnectivity) << "the system says" << reachability;
        setSystemOffline(reachability == QNetworkInformation::Reachability::Disconnected);
    };
    connect(information, &QNetworkInformation::reachabilityChanged, this, follow);
    follow(information->reachability());
}

void Connectivity::setRetryDelays(int firstMs, int maxMs)
{
    firstDelayMs_ = firstMs;
    maxDelayMs_ = maxMs;
    delayMs_ = firstMs;
}

void Connectivity::suspect(const QString& sourceId)
{
    suspects_.insert(sourceId);
    if (!timer_.isActive())
        timer_.start(delayMs_);
}

void Connectivity::reportOffline(const QString& sourceId)
{
    const bool was = offline();
    offlineSources_.insert(sourceId);
    suspect(sourceId);
    notify(was);
}

void Connectivity::reportReached(const QString& sourceId)
{
    const bool was = offline();
    suspects_.remove(sourceId);
    // The network is back, not just a source that answers while another
    // thing failed: the next failure is news again.
    if (offlineSources_.remove(sourceId))
        delayMs_ = firstDelayMs_;
    systemOffline_ = false;
    if (suspects_.isEmpty())
        timer_.stop();
    notify(was);
}

void Connectivity::setSystemOffline(bool isOffline)
{
    const bool was = offline();
    systemOffline_ = isOffline;
    if (!isOffline) {
        delayMs_ = firstDelayMs_;
        if (!suspects_.isEmpty())
            timer_.start(kAfterSystemSaysOnlineMs);
    }
    notify(was);
}

void Connectivity::retry()
{
    // Asked again as a result, whichever way it goes: a failure lands back here.
    const QSet<QString> sources = suspects_;
    delayMs_ = qMin(delayMs_ * 2, maxDelayMs_);
    timer_.start(delayMs_);
    for (const QString& sourceId : sources)
        emit retryWanted(sourceId);
}

void Connectivity::notify(bool wasOffline)
{
    if (wasOffline != offline())
        emit offlineChanged();
}

} // namespace App
