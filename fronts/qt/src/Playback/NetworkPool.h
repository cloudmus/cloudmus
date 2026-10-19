#pragma once

#include <QHash>
#include <QNetworkProxy>
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

} // namespace Playback
