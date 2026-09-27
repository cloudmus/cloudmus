#include "GeoNetworkSignature.h"

#include <QNetworkInterface>
#include <QStringList>

#if defined(Q_OS_LINUX)
#include <QFile>
#endif

namespace App {

namespace {

QString defaultRouteSignature()
{
#if defined(Q_OS_LINUX)
    // Linux can switch its default route without changing any interface
    // address. The Qt interface snapshot below would miss that change.
    QStringList routes;
    QFile routeTable(QStringLiteral("/proc/net/route"));
    if (routeTable.open(QIODevice::ReadOnly)) {
        while (!routeTable.atEnd()) {
            const QList<QByteArray> fields = routeTable.readLine().simplified().split(' ');
            if (fields.size() > 2 && fields[1] == "00000000")
                routes.append(QString::fromLatin1(fields[0]) + QLatin1Char(':') + QString::fromLatin1(fields[2]));
        }
    }
    routes.sort();
    return routes.join(QLatin1Char('|'));
#else
    return { };
#endif
}

} // namespace

QString geoNetworkSignature()
{
    QStringList parts;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        parts.append(interface.name());
        for (const QNetworkAddressEntry& address : interface.addressEntries())
            parts.append(interface.name() + QLatin1Char(':') + address.ip().toString());
    }
    const QString route = defaultRouteSignature();
    if (!route.isEmpty())
        parts.append(route);
    parts.sort();
    return parts.join(QLatin1Char('|'));
}

} // namespace App
