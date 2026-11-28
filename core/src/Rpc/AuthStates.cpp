#include "AuthStates.h"

namespace Rpc {

namespace {

bool isComplete(const AuthMethod& method)
{
    const auto set = [](const std::optional<QString>& value) { return value && !value->isEmpty(); };
    if (method.kind == QStringLiteral("code"))
        return set(method.url) && set(method.code);
    if (method.kind == QStringLiteral("browser"))
        return set(method.url) && method.cookieUrls && !method.cookieUrls->isEmpty()
            && ((method.doneCookies && !method.doneCookies->isEmpty()) || set(method.doneUrlPrefix));
    if (method.kind == QStringLiteral("form"))
        return method.fields && !method.fields->isEmpty();
    return false;
}

} // namespace

std::optional<AuthMethod> AuthStates::State::method(const QStringList& kinds) const
{
    for (const AuthMethod& method : methods) {
        if (kinds.contains(method.kind) && isComplete(method))
            return method;
    }
    return std::nullopt;
}

void AuthStates::setPrompt(const QString& sourceId, const QList<AuthMethod>& methods)
{
    states_[sourceId] = State { true, methods, { } };
    emit changed(sourceId);
    if (!prompted_.contains(sourceId)) {
        prompted_.insert(sourceId);
        emit signInPrompted(sourceId);
    }
}

void AuthStates::setAuthenticated(const QString& sourceId)
{
    states_[sourceId] = State { };
    emit changed(sourceId);
    // Already signed in at startup isn't a sign-in.
    if (prompted_.remove(sourceId))
        emit signInCompleted(sourceId);
}

void AuthStates::setError(const QString& sourceId, const QString& message)
{
    // An error supersedes any earlier prompt.
    states_[sourceId] = State { true, { }, message };
    emit changed(sourceId);
    prompted_.remove(sourceId);
    emit signInFailed(sourceId);
}

void AuthStates::setSignedOut(const QString& sourceId)
{
    states_[sourceId] = State { true, { }, { } };
    emit changed(sourceId);
    emit signedOut(sourceId);
}

void AuthStates::remove(const QString& sourceId)
{
    prompted_.remove(sourceId);
    if (states_.remove(sourceId) > 0)
        emit changed(sourceId);
}

} // namespace Rpc
