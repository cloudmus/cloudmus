#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

#include "Geo.h"

class QNetworkReply;

namespace Config {
class Settings;
}

namespace App {

QString languageForAnalytics(const QStringList& uiLanguages);

// Small, best-effort GA4 Measurement Protocol client. It accepts only
// app-defined event fields so backend data can never reach analytics.
class Analytics : public QObject {
    Q_OBJECT

public:
    explicit Analytics(Config::Settings& settings, QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);

    void configure(QString measurementId, QString apiSecret, QString appVersion,
        QUrl endpoint = QUrl(QStringLiteral("https://www.google-analytics.com/mp/collect")));
    void setEnabled(bool enabled);
    // Debug aid: also posts every request body to GA4's validation server
    // and logs what it reports. That server records nothing.
    void setValidateRequests(bool validate) { validateRequests_ = validate; }
    // Sends whatever is queued without waiting for geolocation, then calls
    // done once nothing is left or after timeoutMs, whichever comes first.
    void flush(int timeoutMs, std::function<void()> done);

    void recordLaunch();
    void recordPlayback(const QString& sourceId);
    void recordSourceOpened(const QString& sourceId);
    void recordDownload(const QString& sourceId, bool playlist, int savedCount);
    void recordPlaylistChange(const QString& sourceId, bool added);

private:
    void record(const QString& name, QJsonObject params = { });
    void scheduleSend();
    void send();
    void onGeoResolved(std::optional<GeoLocation> location);
    void validate(const QByteArray& body, quint64 requestId);
    void finishFlushIfIdle(bool timedOut = false);
    QJsonObject device() const;
    static QString sourceCategory(const QString& sourceId);

    Config::Settings& settings_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager* network_ = nullptr;
    GeoLocator geoLocator_;
    std::optional<GeoLocation> geoLocation_;
    QTimer geoWaitTimer_;
    bool geoWaitExpired_ = false;
    QTimer flushTimer_;
    std::function<void()> flushDone_;
    bool validateRequests_ = false;
    QPointer<QNetworkReply> inFlight_;
    QUrl endpoint_;
    QString measurementId_;
    QString apiSecret_;
    QString appVersion_;
    QString clientId_;
    QString sessionId_;
    QList<QJsonObject> pending_;
    bool sendScheduled_ = false;
    bool cancelledInFlight_ = false;
    quint64 nextRequestId_ = 1;
};

} // namespace App
