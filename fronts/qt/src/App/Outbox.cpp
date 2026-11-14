#include "Outbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QSaveFile>

#include "RpcClient.h"
#include "SourceManager.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcOutbox, "cloudmus.app.outbox")
constexpr int kCallTimeoutMs = 20000;

// The source said no for good: sending it again would change nothing.
bool isFinal(const Rpc::RpcError& error)
{
    if (error.data.contains(QStringLiteral("retryable")) && !error.data.value(QStringLiteral("retryable")).toBool())
        return true;
    // Method not found, invalid params; a capability or resource error.
    return error.code == -32601 || error.code == -32602 || (error.code >= 1100 && error.code <= 1399);
}
}

Outbox::Outbox(Rpc::SourceManager& sourceManager, QString filePath, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , filePath_(std::move(filePath))
{
    load();
}

bool Outbox::holds(const QString& sourceId) const
{
    if (offline_ && offline_(sourceId))
        return true;
    for (const Item& item : items_) {
        if (item.sourceId == sourceId)
            return true;
    }
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client == nullptr || !client->available();
}

bool Outbox::contains(const QString& sourceId, const QString& key) const
{
    for (const Item& item : items_) {
        if (item.sourceId == sourceId && item.key == key)
            return true;
    }
    return false;
}

void Outbox::enqueue(const QString& sourceId, const QString& method, const QJsonObject& params, const QString& key)
{
    if (!key.isEmpty())
        items_.removeIf([&](const Item& item) { return item.sourceId == sourceId && item.key == key; });
    items_.append(Item { sourceId, method, params, key, QDateTime::currentDateTimeUtc(), 0, nextId_++ });
    // Events past the cap: the oldest go (a keyed item is a state, and stays).
    int events = 0;
    for (int i = int(items_.size()) - 1; i >= 0; --i) {
        if (items_[i].sourceId != sourceId || !items_[i].key.isEmpty())
            continue;
        if (++events > kMaxPerSource)
            items_.removeAt(i);
    }
    qCInfo(lcOutbox) << "queued" << method << "for" << sourceId << "(" << items_.size() << "waiting)";
    changed();
    if (!offline_ || !offline_(sourceId))
        flush(sourceId).detach();
}

void Outbox::removeSource(const QString& sourceId)
{
    if (items_.removeIf([&](const Item& item) { return item.sourceId == sourceId; }) > 0)
        changed();
}

Rpc::Task<void> Outbox::flush(QString sourceId)
{
    if (flushing_.contains(sourceId))
        co_return;
    flushing_.insert(sourceId);
    const QPointer<Outbox> alive(this);
    while (true) {
        Rpc::RpcClient* client = sourceManager_.client(sourceId);
        if (client == nullptr || !client->available())
            break;
        int index = -1;
        for (int i = 0; i < items_.size(); ++i) {
            if (items_[i].sourceId == sourceId) {
                index = i;
                break;
            }
        }
        if (index < 0)
            break;
        const Item item = items_[index];
        // Whatever happened to the queue meanwhile (a newer one in its place),
        // it's this one that was sent.
        const auto sameItem = [&item](const Item& i) { return i.id == item.id; };
        bool keepGoing = true;
        try {
            co_await client->callRaw(item.method, item.params, kCallTimeoutMs);
            if (!alive)
                co_return;
            qCInfo(lcOutbox) << "sent" << item.method << "to" << sourceId;
            items_.removeIf(sameItem);
            changed();
            emit delivered(item);
        } catch (const Rpc::RpcCallException& e) {
            if (!alive)
                co_return;
            const Rpc::RpcError& error = e.error();
            if (e.isOffline()) {
                qCInfo(lcOutbox) << "still offline:" << sourceId;
                emit offlineSeen(sourceId);
                keepGoing = false;
            } else if (isFinal(error) || item.attempts + 1 >= kMaxAttempts) {
                qCWarning(lcOutbox) << "dropping" << item.method << "for" << sourceId << ":" << error.message;
                items_.removeIf(sameItem);
                changed();
                emit dropped(item, error.message);
            } else {
                // A timeout, a hiccup: tried again next time, not now.
                for (Item& i : items_) {
                    if (sameItem(i))
                        ++i.attempts;
                }
                save();
                keepGoing = false;
            }
        } catch (const std::exception& e) {
            // The source went away mid-call: next time.
            if (!alive)
                co_return;
            qCDebug(lcOutbox) << "sending" << item.method << "failed:" << e.what();
            keepGoing = false;
        }
        if (!keepGoing)
            break;
    }
    if (alive)
        flushing_.remove(sourceId);
}

void Outbox::changed()
{
    save();
    emit countChanged(count());
}

void Outbox::load()
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue& value : array) {
        const QJsonObject obj = value.toObject();
        Item item {
            obj.value(QStringLiteral("sourceId")).toString(),
            obj.value(QStringLiteral("method")).toString(),
            obj.value(QStringLiteral("params")).toObject(),
            obj.value(QStringLiteral("key")).toString(),
            QDateTime::fromString(obj.value(QStringLiteral("queuedAt")).toString(), Qt::ISODateWithMs),
            obj.value(QStringLiteral("attempts")).toInt(),
            nextId_++,
        };
        if (!item.sourceId.isEmpty() && !item.method.isEmpty())
            items_.append(item);
    }
    qCInfo(lcOutbox) << items_.size() << "actions waiting from the last run";
}

void Outbox::save() const
{
    if (filePath_.isEmpty())
        return;
    if (items_.isEmpty()) {
        QFile::remove(filePath_);
        return;
    }
    QJsonArray array;
    for (const Item& item : items_) {
        QJsonObject obj {
            { QStringLiteral("sourceId"), item.sourceId },
            { QStringLiteral("method"), item.method },
            { QStringLiteral("params"), item.params },
            { QStringLiteral("queuedAt"), item.queuedAt.toString(Qt::ISODateWithMs) },
        };
        if (!item.key.isEmpty())
            obj.insert(QStringLiteral("key"), item.key);
        if (item.attempts > 0)
            obj.insert(QStringLiteral("attempts"), item.attempts);
        array.append(obj);
    }
    QDir().mkpath(QFileInfo(filePath_).absolutePath());
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcOutbox) << "can't save" << filePath_ << file.errorString();
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace App
