#include "ProxyRouting.h"

#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QUrl>

namespace Net {

namespace {

const char* const kProxyVariables[] = { "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY" };

QString environmentVariable(const QString& name)
{
    QString value = qEnvironmentVariable(name.toLower().toLocal8Bit().constData()).trimmed();
    if (value.isEmpty())
        value = qEnvironmentVariable(name.toUpper().toLocal8Bit().constData()).trimmed();
    return value;
}

bool bypassedByNoProxy(const QString& host)
{
    if (host.isEmpty())
        return false;
    for (QString entry : environmentVariable(QStringLiteral("no_proxy")).split(u',', Qt::SkipEmptyParts)) {
        entry = entry.trimmed().toLower();
        if (entry == u"*")
            return true;
        if (entry.startsWith(u'.'))
            entry.remove(0, 1);
        if (!entry.isEmpty() && (host == entry || host.endsWith(u'.' + entry)))
            return true;
    }
    return false;
}

} // namespace

Connection connectionFor(const QString& choice, const QList<Config::ProxyConfig>& proxies)
{
    if (choice == QLatin1String(Config::Settings::kDirectConnection))
        return { Connection::Mode::Direct, std::nullopt };
    for (const Config::ProxyConfig& proxy : proxies) {
        if (proxy.id == choice)
            return { Connection::Mode::Proxy, proxy };
    }
    return { };
}

Connection connectionFor(const Config::Settings& settings, const QString& sourceId)
{
    return connectionFor(settings.sourceConnection(sourceId), settings.proxies());
}

QString proxyUrl(const Config::ProxyConfig& proxy)
{
    QUrl url;
    url.setScheme(proxy.type == Config::ProxyConfig::Type::Socks5 ? QStringLiteral("socks5h") : QStringLiteral("http"));
    url.setHost(proxy.host);
    url.setPort(proxy.port);
    // QUrl percent-encodes whatever a URL's userinfo can't carry as is
    // (":", "@", "/" in a password).
    url.setUserName(proxy.username);
    url.setPassword(proxy.password);
    return url.toString(QUrl::FullyEncoded);
}

QProcessEnvironment backendEnvironment(const Connection& connection)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (connection.mode == Connection::Mode::System)
        return env;
    for (const char* name : kProxyVariables) {
        env.remove(QLatin1String(name));
        env.remove(QLatin1String(name).toString().toLower());
    }
    if (connection.mode == Connection::Mode::Proxy) {
        const QString url = proxyUrl(*connection.proxy);
        for (const char* name : { "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY" }) {
            env.insert(QLatin1String(name), url);
            env.insert(QLatin1String(name).toString().toLower(), url);
        }
        // Never the front's own local stream relay (Playback::StreamRelay)
        // or anything else on this machine.
        const QString local = QStringLiteral("localhost,127.0.0.1,::1");
        env.insert(QStringLiteral("NO_PROXY"), local);
        env.insert(QStringLiteral("no_proxy"), local);
    }
    return env;
}

void bypassProxyForLoopback()
{
    for (const char* name : { "no_proxy", "NO_PROXY" }) {
        QByteArray value = qgetenv(name);
        for (const QByteArray host : { QByteArray("localhost"), QByteArray("127.0.0.1"), QByteArray("::1") }) {
            if (!value.split(',').contains(host))
                value += (value.isEmpty() ? "" : ",") + host;
        }
        qputenv(name, value);
    }
}

QNetworkProxy networkProxy(const Config::ProxyConfig& proxy)
{
    const auto type
        = proxy.type == Config::ProxyConfig::Type::Socks5 ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy;
    QNetworkProxy result(type, proxy.host, quint16(proxy.port), proxy.username, proxy.password);
    if (type == QNetworkProxy::Socks5Proxy)
        // Host names resolved by the proxy — socks5h, as in proxyUrl().
        result.setCapabilities(result.capabilities() | QNetworkProxy::HostNameLookupCapability);
    if (type == QNetworkProxy::HttpProxy && !proxy.username.isEmpty()) {
        // Credentials up front, as curl and requests send them. Left to
        // itself Qt sends CONNECT bare, expects a 407 and retries on the same
        // connection — and gives up with "Proxy connection closed
        // prematurely" when the proxy (seen with a real Squid) drops the
        // connection after that 407 instead of keeping it open.
        const QByteArray credentials = (proxy.username + QLatin1Char(':') + proxy.password).toUtf8();
        result.setRawHeader("Proxy-Authorization", "Basic " + credentials.toBase64());
    }
    return result;
}

QNetworkProxy networkProxy(const Connection& connection, const QUrl& url)
{
    switch (connection.mode) {
        case Connection::Mode::Direct:
            return QNetworkProxy(QNetworkProxy::NoProxy);
        case Connection::Mode::Proxy:
            return networkProxy(*connection.proxy);
        case Connection::Mode::System:
            break;
    }
    return systemProxy(url);
}

std::optional<QNetworkProxy> environmentProxy(const QUrl& url)
{
    if (bypassedByNoProxy(url.host().toLower()))
        return std::nullopt;
    const QString scheme = url.scheme().isEmpty() ? QStringLiteral("https") : url.scheme().toLower();
    QString value = environmentVariable(scheme + QStringLiteral("_proxy"));
    if (value.isEmpty())
        value = environmentVariable(QStringLiteral("all_proxy"));
    if (value.isEmpty())
        return std::nullopt;
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("http://"));

    const QUrl proxyUrl(value);
    const QString proxyScheme = proxyUrl.scheme().toLower();
    Config::ProxyConfig proxy;
    if (proxyScheme.startsWith(QStringLiteral("socks5")) || proxyScheme == u"socks")
        proxy.type = Config::ProxyConfig::Type::Socks5;
    else if (proxyScheme == u"http" || proxyScheme == u"https")
        proxy.type = Config::ProxyConfig::Type::Http;
    else
        return std::nullopt;
    proxy.host = proxyUrl.host();
    if (proxy.host.isEmpty())
        return std::nullopt;
    proxy.port = proxyUrl.port(proxy.type == Config::ProxyConfig::Type::Socks5 ? 1080 : 80);
    proxy.username = proxyUrl.userName(QUrl::FullyDecoded);
    proxy.password = proxyUrl.password(QUrl::FullyDecoded);
    return networkProxy(proxy);
}

QNetworkProxy systemProxy(const QUrl& url)
{
    if (const std::optional<QNetworkProxy> proxy = environmentProxy(url))
        return *proxy;
    const QList<QNetworkProxy> system = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(url));
    return system.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : system.constFirst();
}

} // namespace Net
