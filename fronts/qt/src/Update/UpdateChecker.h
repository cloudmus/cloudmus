#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "ReleaseFeed.h"

class QNetworkReply;

namespace Config {
class Settings;
}

namespace Update {

// Asks GitHub's releases whether there is something newer than the running
// version. An automatic check (at startup) stays quiet unless it finds an
// update the user hasn't skipped; a manual one (the menu) reports every
// outcome, a skipped version included.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(
        Config::Settings& settings, QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);

    void configure(QString currentVersion,
        QUrl feedUrl = QUrl(QStringLiteral("https://api.github.com/repos/cloudmus/cloudmus/releases?per_page=30")),
        QString assetSuffix = platformAssetSuffix());

    QString currentVersion() const { return currentVersion_; }
    // Built between release tags (see Update::Version): not checked at
    // startup, as a developer's own build would always be "outdated".
    bool isDevBuild() const;
    QNetworkAccessManager* network() const { return network_; }

    // A check already running is not started again; its outcome then goes
    // out as manual if either asked for that.
    void check(bool manual);
    // Not offered at startup any more; a newer version will be.
    void skip(const QString& version);

signals:
    void updateAvailable(const Update::PendingUpdate& update, bool manual);
    // Manual checks only. `own`: the release this version is, or was built
    // on top of, if the feed has it.
    void upToDate(const std::optional<Update::Release>& own);
    void checkFailed(const QString& error, bool manual);

private:
    void handleReply(QNetworkReply* reply);

    Config::Settings& settings_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager* network_;
    QString currentVersion_;
    QUrl feedUrl_;
    QString assetSuffix_;
    QPointer<QNetworkReply> reply_;
    bool manual_ = false;
};

} // namespace Update
