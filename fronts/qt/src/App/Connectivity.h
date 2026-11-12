#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace App {

// Whether the app can reach the network, and asking again until it can. Going
// by what the sources report, since that's the network that counts, and by what
// the system says where it says anything (QNetworkInformation): that's only
// there to be quicker about it, and is overruled by a source that answers.
class Connectivity : public QObject {
    Q_OBJECT

public:
    explicit Connectivity(QObject* parent = nullptr);

    bool offline() const { return systemOffline_ || !offlineSources_.isEmpty(); }

    // Something failed that a lost network would explain: ask the source
    // again in a while.
    void suspect(const QString& sourceId);
    // The source said so itself.
    void reportOffline(const QString& sourceId);
    // The source answered.
    void reportReached(const QString& sourceId);

    // The first wait, and what it doubles up to. For a test.
    void setRetryDelays(int firstMs, int maxMs);

signals:
    void offlineChanged();
    // Time to try the source again.
    void retryWanted(const QString& sourceId);

private:
    void setSystemOffline(bool offline);
    void retry();
    void notify(bool wasOffline);

    QTimer timer_;
    QSet<QString> suspects_;
    QSet<QString> offlineSources_;
    bool systemOffline_ = false;
    int firstDelayMs_ = 15000;
    int maxDelayMs_ = 60000;
    int delayMs_ = firstDelayMs_;
};

} // namespace App
