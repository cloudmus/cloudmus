#include "Dispatcher.h"

#include <algorithm>

#include "NowPlaying.h"
#include "Registry.h"

namespace Hotkeys {

namespace {
constexpr int kVolumeStep = 5;
} // namespace

Dispatcher::Dispatcher(ViewModel::NowPlaying& nowPlaying, Registry& registry, QObject* parent)
    : QObject(parent)
    , nowPlaying_(nowPlaying)
    , registry_(registry)
{
}

void Dispatcher::trigger(const QString& actionId, const QString& activationToken)
{
    if (const ActionInfo* entry = infoById(actionId))
        trigger(entry->action, activationToken);
}

Dispatcher::Notice Dispatcher::trackNotice(const QString& title) const
{
    Notice notice;
    notice.title = title;
    if (!nowPlaying_.hasTrack())
        return notice;
    const Track& track = nowPlaying_.track();
    QString artists;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artists += QStringLiteral(", ");
        artists += track.artists[i].name;
    }
    notice.body = artists.isEmpty() ? track.title : track.title + QStringLiteral(" — ") + artists;
    notice.coverUrl = track.coverUrl.value_or(QString());
    if (notice.coverUrl.isEmpty() && track.album.has_value())
        notice.coverUrl = track.album->coverUrl.value_or(QString());
    return notice;
}

void Dispatcher::trigger(Action action, const QString& activationToken)
{
    const bool wantNotice = registry_.binding(action).notify;
    const auto say = [&](const Notice& notice) {
        if (wantNotice)
            emit noticeRequested(notice);
    };

    switch (action) {
        case Action::ShowPlayer:
            emit showPlayerRequested(activationToken);
            break;
        case Action::PlayPause:
            nowPlaying_.togglePause();
            break;
        case Action::Next:
            nowPlaying_.next();
            break;
        case Action::Previous:
            nowPlaying_.previous();
            break;
        case Action::Stop:
            nowPlaying_.stop();
            break;
        case Action::VolumeUp:
        case Action::VolumeDown: {
            const int step = action == Action::VolumeUp ? kVolumeStep : -kVolumeStep;
            nowPlaying_.setVolume(std::clamp(nowPlaying_.volume() + step, 0, 100));
            say({ tr("Volume"), tr("%1%").arg(nowPlaying_.volume()), { } });
            break;
        }
        case Action::Like: {
            const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
            if (!feedback.likeSupported) {
                say({ tr("Like"), tr("Nothing to like"), { } });
                break;
            }
            if (feedback.likeBusy)
                break;
            nowPlaying_.setLiked(!feedback.liked);
            say(trackNotice(feedback.liked ? tr("Removed from liked") : tr("Liked")));
            break;
        }
        case Action::Dislike: {
            const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
            if (!feedback.dislikeSupported) {
                say({ tr("Dislike"), tr("Nothing to dislike"), { } });
                break;
            }
            if (feedback.dislikeBusy)
                break;
            nowPlaying_.setDisliked(!feedback.disliked);
            say(trackNotice(feedback.disliked ? tr("Dislike removed") : tr("Disliked")));
            break;
        }
        case Action::Download: {
            const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
            if (!nowPlaying_.hasTrack() || !feedback.downloadSupported) {
                say({ tr("Download"), tr("Nothing to download"), { } });
                break;
            }
            if (feedback.downloadBusy)
                break;
            nowPlaying_.download();
            say(trackNotice(tr("Downloading")));
            break;
        }
    }
}

} // namespace Hotkeys
