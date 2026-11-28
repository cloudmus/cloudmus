#include "ConnectionStatus.h"

#include <QLocale>

#include "Connectivity.h"
#include "PlaybackController.h"
#include "RpcClient.h"
#include "SourceManager.h"

namespace ViewModel {

ConnectionStatus::ConnectionStatus(App::Connectivity& connectivity, Rpc::SourceManager& sourceManager,
    Playback::PlaybackController& playback, QObject* parent)
    : QObject(parent)
    , connectivity_(connectivity)
    , sourceManager_(sourceManager)
    , playback_(playback)
{
    connect(&connectivity_, &App::Connectivity::offlineChanged, this, &ConnectionStatus::changed);
    connect(&connectivity_, &App::Connectivity::stateChanged, this, &ConnectionStatus::changed);
    connect(&connectivity_, &App::Connectivity::checkingChanged, this, &ConnectionStatus::checkingChanged);
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, &ConnectionStatus::applyPause);
}

int ConnectionStatus::state() const
{
    if (connectivity_.systemOffline())
        return NoNetwork;
    return connectivity_.offlineSources().isEmpty() ? Online : Unreachable;
}

QString ConnectionStatus::nameOf(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr && !client->sourceName().isEmpty() ? client->sourceName() : sourceId;
}

QStringList ConnectionStatus::unreachable() const
{
    QStringList names;
    for (const QString& sourceId : connectivity_.offlineSources())
        names.append(nameOf(sourceId));
    return names;
}

QString ConnectionStatus::text() const
{
    switch (state()) {
        case NoNetwork:
            return tr("No network");
        case Unreachable: {
            const QStringList names = unreachable();
            return names.size() == 1 ? tr("%1 is unreachable").arg(names.first()) : tr("Services are unreachable");
        }
        default:
            return tr("Online");
    }
}

QString ConnectionStatus::details() const
{
    QStringList lines;
    if (connectivity_.systemOffline())
        lines.append(tr("The device is not connected to a network"));
    for (const QString& sourceId : connectivity_.offlineSources()) {
        const QString since
            = QLocale().toString(connectivity_.offlineSince(sourceId).toLocalTime().time(), QLocale::ShortFormat);
        // A few words from the source; whatever it says, not a paragraph.
        constexpr int kMaxDetail = 100;
        QString detail = connectivity_.offlineDetail(sourceId).simplified();
        if (detail.size() > kMaxDetail)
            detail = detail.left(kMaxDetail - 1) + QChar(0x2026);
        lines.append(detail.isEmpty() ? tr("%1: unreachable since %2").arg(nameOf(sourceId), since)
                                      : tr("%1: unreachable since %2 (%3)").arg(nameOf(sourceId), since, detail));
    }
    if (pendingActions_ > 0)
        lines.append(tr("%n action(s) will be sent once the connection is back", nullptr, pendingActions_));
    return lines.join(QLatin1Char('\n'));
}

bool ConnectionStatus::checking() const { return connectivity_.checking(); }

void ConnectionStatus::setPendingActions(int count)
{
    if (pendingActions_ == count)
        return;
    pendingActions_ = count;
    emit changed();
}

void ConnectionStatus::checkNow() { connectivity_.checkNow(); }

void ConnectionStatus::setViewShown(bool shown)
{
    viewShown_ = shown;
    applyPause();
}

void ConnectionStatus::applyPause() { connectivity_.setPaused(!viewShown_ && !playback_.isPlaying()); }

} // namespace ViewModel
