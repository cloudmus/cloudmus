#include "AuthStates.h"

namespace Rpc {

void AuthStates::setPrompt(const QString& sourceId, const QJsonObject& prompt)
{
    states_[sourceId] = State { true, prompt, { } };
    emit changed(sourceId);
}

void AuthStates::setAuthenticated(const QString& sourceId)
{
    states_[sourceId] = State { };
    emit changed(sourceId);
}

void AuthStates::setError(const QString& sourceId, const QString& message)
{
    // An error supersedes any earlier prompt.
    states_[sourceId] = State { true, { }, message };
    emit changed(sourceId);
}

void AuthStates::setSignedOut(const QString& sourceId)
{
    states_[sourceId] = State { true, { }, { } };
    emit changed(sourceId);
    emit signedOut(sourceId);
}

void AuthStates::remove(const QString& sourceId)
{
    if (states_.remove(sourceId) > 0)
        emit changed(sourceId);
}

} // namespace Rpc
