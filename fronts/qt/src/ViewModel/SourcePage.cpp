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
    [](SourcePage* self, QString sourceId, QJsonObject fields) -> Rpc::Task<void> {
        self->setBusy(sourceId, true);
        const QString error = co_await self->sourceSession_.submitSignIn(sourceId, fields);
        if (!error.isEmpty())
            emit self->submitFailed(sourceId, error);
        self->setBusy(sourceId, false);
    }(this, sourceId, fields)
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
