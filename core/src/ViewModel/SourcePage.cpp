#include "SourcePage.h"

#include "SourceSession.h"

namespace ViewModel {

SourcePage::SourcePage(App::SourceSession& sourceSession, QObject* parent)
    : QObject(parent)
    , sourceSession_(sourceSession)
{
}

void SourcePage::signIn(const QString& sourceId)
{
    [](SourcePage* self, QString sourceId) -> Rpc::Task<void> {
        self->setBusy(sourceId, true);
        co_await self->sourceSession_.signIn(sourceId);
        self->setBusy(sourceId, false);
    }(this, sourceId)
                                                  .detach();
}

void SourcePage::submit(const QString& sourceId, const QJsonObject& fields)
{
    setBusy(sourceId, true);
    follow(sourceId, sourceSession_.submitForm(sourceId, fields));
}

void SourcePage::submitBrowser(const QString& sourceId, const QJsonObject& result)
{
    AuthBrowserResult parsed;
    try {
        parsed = AuthBrowserResult::fromJson(result);
    } catch (const std::exception& e) {
        emit submitFailed(sourceId, QString::fromStdString(e.what()));
        return;
    }
    setBusy(sourceId, true);
    follow(sourceId, sourceSession_.submitBrowser(sourceId, parsed));
}

void SourcePage::follow(const QString& sourceId, Rpc::Task<QString> submission)
{
    [](SourcePage* self, QString sourceId, Rpc::Task<QString> submission) -> Rpc::Task<void> {
        const QString error = co_await std::move(submission);
        if (!error.isEmpty())
            emit self->submitFailed(sourceId, error);
        self->setBusy(sourceId, false);
    }(this, sourceId, std::move(submission))
                                                                                 .detach();
}

bool SourcePage::wake(const QString& sourceId) { return sourceSession_.wake(sourceId); }

QJsonObject SourcePage::capabilities(const QString& sourceId) const { return sourceSession_.capabilities(sourceId); }

QStringList SourcePage::capabilityIds(const QString& sourceId) const
{
    const QJsonObject caps = capabilities(sourceId);
    const auto flag
        = [](const QJsonObject& object, const char* key) { return object.value(QLatin1String(key)).toBool(); };
    const auto nested = [&](const char* group) { return caps.value(QLatin1String(group)).toObject(); };
    const QJsonObject browse = nested("browse");
    const QJsonObject feedback = nested("feedback");
    const QJsonObject playback = nested("playback");
    const QJsonObject controls = playback.value(QStringLiteral("controls")).toObject();
    struct Entry {
        const char* id;
        bool on;
    };
    const Entry entries[] = {
        { "playlists", flag(browse, "playlists") },
        { "likedTracks", flag(browse, "likedTracks") },
        { "radio", flag(browse, "radio") },
        { "search", flag(browse, "search") },
        { "editPlaylists", flag(browse, "editPlaylists") },
        { "like", flag(feedback, "like") },
        { "dislike", flag(feedback, "dislike") },
        { "stream", flag(playback, "providesStream") },
        { "seek", flag(controls, "seek") },
        { "download", flag(caps, "download") || flag(caps, "downloadFromStream") },
        { "settings", flag(caps, "settings") },
    };
    QStringList ids;
    if (caps.isEmpty())
        return ids;
    for (const Entry& entry : entries) {
        if (entry.on)
            ids.append(QLatin1String(entry.id));
    }
    return ids;
}

bool SourcePage::canSignOut(const QString& sourceId) const { return sourceSession_.canSignOut(sourceId); }

void SourcePage::signOut(const QString& sourceId)
{
    [](SourcePage* self, QString sourceId) -> Rpc::Task<void> {
        self->setBusy(sourceId, true);
        co_await self->sourceSession_.signOut(sourceId);
        self->setBusy(sourceId, false);
    }(this, sourceId)
                                                  .detach();
}

void SourcePage::setBusy(const QString& sourceId, bool busy)
{
    if (busy == busy_.contains(sourceId))
        return;
    if (busy)
        busy_.insert(sourceId);
    else
        busy_.remove(sourceId);
    emit busyChanged(sourceId, busy);
}

} // namespace ViewModel
