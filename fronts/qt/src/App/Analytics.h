#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class QNetworkReply;

namespace Config {
class Settings;
}

namespace App {

// The preferred UI language as GA4's web tag sends it ("ru-ru"), or empty.
QString languageForAnalytics(const QStringList& uiLanguages);

// Small, best-effort GA4 client. It accepts only app-defined event fields
// so backend data can never reach analytics.
//
// It speaks the web tag's collection endpoint (/g/collect), not the
// Measurement Protocol: GA4 derives country and city from the request's
// address only there, while the Measurement Protocol leaves an app's users
// without a location unless it looks one up and sends it itself.
class Analytics : public QObject {
    Q_OBJECT

public:
    explicit Analytics(Config::Settings& settings, QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);

    void configure(QString measurementId, QString appVersion,
        QUrl endpoint = QUrl(QStringLiteral("https://www.google-analytics.com/g/collect")));
    void setEnabled(bool enabled);
    // Marks events for GA4's DebugView (Admin → DebugView), to check them
    // live without digging through the reports.
    void setDebugView(bool debugView) { debugView_ = debugView; }
    // Sends whatever is queued, then calls done once nothing is left or
    // after timeoutMs, whichever comes first.
    void flush(int timeoutMs, std::function<void()> done);

    // How the app was started and set up, sent with app_launch.
    struct LaunchInfo {
        bool atLogin = false; // started by autostart
        bool hidden = false; // ...straight into the tray
        QString theme; // "system", "light", "dark"
        bool glass = false; // the translucent window in effect
        QString uiLanguage; // the language picked in Settings, or "system"
    };
    void recordLaunch();
    void recordLaunch(const LaunchInfo& info);
    // Crash reports the previous runs left (Diagnostics::CrashReporter).
    void recordCrashes(int count);
    // `reason`: see Playback::PlaybackController::failed().
    void recordPlaybackFailure(const QString& sourceId, const QString& reason);
    // A backend went down by itself; `gaveUp` once no restarts are left.
    void recordBackendFailure(const QString& sourceId, bool gaveUp);
    enum class SignInStep {
        Prompt,
        Success,
        Error
    };
    void recordSignIn(const QString& sourceId, SignInStep step);
    // `action`: "offered", "install", "install_launched", "failed",
    // "release_page"; `manual` tells a check the user asked for.
    void recordUpdate(const QString& action, bool manual = false);
    // `action`: "like", "unlike", "dislike", "undislike".
    void recordTrackFeedback(const QString& sourceId, const QString& action);
    // Where the player was controlled from — "global_hotkey",
    // "window_shortcut", "tray", "media_controls", "taskbar"; sent once a
    // run per place, to count who uses it, not how often.
    void recordControlUsed(const QString& trigger);
    void recordListeningTime(int minutes);
    void recordPlayback(const QString& sourceId);
    void recordSourceOpened(const QString& sourceId);
    void recordDownload(const QString& sourceId, bool playlist, int savedCount);
    void recordPlaylistChange(const QString& sourceId, bool added);
    enum class StarPromptAction {
        Shown,
        Star,
        Dismiss
    };
    void recordStarPrompt(StarPromptAction action);

private:
    struct Param {
        QString name;
        QString value;
        bool numeric = false;
    };

    void record(const QString& name, const QList<Param>& params = { });
    void scheduleSend();
    void send();
    void finishFlushIfIdle(bool timedOut = false);
    QUrlQuery sharedParams();
    QByteArray userAgent() const;
    static QString sourceCategory(const QString& sourceId);

    Config::Settings& settings_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager* network_ = nullptr;
    QPointer<QNetworkReply> inFlight_;
    QUrl endpoint_;
    QString measurementId_;
    QString appVersion_;
    QString clientId_;
    QString sessionId_;
    int sessionNumber_ = 0;
    int hitNumber_ = 0;
    QList<QUrlQuery> pending_;
    QSet<QString> controlsUsed_;
    QTimer flushTimer_;
    std::function<void()> flushDone_;
    bool debugView_ = false;
    bool sendScheduled_ = false;
    bool cancelledInFlight_ = false;
    quint64 nextRequestId_ = 1;
};

} // namespace App
