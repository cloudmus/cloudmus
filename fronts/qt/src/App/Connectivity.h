#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <optional>

#include "Coro.h"

namespace App {

// Whether each source can reach its service, and the network as the system
// sees it (QNetworkInformation), which wins: while the system says there is
// no network every source counts as offline and nothing is checked; once it
// says there is one, every source is checked at once.
//
// Otherwise the sources tell: a request failing for lack of a network, a
// connection/changed of their own, an answer to connection.check
// (docs/protocol.md §7.9). Each networked source is checked every 30 seconds,
// at once on checkNow() (the offline indicator), and by whoever is about to
// need it (checkSource(), playback before a track that isn't downloaded).
// A source without connectionCheck is "checked" by retryWanted, which lists
// its playlists again.
class Connectivity : public QObject {
    Q_OBJECT

public:
    enum class SystemNetwork {
        Follow, // QNetworkInformation, where there is a backend for it
        Ignore, // for a test: setSystemOffline() only
    };

    struct CheckResult {
        bool online = true;
        QString detail; // why not, for a tooltip
    };
    // nullopt: the source can't be asked that way (not running, or no
    // connectionCheck); retryWanted() goes out instead.
    using Checker = std::function<Rpc::Task<std::optional<CheckResult>>(QString sourceId)>;
    // The sources that reach the network, running or not.
    using SourcesProvider = std::function<QStringList()>;

    explicit Connectivity(SystemNetwork system = SystemNetwork::Follow, QObject* parent = nullptr);

    void setChecker(Checker checker) { checker_ = std::move(checker); }
    void setSourcesProvider(SourcesProvider provider) { sources_ = std::move(provider); }

    // Anything at all offline: the system's network, or a source.
    bool offline() const { return systemOffline_ || !offlineSources_.isEmpty(); }
    bool systemOffline() const { return systemOffline_; }
    bool isOffline(const QString& sourceId) const { return systemOffline_ || offlineSources_.contains(sourceId); }
    // The sources found unreachable (not counting the system's network).
    QStringList offlineSources() const;
    // Since when; invalid if it isn't.
    QDateTime offlineSince(const QString& sourceId) const { return offlineSources_.value(sourceId); }
    // What the source said about it, for a tooltip.
    QString offlineDetail(const QString& sourceId) const { return details_.value(sourceId); }
    // A check is running (the indicator spins).
    bool checking() const { return running_ > 0; }

    // Something failed that a lost network would explain: the source is
    // asked again soon, without it being called offline yet.
    void suspect(const QString& sourceId);
    // The source said so itself.
    void reportOffline(const QString& sourceId, const QString& detail = QString());
    // The source answered.
    void reportReached(const QString& sourceId);

    // Every source, now (the indicator's click).
    void checkNow();
    // The one source, awaited; true if it can be reached. Without the system's
    // network, false at once.
    Rpc::Task<bool> checkSource(QString sourceId);

    // No periodic checks while nothing needs them (the app in the
    // background on a phone, playing nothing).
    void setPaused(bool paused);
    // For a test.
    void setIntervals(int periodicMs, int suspectMs);
    void setSystemOffline(bool offline);

signals:
    void offlineChanged();
    // A source turned offline or back online.
    void stateChanged(const QString& sourceId);
    void checkingChanged();
    // Back online after being offline (the system's network included):
    // what waited for it can go on.
    void sourceOnline(const QString& sourceId);
    // Ask the source again the old way (catalog.listPlaylists).
    void retryWanted(const QString& sourceId);

private:
    Rpc::Task<void> runCheck(QString sourceId);
    void checkAll();
    void setOffline(const QString& sourceId, bool offline, const QString& detail);
    void notify(bool wasOffline);

    Checker checker_;
    SourcesProvider sources_;
    QTimer periodic_;
    QTimer suspectTimer_;
    QSet<QString> suspects_;
    QHash<QString, QDateTime> offlineSources_;
    QHash<QString, QString> details_;
    // Checks under way, by source: a second ask joins the first.
    QSet<QString> checkingSources_;
    // Back from the system's "no network": each says so once its check
    // finds it there.
    QSet<QString> pendingOnline_;
    int running_ = 0;
    bool systemOffline_ = false;
    bool paused_ = false;
};

} // namespace App
