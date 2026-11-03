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
    qCInfo(lcRemote) << "core offered on" << socketPath;
}

} // namespace Remote
