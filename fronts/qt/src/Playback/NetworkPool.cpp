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

bool isTransientError(QNetworkReply::NetworkError error)
{
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError: // what a transfer timeout reports
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::ServiceUnavailableError:
        case QNetworkReply::InternalServerError:
            return true;
        default:
            return false;
    }
}

} // namespace Playback
