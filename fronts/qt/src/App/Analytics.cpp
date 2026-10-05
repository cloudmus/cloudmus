#include "Analytics.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScreen>
#include <QSysInfo>

#include <utility>

#include "Settings.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcAnalytics, "cloudmus.app.analytics")

// Up to this many events go in one request, as one line each.
constexpr int kMaxBatch = 20;
constexpr int kMaxQueued = 50;
}

QString languageForAnalytics(const QStringList& uiLanguages)
{
    if (uiLanguages.isEmpty())
        return { };
    // GA4 takes an ISO 639-1 language with an optional region, while
    // uiLanguages() tags may carry a script too ("ru-Cyrl-RU").
    const QLocale locale(uiLanguages.first());
    if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage)
        return { };
    QString language = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
    if (language.isEmpty())
        return { }; // no two-letter code for this language
    if (locale.territory() != QLocale::AnyTerritory)
        language += QLatin1Char('-') + QLocale::territoryToCode(locale.territory());
    // Lowercase, like the web tag's navigator.language, so both land in
    // one bucket.
    return language.toLower();
}

Analytics::Analytics(Config::Settings& settings, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent)
    , settings_(settings)
    , network_(network != nullptr ? network : &ownedNetwork_)
    , clientId_(settings.analyticsClientId())
    , sessionId_(QString::number(QDateTime::currentSecsSinceEpoch()))
{
    flushTimer_.setSingleShot(true);
    connect(&flushTimer_, &QTimer::timeout, this, [this]() { finishFlushIfIdle(true); });
}

void Analytics::configure(QString measurementId, QString appVersion, QUrl endpoint)
{
    measurementId_ = std::move(measurementId);
    appVersion_ = std::move(appVersion);
    endpoint_ = std::move(endpoint);
    qCDebug(lcAnalytics) << "initialized:" << (measurementId_.isEmpty() ? "measurement ID missing" : "configured")
                         << "measurement ID" << measurementId_ << "collection enabled" << settings_.analyticsEnabled()
                         << "endpoint"
                         << endpoint_.toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)
                         << "user agent" << userAgent();
}

void Analytics::setEnabled(bool enabled)
{
    settings_.setAnalyticsEnabled(enabled);
    qCDebug(lcAnalytics) << "collection" << (enabled ? "enabled" : "disabled");
    if (enabled)
        return;
    qCDebug(lcAnalytics) << "discarding" << pending_.size() << "queued events"
                         << (inFlight_ ? "and aborting the current request" : "");
    pending_.clear();
    if (inFlight_) {
        cancelledInFlight_ = true;
        inFlight_->abort();
    }
}

void Analytics::flush(int timeoutMs, std::function<void()> done)
{
    flushDone_ = std::move(done);
    if (!inFlight_ && pending_.isEmpty()) {
        finishFlushIfIdle();
        return;
    }
    qCDebug(lcAnalytics) << "flushing" << pending_.size() << "queued events"
                         << (inFlight_ ? "after the current request" : "");
    flushTimer_.start(timeoutMs);
    send();
}

void Analytics::finishFlushIfIdle(bool timedOut)
{
    if (!flushDone_ || (!timedOut && (inFlight_ || !pending_.isEmpty())))
        return;
    if (timedOut)
        qCDebug(lcAnalytics) << "flush timed out;" << pending_.size() << "events dropped";
    flushTimer_.stop();
    std::exchange(flushDone_, nullptr)();
}

QByteArray Analytics::userAgent() const
{
    // Browser-shaped, so GA4 can tell the OS from it, but naming the app
    // rather than posing as a browser.
    QString platform;
    const QString kernel = QSysInfo::kernelType();
    if (kernel == QLatin1String("winnt"))
        platform = QStringLiteral("Windows NT %1; Win64; x64").arg(QSysInfo::kernelVersion().section(u'.', 0, 1));
    else if (kernel == QLatin1String("darwin"))
        platform = QStringLiteral("Macintosh; Intel Mac OS X %1")
                       .arg(QSysInfo::productVersion().replace(QLatin1Char('.'), QLatin1Char('_')));
    else
        platform = QStringLiteral("X11; Linux %1").arg(QSysInfo::currentCpuArchitecture());
    return QStringLiteral("Mozilla/5.0 (%1) CloudMus/%2").arg(platform, appVersion_).toUtf8();
}

QString Analytics::sourceCategory(const QString& sourceId)
{
    if (sourceId == QStringLiteral("yandex-music"))
        return QStringLiteral("yandex_music");
    if (sourceId == QStringLiteral("youtube-music"))
        return QStringLiteral("youtube_music");
    if (sourceId == QStringLiteral("local-folder"))
        return QStringLiteral("local_folder");
    return QStringLiteral("other");
}

void Analytics::recordLaunch() { recordLaunch(LaunchInfo()); }

void Analytics::recordLaunch(const LaunchInfo& info)
{
    const auto flag = [](bool on) { return on ? QStringLiteral("1") : QStringLiteral("0"); };
    QList<Param> params {
        { QStringLiteral("at_login"), flag(info.atLogin) },
        { QStringLiteral("hidden"), flag(info.hidden) },
        { QStringLiteral("glass"), flag(info.glass) },
    };
    if (!info.theme.isEmpty())
        params.append({ QStringLiteral("theme"), info.theme });
    if (!info.uiLanguage.isEmpty())
        params.append({ QStringLiteral("ui_language"), info.uiLanguage });
    record(QStringLiteral("app_launch"), params);
}

void Analytics::recordCrashes(int count)
{
    if (count > 0)
        record(QStringLiteral("app_crashed"), { { QStringLiteral("count"), QString::number(count), true } });
}

void Analytics::recordPlaybackFailure(const QString& sourceId, const QString& reason)
{
    record(QStringLiteral("playback_failed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) }, { QStringLiteral("reason"), reason } });
}

void Analytics::recordBackendFailure(const QString& sourceId, bool gaveUp)
{
    record(QStringLiteral("backend_failed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) },
            { QStringLiteral("reason"), gaveUp ? QStringLiteral("gave_up") : QStringLiteral("crashed") } });
}

void Analytics::recordSignIn(const QString& sourceId, SignInStep step)
{
    QString action;
    switch (step) {
        case SignInStep::Prompt:
            action = QStringLiteral("prompt");
            break;
        case SignInStep::Success:
            action = QStringLiteral("success");
            break;
        case SignInStep::Error:
            action = QStringLiteral("error");
            break;
    }
    record(QStringLiteral("sign_in"),
        { { QStringLiteral("source"), sourceCategory(sourceId) }, { QStringLiteral("action"), action } });
}

void Analytics::recordUpdate(const QString& action, bool manual)
{
    QList<Param> params { { QStringLiteral("action"), action } };
    if (action == QLatin1String("offered"))
        params.append({ QStringLiteral("kind"), manual ? QStringLiteral("manual") : QStringLiteral("auto") });
    record(QStringLiteral("update"), params);
}

void Analytics::recordTrackFeedback(const QString& sourceId, const QString& action)
{
    record(QStringLiteral("track_feedback"),
        { { QStringLiteral("source"), sourceCategory(sourceId) }, { QStringLiteral("action"), action } });
}

void Analytics::recordControlUsed(const QString& trigger)
{
    if (controlsUsed_.contains(trigger))
        return;
    controlsUsed_.insert(trigger);
    record(QStringLiteral("control_used"), { { QStringLiteral("trigger"), trigger } });
}

void Analytics::recordListeningTime(int minutes)
{
    if (minutes > 0)
        record(QStringLiteral("listening_time"), { { QStringLiteral("minutes"), QString::number(minutes), true } });
}

void Analytics::recordPlayback(const QString& sourceId)
{
    record(QStringLiteral("playback_started"), { { QStringLiteral("source"), sourceCategory(sourceId) } });
}

void Analytics::recordSourceOpened(const QString& sourceId)
{
    record(QStringLiteral("source_opened"), { { QStringLiteral("source"), sourceCategory(sourceId) } });
}

void Analytics::recordDownload(const QString& sourceId, bool playlist, int savedCount)
{
    record(QStringLiteral("download_completed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) },
            { QStringLiteral("kind"), playlist ? QStringLiteral("playlist") : QStringLiteral("track") },
            { QStringLiteral("saved_count"), QString::number(savedCount), true } });
}

void Analytics::recordPlaylistChange(const QString& sourceId, bool added)
{
    record(QStringLiteral("playlist_changed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) },
            { QStringLiteral("action"), added ? QStringLiteral("add") : QStringLiteral("remove") } });
}

void Analytics::recordStarPrompt(StarPromptAction action)
{
    QString value;
    switch (action) {
        case StarPromptAction::Shown:
            value = QStringLiteral("shown");
            break;
        case StarPromptAction::Star:
            value = QStringLiteral("star");
            break;
        case StarPromptAction::Dismiss:
            value = QStringLiteral("dismiss");
            break;
    }
    record(QStringLiteral("star_prompt"), { { QStringLiteral("action"), value } });
}

void Analytics::record(const QString& name, const QList<Param>& params)
{
    if (!settings_.analyticsEnabled()) {
        qCDebug(lcAnalytics) << "not sending" << name << "because collection is disabled";
        return;
    }
    if (measurementId_.isEmpty() || !endpoint_.isValid()) {
        qCDebug(lcAnalytics) << "not sending" << name << "because GA4 is not configured";
        return;
    }
    if (pending_.size() >= kMaxQueued)
        pending_.removeFirst();
    QUrlQuery event;
    event.addQueryItem(QStringLiteral("en"), name);
    event.addQueryItem(QStringLiteral("_et"), QStringLiteral("1"));
    // ep. marks a text parameter, epn. a number.
    for (const Param& param : params)
        event.addQueryItem((param.numeric ? QStringLiteral("epn.") : QStringLiteral("ep.")) + param.name, param.value);
    event.addQueryItem(QStringLiteral("ep.app_version"), appVersion_);
    pending_.append(event);
    scheduleSend();
}

void Analytics::scheduleSend()
{
    if (sendScheduled_ || inFlight_ || pending_.isEmpty())
        return;
    sendScheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        sendScheduled_ = false;
        send();
    });
}

QUrlQuery Analytics::sharedParams()
{
    // The web tag's request fields: v protocol version, tid property, cid
    // client, sid/sct this session and how many so far, seg session
    // engaged, _s hit number in the session, ul/sr language and screen,
    // dl/dt the "page", npa no ad personalization.
    if (sessionNumber_ == 0)
        sessionNumber_ = settings_.nextAnalyticsSession();
    ++hitNumber_;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("tid"), measurementId_);
    query.addQueryItem(QStringLiteral("cid"), clientId_);
    query.addQueryItem(QStringLiteral("sid"), sessionId_);
    query.addQueryItem(QStringLiteral("sct"), QString::number(sessionNumber_));
    query.addQueryItem(QStringLiteral("seg"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("_s"), QString::number(hitNumber_));
    // The first hit of a session makes GA4 log session_start, and of the
    // installation's first session also first_visit.
    if (hitNumber_ == 1) {
        query.addQueryItem(QStringLiteral("_ss"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("_nsi"), QStringLiteral("1"));
        if (sessionNumber_ == 1)
            query.addQueryItem(QStringLiteral("_fv"), QStringLiteral("1"));
    }
    const QString language = languageForAnalytics(QLocale::system().uiLanguages());
    if (!language.isEmpty())
        query.addQueryItem(QStringLiteral("ul"), language);
    // Logical pixels, as a browser reports screen.width x screen.height.
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize size = screen->size();
        if (!size.isEmpty())
            query.addQueryItem(QStringLiteral("sr"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
    }
    query.addQueryItem(QStringLiteral("dl"), QStringLiteral("https://github.com/cloudmus/cloudmus"));
    query.addQueryItem(QStringLiteral("dt"), QStringLiteral("CloudMus"));
    query.addQueryItem(QStringLiteral("npa"), QStringLiteral("1"));
    if (debugView_)
        query.addQueryItem(QStringLiteral("_dbg"), QStringLiteral("1"));
    return query;
}

void Analytics::send()
{
    if (!settings_.analyticsEnabled() || inFlight_ || pending_.isEmpty())
        return;
    QList<QUrlQuery> events;
    while (!pending_.isEmpty() && events.size() < kMaxBatch)
        events.append(pending_.takeFirst());

    // Like the web tag: a lone event goes in the URL; several go in the
    // body, one per line, with the shared fields in the URL.
    QUrlQuery query = sharedParams();
    QByteArray body;
    if (events.size() == 1) {
        for (const auto& [key, value] : events.first().queryItems(QUrl::FullyDecoded))
            query.addQueryItem(key, value);
    } else {
        QByteArrayList lines;
        for (const QUrlQuery& event : events)
            lines.append(event.query(QUrl::FullyEncoded).toUtf8());
        body = lines.join("\r\n");
    }
    QUrl url = endpoint_;
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain;charset=UTF-8"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);

    QUrlQuery logQuery = query;
    logQuery.removeQueryItem(QStringLiteral("cid"));
    logQuery.addQueryItem(QStringLiteral("cid"), QStringLiteral("<redacted>"));
    const quint64 requestId = nextRequestId_++;
    qCDebug(lcAnalytics).noquote() << "request" << requestId << "POST"
                                   << endpoint_.toDisplayString(
                                          QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)
                                   << "query" << logQuery.query(QUrl::FullyDecoded) << "body"
                                   << QString::fromUtf8(QByteArray::fromPercentEncoding(body))
                                          .replace(QStringLiteral("\r\n"), QStringLiteral(" | "));
    QNetworkReply* reply = network_->post(request, body);
    inFlight_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (cancelledInFlight_) {
            qCDebug(lcAnalytics) << "request" << requestId << "cancelled because collection was disabled";
        } else if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            qCDebug(lcAnalytics) << "request" << requestId << "delivered, HTTP" << status;
        } else {
            QString error = reply->errorString();
            error.replace(clientId_, QStringLiteral("<redacted>"));
            qCDebug(lcAnalytics) << "request" << requestId << "failed, HTTP" << status << "network error"
                                 << int(reply->error()) << error;
        }
        cancelledInFlight_ = false;
        inFlight_ = nullptr;
        reply->deleteLater();
        scheduleSend();
        finishFlushIfIdle();
    });
}

} // namespace App
