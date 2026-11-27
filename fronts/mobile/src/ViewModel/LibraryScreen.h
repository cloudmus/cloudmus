#pragma once

#include <QJsonValue>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

#include "Coro.h"
#include "Models.h"
#include "RowsModel.h"

namespace Config {
class Settings;
}
namespace Covers {
class CoverArtCache;
}
namespace Rpc {
class AuthStates;
class SourceManager;
}

namespace ViewModel {

class ActivePlaylist;
class NowPlaying;
class SourcePage;
class Sources;

// The Library screen: the sources in a flat list, the one picked, its
// favorites as tiles and its other playlists as a list. The desktop shows
// these as a tree with a sheet over the window, so the shape is this front's;
// what is listed, what's a favorite and how a playlist starts are the shared
// view models'. Slots and plain types: on Android this is a replica.
class LibraryScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(QString currentSourceName READ currentSourceName NOTIFY currentSourceNameChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // The active playlist's source, for the button that opens this screen.
    Q_PROPERTY(QString activeSourceName READ activeSourceName NOTIFY activeSourceNameChanged)
    Q_PROPERTY(QString activeSourceIcon READ activeSourceIcon NOTIFY activeSourceIconChanged)
    // In full color, for where it stands out; the icons only for lists of them.
    Q_PROPERTY(QString activeSourceLogo READ activeSourceLogo NOTIFY activeSourceLogoChanged)
    Q_PROPERTY(QString currentSourceIcon READ currentSourceIcon NOTIFY currentSourceIconChanged)
    Q_PROPERTY(QString currentSourceLogo READ currentSourceLogo NOTIFY currentSourceLogoChanged)
    // Signing in to the source picked: needed at all, then what it asks for,
    // by the methods it offers (docs/protocol.md §10).
    Q_PROPERTY(bool authNeeded READ authNeeded NOTIFY authNeededChanged)
    // Their kinds ("code", "browser", "form"), the source's preferred first.
    Q_PROPERTY(QStringList authKinds READ authKinds NOTIFY authKindsChanged)
    // `code`: the page and the code to enter there.
    Q_PROPERTY(QString authUrl READ authUrl NOTIFY authUrlChanged)
    Q_PROPERTY(QString authCode READ authCode NOTIFY authCodeChanged)
    // `browser`: the method as JSON, for the platform's built-in browser, with
    // how the source connects as "connection" ("mode": "system", "direct" or
    // "proxy", the latter with the proxy): the browser goes the same way.
    Q_PROPERTY(QString authBrowser READ authBrowser NOTIFY authBrowserChanged)
    // The source connects through a proxy from the user's list.
    Q_PROPERTY(bool authBrowserNeedsProxy READ authBrowserNeedsProxy NOTIFY authBrowserNeedsProxyChanged)
    // `form`: its fields (name, label, secret, multiline) and what the source
    // says about them, as rich text.
    Q_PROPERTY(QVariantList authFields READ authFields NOTIFY authFieldsChanged)
    Q_PROPERTY(QString authFormMessage READ authFormMessage NOTIFY authFormMessageChanged)
    // A submission, or asking to sign in, is under way.
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authBusyChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authErrorChanged)
    Q_PROPERTY(bool canSignOut READ canSignOut NOTIFY canSignOutChanged)
    // What the source picked says it can do, as ids the page words itself
    // ("radio", "download", ...); empty while it isn't running.
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY capabilitiesChanged)

public:
    LibraryScreen(Sources& sources, ActivePlaylist& activePlaylist, NowPlaying& nowPlaying, Rpc::AuthStates& authStates,
        SourcePage& sourcePage, Rpc::SourceManager& sourceManager, Covers::CoverArtCache& covers,
        const Config::Settings& settings, QObject* parent = nullptr);

    RowsModel& sourceRows() { return sourceRows_; }
    RowsModel& favoriteRows() { return favoriteRows_; }
    RowsModel& otherRows() { return otherRows_; }

    QString currentSourceId() const { return currentSourceId_; }
    QString currentSourceName() const;
    bool loading() const;
    QString activeSourceName() const;
    QString activeSourceIcon() const;
    QString activeSourceLogo() const;
    QString currentSourceIcon() const;
    QString currentSourceLogo() const;
    bool authNeeded() const;
    QStringList authKinds() const;
    QString authUrl() const;
    QString authCode() const;
    QString authBrowser() const;
    bool authBrowserNeedsProxy() const;
    QVariantList authFields() const;
    QString authFormMessage() const;
    bool authBusy() const;
    QString authError() const;
    bool canSignOut() const;
    QStringList capabilities() const;
    // playPlaylist() for any source: for what doesn't go through picking one.
    void playPlaylistOf(const QString& sourceId, const QString& playlistId);

public slots:
    void selectSource(const QString& sourceId);
    // The active playlist's source, if there is one: where the Library opens
    // from the main screen.
    void selectActiveSource();
    // Starts the current source's playlist and, once it runs, activated().
    // The active one isn't started over: it only plays on if paused.
    void playPlaylist(const QString& playlistId);
    void toggleFavorite(const QString& playlistId);
    // The current source's playlist asked a question before it's opened
    // (Playlist.offer, a row's offerQuestion): the user's answer, which sets
    // the source's settings it names. Nothing is opened: the list changes.
    void answerOffer(const QString& playlistId, bool accept);
    void refresh();
    void signIn();
    // The sign-in form's values, by field name.
    void submitSignInForm(const QVariantMap& values);
    // What the built-in browser ended up with, as JSON (auth.submit's
    // `browser`). By source: another may be picked by then.
    void submitBrowserSignIn(const QString& sourceId, const QString& result);
    void signOut();

signals:
    void currentSourceIdChanged();
    void currentSourceNameChanged();
    void loadingChanged();
    void activeSourceNameChanged();
    void activeSourceIconChanged();
    void activeSourceLogoChanged();
    void currentSourceIconChanged();
    void currentSourceLogoChanged();
    void authNeededChanged();
    void authKindsChanged();
    void authUrlChanged();
    void authCodeChanged();
    void authBrowserChanged();
    void authBrowserNeedsProxyChanged();
    void authFieldsChanged();
    void authFormMessageChanged();
    void authBusyChanged();
    void authErrorChanged();
    void canSignOutChanged();
    void capabilitiesChanged();
    // Something started playing from here: time for the main screen.
    void activated();

private:
    void rebuild();
    // The source picked's sign-in method of `kind`, if it offers one.
    std::optional<AuthMethod> authMethod(const QString& kind) const;
    QVariantHash playlistRow(const Playlist& playlist);
    Rpc::Task<void> start(QString sourceId, Playlist playlist);
    Rpc::Task<void> applyOffer(QString sourceId, QMap<QString, QJsonValue> values);
    // A source that shows the device's own music (the hostMedia capability):
    // first in the list, and what the Library opens on at first.
    bool keepsDeviceMusic(const QString& sourceId) const;

    Sources& sources_;
    ActivePlaylist& activePlaylist_;
    NowPlaying& nowPlaying_;
    Rpc::AuthStates& authStates_;
    SourcePage& sourcePage_;
    Rpc::SourceManager& sourceManager_;
    Covers::CoverArtCache& covers_;
    const Config::Settings& settings_;
    RowsModel sourceRows_;
    RowsModel favoriteRows_;
    RowsModel otherRows_;
    QString currentSourceId_;
    QString startingPlaylistId_;
    // A source was picked in the Library: it stays, as sources come and go.
    // Until then the Library follows what plays, else the first.
    bool picked_ = false;
};

} // namespace ViewModel
