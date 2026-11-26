#pragma once

#include <QJsonValue>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include "Coro.h"
#include "Models.h"
#include "RowsModel.h"

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
    // Signing in to the source picked: needed at all, then what it asks for.
    Q_PROPERTY(bool authNeeded READ authNeeded NOTIFY authNeededChanged)
    Q_PROPERTY(QString authUrl READ authUrl NOTIFY authUrlChanged)
    Q_PROPERTY(QString authCode READ authCode NOTIFY authCodeChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authErrorChanged)
    Q_PROPERTY(bool canSignOut READ canSignOut NOTIFY canSignOutChanged)
    // What the source picked says it can do, as ids the page words itself
    // ("radio", "download", ...); empty while it isn't running.
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY capabilitiesChanged)

public:
    LibraryScreen(Sources& sources, ActivePlaylist& activePlaylist, NowPlaying& nowPlaying, Rpc::AuthStates& authStates,
        SourcePage& sourcePage, Rpc::SourceManager& sourceManager, Covers::CoverArtCache& covers,
        QObject* parent = nullptr);

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
    QString authUrl() const;
    QString authCode() const;
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
    void authUrlChanged();
    void authCodeChanged();
    void authErrorChanged();
    void canSignOutChanged();
    void capabilitiesChanged();
    // Something started playing from here: time for the main screen.
    void activated();

private:
    void rebuild();
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
