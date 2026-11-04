#include "Host.h"

#include <QLocalServer>
#include <QLoggingCategory>
#include <QUrl>

#include "MobileCore.h"
#include "Names.h"

namespace Remote {

namespace {
Q_LOGGING_CATEGORY(lcRemote, "cloudmus.remote")
}

Host::Host(App::MobileCore& core, const QString& socketPath, QObject* parent)
    : QObject(parent)
{
    // A socket file left by a process that was killed blocks listen().
    QLocalServer::removeServer(socketPath);
    listening_ = host_.setHostUrl(QUrl(QStringLiteral("local:") + socketPath));
    if (!listening_) {
        qCWarning(lcRemote) << "can't listen on" << socketPath << host_.lastError();
        return;
    }
    host_.enableRemoting(&core.nowPlaying(), Names::nowPlaying);
    host_.enableRemoting(&core.activePlaylist(), Names::activePlaylist);
    host_.enableRemoting(&core.settings(), Names::settings);
    auto& tracks = core.tracks();
    host_.enableRemoting(&tracks, Names::tracks, tracks.roleNames().keys().toVector());
#ifdef CLOUDMUS_MOBILE_SHARED_CORE
    ViewModel::LibraryScreen& library = core.library();
    host_.enableRemoting(&library, Names::library);
    const auto offerRows = [this](ViewModel::RowsModel& rows, const QString& name) {
        host_.enableRemoting(&rows, name, rows.roleNames().keys().toVector());
    };
    offerRows(library.sourceRows(), Names::sources);
    offerRows(library.favoriteRows(), Names::favorites);
    offerRows(library.otherRows(), Names::otherPlaylists);
    host_.enableRemoting(&core.mainScreen(), Names::mainScreen);
    ViewModel::PlaylistScreen& playlist = core.playlistScreen();
    host_.enableRemoting(&playlist, Names::playlistScreen);
    host_.enableRemoting(&playlist.rows(), Names::openTracks, playlist.allRows().roleNames().keys().toVector());
    host_.enableRemoting(&core.trackScreen(), Names::trackScreen);
    offerRows(core.trackScreen().playlistRows(), Names::trackPlaylists);
#endif
    qCInfo(lcRemote) << "core offered on" << socketPath;
}

} // namespace Remote
