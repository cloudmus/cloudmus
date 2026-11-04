#pragma once

#include <QObject>
#include <QString>

#include "RowsModel.h"

namespace App {
class Core;
}

namespace ViewModel {

class SourceSettings;

// The Settings screen, what's on the page itself: preloading, the sources to
// pick from, the proxies, statistics. Applied at once, there being no dialog to
// say OK in. Slots, plain types and a signal per property: on Android this is a
// replica. What these settings do is the shared view models'.
class SettingsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(int playlistTracks READ playlistTracks NOTIFY playlistTracksChanged)
    Q_PROPERTY(int radioTracks READ radioTracks NOTIFY radioTracksChanged)
    Q_PROPERTY(int cacheMaxMb READ cacheMaxMb NOTIFY cacheMaxMbChanged)
    Q_PROPERTY(QString cacheUsedText READ cacheUsedText NOTIFY cacheUsedTextChanged)
    Q_PROPERTY(bool analytics READ analytics NOTIFY analyticsChanged)
    // How the last check of a proxy went.
    Q_PROPERTY(bool proxyChecking READ proxyChecking NOTIFY proxyCheckingChanged)
    Q_PROPERTY(bool proxyCheckOk READ proxyCheckOk NOTIFY proxyCheckOkChanged)
    Q_PROPERTY(QString proxyCheckText READ proxyCheckText NOTIFY proxyCheckTextChanged)

public:
    SettingsScreen(App::Core& core, SourceSettings& sourceSettings, QObject* parent = nullptr);

    RowsModel& sourceRows() { return sourceRows_; }
    RowsModel& proxyRows() { return proxyRows_; }

    int playlistTracks() const;
    int radioTracks() const;
    int cacheMaxMb() const;
    QString cacheUsedText() const;
    bool analytics() const;
    bool proxyChecking() const { return proxyChecking_; }
    bool proxyCheckOk() const { return proxyCheckOk_; }
    QString proxyCheckText() const { return proxyCheckText_; }

public slots:
    void setPlaylistTracks(int tracks);
    void setRadioTracks(int tracks);
    void setCacheMaxMb(int megabytes);
    void clearCache();
    void setAnalytics(bool on);
    // `proxyId` empty: a new one. `kind`: "http" or "socks5".
    void saveProxy(const QString& proxyId, const QString& name, const QString& kind, const QString& host, int port,
        const QString& username, const QString& password);
    void removeProxy(const QString& proxyId);
    void checkProxy(const QString& kind, const QString& host, int port, const QString& username, const QString& password);
    void forgetProxyCheck();

signals:
    void playlistTracksChanged();
    void radioTracksChanged();
    void cacheMaxMbChanged();
    void cacheUsedTextChanged();
    void analyticsChanged();
    void proxyCheckingChanged();
    void proxyCheckOkChanged();
    void proxyCheckTextChanged();

private:
    void rebuildSources();
    void rebuildProxies();
    void setProxyCheck(bool checking, bool ok, const QString& text);

    App::Core& core_;
    SourceSettings& sourceSettings_;
    RowsModel sourceRows_;
    RowsModel proxyRows_;
    bool proxyChecking_ = false;
    bool proxyCheckOk_ = false;
    QString proxyCheckText_;
    // Which check is the last one: an earlier answer says nothing any more.
    int proxyCheck_ = 0;
};

} // namespace ViewModel
