#include "SourceSettings.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <stdexcept>
#include <tuple>

#include "ProxyRouting.h"
#include "RpcMethods.h"
#include "SourceManager.h"

namespace ViewModel {

namespace {

// Answers 204 with an empty body; small and always up: a test of the proxy,
// not of any one music service.
const char kCheckUrl[] = "https://www.gstatic.com/generate_204";
constexpr int kCheckTimeoutMs = 10000;

} // namespace

SourceSettings::SourceSettings(Rpc::SourceManager& sourceManager, Config::Settings& settings, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , settings_(settings)
{
}

SourceSettings::Facts SourceSettings::facts(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    Facts facts;
    if (client == nullptr || !client->available())
        return facts;
    const QJsonObject capabilities = client->capabilities();
    facts.running = true;
    facts.usesNetwork = client->usesNetwork();
    facts.description = client->sourceDescription();
    facts.hasRadio = capabilities.value(QStringLiteral("browse")).toObject().value(QStringLiteral("radio")).toBool();
    facts.downloadsStreams = facts.usesNetwork
        && capabilities.value(QStringLiteral("playback")).toObject().value(QStringLiteral("providesStream")).toBool();
    facts.hasSettings = capabilities.value(QStringLiteral("settings")).toBool();
    facts.needsAuth = capabilities.value(QStringLiteral("auth")).toObject().value(QStringLiteral("required")).toBool();
    return facts;
}

void SourceSettings::setEnabled(const QString& sourceId, bool enabled)
{
    QStringList disabled = settings_.disabledSources();
    disabled.removeAll(sourceId);
    if (!enabled)
        disabled.append(sourceId);
    settings_.setDisabledSources(disabled);
    sourceManager_.setEnabled(sourceId, enabled);
}

bool SourceSettings::setConnection(const QString& sourceId, const QString& connection)
{
    if (connection == settings_.sourceConnection(sourceId))
        return false;
    settings_.setSourceConnection(sourceId, connection);
    return sourceManager_.isEnabled(sourceId);
}

Rpc::Task<SettingsDescription> SourceSettings::describe(QString sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        throw std::runtime_error("the source isn't running");
    co_return co_await Rpc::settingsDescribe(*client);
}

Rpc::Task<std::optional<SourceSettings::UpdateFailure>> SourceSettings::update(
    QString sourceId, QMap<QString, QJsonValue> values)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return UpdateFailure { QString(), QStringLiteral("the source isn't running") };
    try {
        co_await Rpc::settingsUpdate(*client, UpdateParams { values });
    } catch (const Rpc::RpcCallException& e) {
        // docs/protocol.md §7.7: data.key/data.message name the field.
        const QString message = e.error().data.value(QStringLiteral("message")).toString();
        co_return UpdateFailure { e.error().data.value(QStringLiteral("key")).toString(),
            message.isEmpty() ? e.error().message : message };
    } catch (const std::exception& e) {
        co_return UpdateFailure { QString(), QString::fromStdString(e.what()) };
    }
    co_return std::nullopt;
}

bool SourceSettings::needRestart(const QList<SettingField>& fields, const QList<QString>& keys)
{
    return std::any_of(fields.cbegin(), fields.cend(),
        [&](const SettingField& field) { return keys.contains(field.key) && field.restartRequired.value_or(false); });
}

QJsonValue SourceSettings::initialValue(const SettingField& field)
{
    return field.value.isUndefined() || field.value.isNull() ? field.default_ : field.value;
}

QSet<QString> SourceSettings::saveProxies(const QList<Config::ProxyConfig>& proxies)
{
    QSet<QString> affected;
    for (const Config::ProxyConfig& old : settings_.proxies()) {
        const auto now = std::find_if(
            proxies.begin(), proxies.end(), [&old](const Config::ProxyConfig& p) { return p.id == old.id; });
        // A new name alone changes nothing on the wire.
        const auto route
            = [](const Config::ProxyConfig& p) { return std::tie(p.type, p.host, p.port, p.username, p.password); };
        if (now == proxies.end() || route(*now) != route(old))
            affected.insert(old.id);
    }
    settings_.setProxies(proxies);
    QSet<QString> sourceIds;
    for (const Rpc::BackendManifest& manifest : sourceManager_.manifests()) {
        if (affected.contains(settings_.sourceConnection(manifest.id)))
            sourceIds.insert(manifest.id);
    }
    return sourceIds;
}

void SourceSettings::checkProxy(
    const Config::ProxyConfig& proxy, QObject* context, std::function<void(const QString& error)> done)
{
    auto* network = new QNetworkAccessManager(context);
    network->setProxy(Net::networkProxy(proxy));
    QNetworkRequest request { QUrl(QString::fromLatin1(kCheckUrl)) };
    request.setTransferTimeout(kCheckTimeoutMs);
    QNetworkReply* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, network, done = std::move(done)]() {
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        network->deleteLater();
        done(error);
    });
}

} // namespace ViewModel
