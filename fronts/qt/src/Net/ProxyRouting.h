#pragma once

#include <QNetworkProxy>
#include <QProcessEnvironment>
#include <QString>

#include <optional>

#include "Settings.h"

namespace Net {

// How one source reaches the network, resolved from its
// Config::Settings::sourceConnection(): the same route for the backend
// process (backendEnvironment()) and for what the front itself fetches on
// the source's behalf — its streams and covers (networkProxy()). A
// stream URL a service hands out may only work from the address that
// asked for it.
struct Connection {
    enum class Mode {
        System, // whatever the environment says (HTTP_PROXY & co.), as without any setting
        Direct,
        Proxy,
    };
    Mode mode = Mode::System;
    std::optional<Config::ProxyConfig> proxy; // set iff mode == Proxy
};

// A connection naming a proxy that's no longer in the list falls back to
// System — see Ui::Settings::SourcePage, which says so.
Connection connectionFor(const Config::Settings& settings, const QString& sourceId);
Connection connectionFor(const QString& choice, const QList<Config::ProxyConfig>& proxies);

// The proxy as a URL for HTTP_PROXY & co.: http://user:pass@host:port, or
// socks5h:// — the "h" has the proxy resolve host names too, so no DNS
// query for a blocked service leaves this machine.
QString proxyUrl(const Config::ProxyConfig& proxy);

// The backend process's environment: this process's own, with the proxy
// variables set (Proxy), removed (Direct) or left alone (System). Both
// letter cases, as tools disagree on which one they read.
QProcessEnvironment backendEnvironment(const Connection& connection);

// For QNetworkAccessManager requests made for the source. System asks
// Qt's view of the system configuration for `url`.
QNetworkProxy networkProxy(const Connection& connection, const QUrl& url);
QNetworkProxy networkProxy(const Config::ProxyConfig& proxy);

} // namespace Net
