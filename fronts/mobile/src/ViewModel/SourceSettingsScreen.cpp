#include "SourceSettingsScreen.h"

#include <QJsonValue>

#include <algorithm>

#include "Core.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourcePage.h"
#include "SourceSettings.h"

namespace ViewModel {

SourceSettingsScreen::SourceSettingsScreen(App::Core& core, SourceSettings& sourceSettings, QObject* parent)
    : QObject(parent)
    , core_(core)
    , sourceSettings_(sourceSettings)
    , connectionRows_({ "connectionId", "title", "isCurrent" })
    , fieldRows_({ "key", "groupTitle", "groupDescription", "isFirst", "isLast", "label", "description", "type", "value", "isSet", "minimum",
          "maximum", "options", "placeholder", "error" })
{
    Rpc::SourceManager& manager = core_.sourceManager();
    const auto refreshIfOpen = [this](const QString& sourceId) {
        if (sourceId == sourceId_)
            refresh();
    };
    connect(&manager, &Rpc::SourceManager::sourceStarting, this,
        [refreshIfOpen](const Rpc::BackendManifest& manifest) { refreshIfOpen(manifest.id); });
    connect(&manager, &Rpc::SourceManager::sourceReady, this,
        [refreshIfOpen](Rpc::RpcClient* client) { refreshIfOpen(client->sourceId()); });
    connect(&manager, &Rpc::SourceManager::sourceStopped, this, refreshIfOpen);
    connect(&manager, &Rpc::SourceManager::sourceUnavailable, this,
        [refreshIfOpen](const QString& sourceId, const QString&, const QStringList&) { refreshIfOpen(sourceId); });
}

QString SourceSettingsScreen::name() const
{
    for (const Rpc::BackendManifest& manifest : core_.sourceManager().manifests()) {
        if (manifest.id == sourceId_)
            return manifest.name;
    }
    return { };
}

QString SourceSettingsScreen::description() const { return sourceSettings_.facts(sourceId_).description; }

QString SourceSettingsScreen::status() const
{
    Rpc::SourceManager& manager = core_.sourceManager();
    if (sourceId_.isEmpty() || sourceSettings_.facts(sourceId_).running)
        return { };
    if (!manager.isEnabled(sourceId_))
        return tr("A switched-off source doesn't start and isn't listed.");
    return manager.isUnavailable(sourceId_) ? tr("The source couldn't be started.") : tr("Starting…");
}

bool SourceSettingsScreen::enabled() const { return core_.sourceManager().isEnabled(sourceId_); }

bool SourceSettingsScreen::usesNetwork() const { return sourceSettings_.facts(sourceId_).usesNetwork; }

QString SourceSettingsScreen::connection() const { return core_.settings().sourceConnection(sourceId_); }

QString SourceSettingsScreen::connectionHint() const
{
    const QString current = connection();
    if (current == QLatin1String(Config::Settings::kDirectConnection))
        return { };
    const QList<Config::ProxyConfig> proxies = core_.settings().proxies();
    const bool isProxy = std::any_of(
        proxies.cbegin(), proxies.cend(), [&](const Config::ProxyConfig& proxy) { return proxy.id == current; });
    if (isProxy)
        return { };
    return current == QLatin1String(Config::Settings::kSystemConnection)
        ? tr("Uses the system's proxy settings, if any.")
        : tr("Its proxy was removed: it uses the system connection.");
}

bool SourceSettingsScreen::hasRadio() const { return sourceSettings_.facts(sourceId_).hasRadio; }

int SourceSettingsScreen::radioRepeatGap() const { return core_.settings().sourceRadioRepeatGap(sourceId_); }

int SourceSettingsScreen::radioRepeatDays() const { return core_.settings().sourceRadioRepeatDays(sourceId_); }

bool SourceSettingsScreen::downloadsStreams() const { return sourceSettings_.facts(sourceId_).downloadsStreams; }

int SourceSettingsScreen::downloadConnections() const { return core_.settings().sourceDownloadConnections(sourceId_); }

void SourceSettingsScreen::open(const QString& sourceId)
{
    sourceId_ = sourceId;
    fieldsClient_ = nullptr;
    fields_ = { };
    fieldErrors_.clear();
    fieldsError_.clear();
    core_.sourcePage().wake(sourceId);
    refresh();
}

void SourceSettingsScreen::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    sourceSettings_.setEnabled(sourceId_, enabled);
    refresh();
}

void SourceSettingsScreen::setConnection(const QString& connection)
{
    if (sourceSettings_.setConnection(sourceId_, connection))
        core_.sourceManager().restart(sourceId_);
    refresh();
}

void SourceSettingsScreen::setRadioRepeatGap(int tracks)
{
    core_.settings().setSourceRadioRepeatGap(
        sourceId_, qBound(0, tracks, Config::Settings::kMaxRadioRepeatGap));
    emit radioRepeatGapChanged();
}

void SourceSettingsScreen::setRadioRepeatDays(int days)
{
    core_.settings().setSourceRadioRepeatDays(sourceId_, qBound(0, days, Config::Settings::kMaxRadioRepeatDays));
    emit radioRepeatDaysChanged();
}

void SourceSettingsScreen::setDownloadConnections(int connections)
{
    core_.settings().setSourceDownloadConnections(
        sourceId_, qBound(0, connections, Config::Settings::kMaxDownloadConnections));
    emit downloadConnectionsChanged();
}

void SourceSettingsScreen::setFieldText(const QString& key, const QString& value) { setField(key, value); }

void SourceSettingsScreen::setFieldNumber(const QString& key, int value) { setField(key, value); }

void SourceSettingsScreen::setFieldBool(const QString& key, bool value) { setField(key, value); }

void SourceSettingsScreen::setField(const QString& key, QJsonValue json)
{
    const auto field = std::find_if(
        fields_.fields.begin(), fields_.fields.end(), [&](const SettingField& other) { return other.key == key; });
    if (field == fields_.fields.end())
        return;
    const bool secret = field->type == QStringLiteral("secret");
    if (json.isString() && !secret)
        json = json.toString().trimmed();
    // A secret isn't shown or sent back unless replaced: the source keeps it.
    const bool same = secret ? json.toString().isEmpty() : json == SourceSettings::initialValue(*field);
    if (same) {
        if (fieldErrors_.remove(key) > 0)
            rebuildFields();
        return;
    }
    updateField(sourceId_, key, json).detach();
}

Rpc::Task<void> SourceSettingsScreen::updateField(QString sourceId, QString key, QJsonValue value)
{
    const std::optional<SourceSettings::UpdateFailure> failure
        = co_await sourceSettings_.update(sourceId, { { key, value } });
    if (sourceId != sourceId_)
        co_return;
    if (failure) {
        fieldErrors_.insert(key, failure->message);
        rebuildFields();
        co_return;
    }
    fieldErrors_.remove(key);
    for (SettingField& field : fields_.fields) {
        if (field.key != key)
            continue;
        if (field.type == QStringLiteral("secret"))
            field.isSet = !value.toString().isEmpty();
        else
            field.value = value;
    }
    rebuildFields();
    if (SourceSettings::needRestart(fields_.fields, { key }))
        core_.sourceManager().restart(sourceId);
    else
        core_.sourceManager().notifySettingsChanged(sourceId);
}

void SourceSettingsScreen::refresh()
{
    rebuildConnections();
    const SourceSettings::Facts facts = sourceSettings_.facts(sourceId_);
    const Rpc::RpcClient* client = core_.sourceManager().client(sourceId_);
    if (!facts.hasSettings) {
        fieldsClient_ = nullptr;
        fields_ = { };
        rebuildFields();
    } else if (fieldsClient_ != client) {
        fieldsClient_ = client;
        loadFields(sourceId_).detach();
    }
    emit sourceIdChanged();
    emit nameChanged();
    emit descriptionChanged();
    emit statusChanged();
    emit enabledChanged();
    emit usesNetworkChanged();
    emit connectionHintChanged();
    emit hasRadioChanged();
    emit radioRepeatGapChanged();
    emit radioRepeatDaysChanged();
    emit downloadsStreamsChanged();
    emit downloadConnectionsChanged();
}

Rpc::Task<void> SourceSettingsScreen::loadFields(QString sourceId)
{
    const void* client = fieldsClient_;
    fieldsLoading_ = true;
    fieldsError_.clear();
    emit fieldsLoadingChanged();
    emit fieldsErrorChanged();
    std::optional<SettingsDescription> description;
    QString failure;
    try {
        description = co_await sourceSettings_.describe(sourceId);
    } catch (const std::exception& e) {
        failure = QString::fromStdString(e.what());
    }
    // Another source opened since, or this one restarted.
    if (sourceId != sourceId_ || client != fieldsClient_)
        co_return;
    fieldsLoading_ = false;
    fieldsError_ = failure;
    fields_ = description.value_or(SettingsDescription { });
    emit fieldsLoadingChanged();
    emit fieldsErrorChanged();
    rebuildFields();
}

void SourceSettingsScreen::rebuildConnections()
{
    const QString current = connection();
    const QList<Config::ProxyConfig> proxies = core_.settings().proxies();
    const auto isProxy = [&](const QString& id) {
        return std::any_of(
            proxies.cbegin(), proxies.cend(), [&](const Config::ProxyConfig& proxy) { return proxy.id == id; });
    };
    const QString system = QLatin1String(Config::Settings::kSystemConnection);
    const QString direct = QLatin1String(Config::Settings::kDirectConnection);
    const QString shown = current == direct || isProxy(current) ? current : system; // a removed proxy falls back
    const auto row = [&](const QString& id, const QString& title) {
        return QVariantHash { { QStringLiteral("connectionId"), id }, { QStringLiteral("title"), title },
            { QStringLiteral("isCurrent"), id == shown } };
    };
    QList<QVariantHash> rows { row(system, tr("System")), row(direct, tr("Direct")) };
    for (const Config::ProxyConfig& proxy : proxies)
        rows.append(row(proxy.id, proxy.name.isEmpty() ? proxy.host : proxy.name));
    connectionRows_.setRows(rows);
}

void SourceSettingsScreen::rebuildFields()
{
    QList<QVariantHash> rows;
    // The untitled group first, then the titled ones in the source's order.
    QList<std::optional<SettingsGroup>> groups { std::nullopt };
    for (const SettingsGroup& group : fields_.groups)
        groups.append(group);
    for (const std::optional<SettingsGroup>& group : groups) {
        const QString groupId = group ? group->id : QString();
        QList<SettingField> fields;
        for (const SettingField& field : fields_.fields) {
            if (field.group.value_or(QString()) == groupId)
                fields.append(field);
        }
        for (qsizetype i = 0; i < fields.size(); ++i) {
            const SettingField& field = fields.at(i);
            const bool first = i == 0;
            QVariantList options;
            for (const SettingOption& option : field.options.value_or(QList<SettingOption>())) {
                options.append(QVariantMap {
                    { QStringLiteral("value"), option.value }, { QStringLiteral("label"), option.label } });
            }
            rows.append({
                { QStringLiteral("key"), field.key },
                // On the group's first field: where its heading goes.
                { QStringLiteral("groupTitle"), first && group ? group->title : QString() },
                { QStringLiteral("groupDescription"),
                    first && group ? group->description.value_or(QString()) : QString() },
                { QStringLiteral("isFirst"), first },
                { QStringLiteral("isLast"), i == fields.size() - 1 },
                { QStringLiteral("label"), field.label },
                { QStringLiteral("description"), field.description.value_or(QString()) },
                { QStringLiteral("type"), field.type },
                { QStringLiteral("value"), SourceSettings::initialValue(field).toVariant() },
                { QStringLiteral("isSet"), field.isSet.value_or(false) },
                { QStringLiteral("minimum"), field.min.value_or(0) },
                { QStringLiteral("maximum"), field.max.value_or(1000000) },
                { QStringLiteral("options"), options },
                { QStringLiteral("placeholder"), field.placeholder.value_or(QString()) },
                { QStringLiteral("error"), fieldErrors_.value(field.key) },
            });
        }
    }
    fieldRows_.setRows(rows);
}

} // namespace ViewModel
