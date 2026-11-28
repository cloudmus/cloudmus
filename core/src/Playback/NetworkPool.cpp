#include "NetworkPool.h"

#include <QNetworkAccessManager>

namespace Playback {

namespace {

QString proxyKey(const QNetworkProxy& proxy)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(int(proxy.type()))
        .arg(proxy.hostName())
        .arg(proxy.port())
        .arg(proxy.user(), proxy.password());
}

} // namespace

NetworkPool::NetworkPool(QObject* parent)
    : QObject(parent)
{
}

QNetworkAccessManager* NetworkPool::managerFor(const QNetworkProxy& proxy)
{
    QNetworkAccessManager*& manager = managers_[proxyKey(proxy)];
    if (manager == nullptr) {
        manager = new QNetworkAccessManager(this);
        manager->setProxy(proxy);
    }
    return manager;
}

} // namespace Playback
