#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

#include "Coro.h"

namespace Rpc {
class SourceManager;
}

namespace App {

// What the user did that a source couldn't be told yet — a like, a playlist
// edit, a play for the history — kept on disk until the source can reach its
// service again, then sent in the order it was done (docs/protocol.md §7.4,
// "Sent late"). Whoever queues it shows it as done already.
//
// An item has a key when only the last one of its kind counts (the like
// state of a track, a track's place in a playlist): a newer one replaces it,
// at the end. Events without one (a play, a skip) are each sent, the oldest
// dropped past kMaxPerSource.
class Outbox : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxPerSource = 500;
    // A call that keeps failing short of "offline" is given up after that.
    static constexpr int kMaxAttempts = 5;

    struct Item {
        QString sourceId;
        QString method;
        QJsonObject params;
        QString key;
        QDateTime queuedAt;
        int attempts = 0;
        // For this run only: which one it is, whatever else is alike.
        quint64 id = 0;
    };

    // Whether the source can't be reached now (App::Connectivity).
    using OfflineProvider = std::function<bool(const QString& sourceId)>;

    Outbox(Rpc::SourceManager& sourceManager, QString filePath, QObject* parent = nullptr);

    void setOfflineProvider(OfflineProvider provider) { offline_ = std::move(provider); }

    // Whether something for the source should be queued rather than sent: it
    // can't be reached, or what was queued before still waits (it goes first).
    bool holds(const QString& sourceId) const;
    // Queued, saved, and sent at once if the source can be reached.
    void enqueue(
        const QString& sourceId, const QString& method, const QJsonObject& params, const QString& key = QString());
    int count() const { return int(items_.size()); }
    QList<Item> items() const { return items_; }
    bool contains(const QString& sourceId, const QString& key) const;
    // Signed out: it isn't the user's any more.
    void removeSource(const QString& sourceId);

    // Sends what waits for the source, in order, until one fails for lack of
    // a network (the rest wait for the next time).
    Rpc::Task<void> flush(QString sourceId);

signals:
    void countChanged(int count);
    // Reached the source.
    void delivered(const Item& item);
    // Given up on (the source said no for good, or after kMaxAttempts).
    void dropped(const Item& item, const QString& error);
    void offlineSeen(const QString& sourceId);

private:
    void load();
    void save() const;
    void changed();

    Rpc::SourceManager& sourceManager_;
    QString filePath_;
    OfflineProvider offline_;
    QList<Item> items_;
    quint64 nextId_ = 1;
    // A flush under way, by source: another ask waits for the next time.
    QSet<QString> flushing_;
};

} // namespace App
