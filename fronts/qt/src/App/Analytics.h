#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace Config {
class Settings;
}

namespace App {

// Small, best-effort GA4 Measurement Protocol client. It accepts only
// app-defined event fields so backend data can never reach analytics.
class Analytics : public QObject {
    Q_OBJECT

public:
    explicit Analytics(Config::Settings& settings, QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);

    void configure(QString measurementId, QString apiSecret, QString appVersion,
        QUrl endpoint = QUrl(QStringLiteral("https://www.google-analytics.com/mp/collect")));
    void setEnabled(bool enabled);

    void recordLaunch();
    void recordPlayback(const QString& sourceId);
    void recordSourceOpened(const QString& sourceId);
    void recordDownload(const QString& sourceId, bool playlist, int savedCount);
    void recordPlaylistChange(const QString& sourceId, bool added);

private:
    void record(const QString& name, QJsonObject params = { });
    void scheduleSend();
    void send();
    static QString sourceCategory(const QString& sourceId);

    Config::Settings& settings_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager* network_ = nullptr;
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
