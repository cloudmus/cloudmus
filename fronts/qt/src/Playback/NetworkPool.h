#pragma once

#include <QHash>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace Playback {

// The network access managers streams are fetched with, one per distinct
// proxy: QNetworkAccessManager's proxy is per manager, and keeping it keeps
// its connections pooled from one request to the next.
class NetworkPool : public QObject {
    Q_OBJECT

public:
    explicit NetworkPool(QObject* parent = nullptr);

    QNetworkAccessManager* managerFor(const QNetworkProxy& proxy);

private:
    QHash<QString, QNetworkAccessManager*> managers_;
};

// A failure worth another attempt at the same URL — the link, not the
// answer, was at fault.
bool isTransientError(QNetworkReply::NetworkError error);

} // namespace Playback
