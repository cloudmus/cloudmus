#include "SettingsScreen.h"

#include <QLocale>
#include <QUrl>
#include <QUuid>

#include <algorithm>

#include "Analytics.h"
#include "AuthStates.h"
#include "Core.h"
#include "Preload.h"
#include "SourceManager.h"
#include "SourceSettings.h"

namespace ViewModel {

namespace {

const QString kSocks5 = QStringLiteral("socks5");

Config::ProxyConfig::Type proxyType(const QString& kind)
{
    return kind == kSocks5 ? Config::ProxyConfig::Type::Socks5 : Config::ProxyConfig::Type::Http;
}

} // namespace

SettingsScreen::SettingsScreen(App::Core& core, SourceSettings& sourceSettings, QObject* parent)
    : QObject(parent)
    , core_(core)
    , sourceSettings_(sourceSettings)
    , sourceRows_({ "sourceId", "name", "iconUrl", "status" })
    , proxyRows_({ "proxyId", "name", "title", "kind", "host", "port", "username", "password" })
{
    Preload& preload = core_.preload();
    connect(&preload, &Preload::settingsChanged, this, [this]() {
        emit playlistTracksChanged();
        emit radioTracksChanged();
        emit cacheMaxMbChanged();
        emit reserveMbChanged();
        emit cacheUsedTextChanged();
    });
    connect(&preload, &Preload::cacheUsedChanged, this, &SettingsScreen::cacheUsedTextChanged);
    connect(&preload, &Preload::diskFreeChanged, this, &SettingsScreen::diskFreeTextChanged);

    Rpc::SourceManager& manager = core_.sourceManager();
    connect(&manager, &Rpc::SourceManager::sourceStarting, this, [this]() { rebuildSources(); });
    connect(&manager, &Rpc::SourceManager::sourceStopped, this, [this]() { rebuildSources(); });
    connect(&manager, &Rpc::SourceManager::sourceReady, this, [this]() { rebuildSources(); });
    connect(&manager, &Rpc::SourceManager::mediaPermissionNeededChanged, this,
        &SettingsScreen::mediaPermissionNeededChanged);
    connect(&manager, &Rpc::SourceManager::mediaRemovalNeedsConsent, this, &SettingsScreen::mediaDeleteConsentNeeded);
    connect(&manager, &Rpc::SourceManager::sourceUnavailable, this, [this]() { rebuildSources(); });
    connect(&core_.authStates(), &Rpc::AuthStates::changed, this, [this]() { rebuildSources(); });
    // Texts the core builds itself are in the new language from the next
    // rebuild; these are the ones on the Settings page.
    connect(&core_.translator(), &I18n::Translator::languageChanged, this, [this]() {
        emit cacheUsedTextChanged();
        emit diskFreeTextChanged();
        rebuildSources();
    });
    rebuildSources();
    rebuildProxies();
}

int SettingsScreen::playlistTracks() const { return core_.preload().playlistTracks(); }

int SettingsScreen::radioTracks() const { return core_.preload().radioTracks(); }

int SettingsScreen::cacheMaxMb() const { return core_.preload().cacheMaxMb(); }

int SettingsScreen::reserveMb() const { return core_.preload().reserveMb(); }

QString SettingsScreen::diskFreeText() const
{
    const qint64 free = core_.preload().diskFreeBytes();
    return free < 0 ? QString() : QLocale().formattedDataSize(free);
}

QString SettingsScreen::cacheUsedText() const
{
    // The default locale follows the language (I18n::Translator).
    return QLocale().formattedDataSize(core_.preload().cacheUsedBytes());
}

double SettingsScreen::cacheUsedFraction() const { return core_.preload().cacheUsedFraction(); }

bool SettingsScreen::analytics() const { return core_.settings().analyticsEnabled(); }

void SettingsScreen::setPlaylistTracks(int tracks) { core_.preload().setPlaylistTracks(tracks); }

void SettingsScreen::setRadioTracks(int tracks) { core_.preload().setRadioTracks(tracks); }

void SettingsScreen::setCacheMaxMb(int megabytes) { core_.preload().setCacheMaxMb(megabytes); }

void SettingsScreen::setReserveMb(int megabytes) { core_.preload().setReserveMb(megabytes); }

void SettingsScreen::clearCache() { core_.preload().clearCache(); }

void SettingsScreen::setAnalytics(bool on)
{
    if (on == analytics())
        return;
    core_.analytics().setEnabled(on);
    emit analyticsChanged();
}

bool SettingsScreen::mediaPermissionNeeded() const
{
    const Rpc::HostMedia* media = core_.sourceManager().hostMedia();
    return media != nullptr && media->permissionNeeded();
}

QString SettingsScreen::language() const { return core_.settings().language(); }

void SettingsScreen::setLanguage(const QString& code)
{
    if (code == language())
        return;
    core_.settings().setLanguage(code);
    core_.translator().setLanguage(code);
    emit languageChanged();
}

void SettingsScreen::setSystemLanguages(const QStringList& uiLanguages)
{
    core_.translator().setSystemLanguages(uiLanguages);
}

void SettingsScreen::saveProxy(const QString& proxyId, const QString& name, const QString& kind, const QString& host,
    int port, const QString& username, const QString& password)
{
    Config::ProxyConfig proxy { proxyId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : proxyId,
        name.trimmed(), proxyType(kind), host.trimmed(), port, username, password };
    QList<Config::ProxyConfig> proxies = core_.settings().proxies();
    const auto it = std::find_if(
        proxies.begin(), proxies.end(), [&](const Config::ProxyConfig& other) { return other.id == proxy.id; });
    if (it != proxies.end())
        *it = proxy;
    else
        proxies.append(proxy);
    for (const QString& sourceId : sourceSettings_.saveProxies(proxies))
        core_.sourceManager().restart(sourceId);
    rebuildProxies();
}

void SettingsScreen::removeProxy(const QString& proxyId)
{
    QList<Config::ProxyConfig> proxies = core_.settings().proxies();
    proxies.removeIf([&](const Config::ProxyConfig& proxy) { return proxy.id == proxyId; });
    for (const QString& sourceId : sourceSettings_.saveProxies(proxies))
        core_.sourceManager().restart(sourceId);
    rebuildProxies();
}

void SettingsScreen::checkProxy(
    const QString& kind, const QString& host, int port, const QString& username, const QString& password)
{
    const int check = ++proxyCheck_;
    setProxyCheck(true, false, QString());
    SourceSettings::checkProxy({ QString(), QString(), proxyType(kind), host.trimmed(), port, username, password }, this,
        [this, check](const QString& error) {
            if (check == proxyCheck_)
                setProxyCheck(false, error.isEmpty(), error.isEmpty() ? tr("It works") : error);
        });
}

void SettingsScreen::forgetProxyCheck()
{
    ++proxyCheck_;
    setProxyCheck(false, false, QString());
}

void SettingsScreen::setProxyCheck(bool checking, bool ok, const QString& text)
{
    proxyChecking_ = checking;
    proxyCheckOk_ = ok;
    proxyCheckText_ = text;
    emit proxyCheckingChanged();
    emit proxyCheckOkChanged();
    emit proxyCheckTextChanged();
}

void SettingsScreen::rebuildSources()
{
    Rpc::SourceManager& manager = core_.sourceManager();
    QList<QVariantHash> rows;
    for (const Rpc::BackendManifest& manifest : manager.manifests()) {
        QString status;
        if (!manager.isEnabled(manifest.id))
            status = tr("Off");
        else if (manager.isUnavailable(manifest.id))
            status = tr("Couldn't be started");
        else if (core_.authStates().state(manifest.id).hasProblem)
            status = tr("Not signed in");
        rows.append({
            { QStringLiteral("sourceId"), manifest.id },
            { QStringLiteral("name"), manager.displayName(manifest) },
            { QStringLiteral("iconUrl"),
                manifest.iconPath.isEmpty() ? QString() : QUrl::fromLocalFile(manifest.iconPath).toString() },
            { QStringLiteral("status"), status },
        });
    }
    sourceRows_.setRows(rows);
}

void SettingsScreen::rebuildProxies()
{
    QList<QVariantHash> rows;
    for (const Config::ProxyConfig& proxy : core_.settings().proxies()) {
        rows.append({
            { QStringLiteral("proxyId"), proxy.id },
            { QStringLiteral("name"), proxy.name },
            { QStringLiteral("title"), proxy.name.isEmpty() ? proxy.host : proxy.name },
            { QStringLiteral("kind"), proxy.type == Config::ProxyConfig::Type::Socks5 ? kSocks5 : QStringLiteral("http") },
            { QStringLiteral("host"), proxy.host },
            { QStringLiteral("port"), proxy.port },
            { QStringLiteral("username"), proxy.username },
            { QStringLiteral("password"), proxy.password },
        });
    }
    proxyRows_.setRows(rows);
}

} // namespace ViewModel
