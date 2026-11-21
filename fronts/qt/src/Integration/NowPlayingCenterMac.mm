#include "NowPlayingCenter.h"

#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QSize>
#include <QString>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "CoverArtCache.h"
#include "NowPlaying.h"
#include "PlaybackController.h"

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

// Built with ARC (see CMakeLists.txt): the Objective-C objects below are
// plain strong pointers, released with the Impl.

namespace Integration {

namespace {
    // Control Center shows the cover at up to ~300pt, at 2x on Retina.
    constexpr QSize kCoverSize { 600, 600 };
} // namespace

class NowPlayingCenter::Impl {
public:
    Impl(Playback::PlaybackController& playback, ViewModel::NowPlaying& nowPlaying, Covers::CoverArtCache& covers,
        NowPlayingCenter* owner)
        : playback_(playback)
        , nowPlaying_(nowPlaying)
        , covers_(covers)
        , owner_(owner)
    {
        registerCommands();

        QObject::connect(&playback_, &Playback::PlaybackController::trackChanged, owner_,
            [this](const Track&, const QString&) { updateTrack(); });
        QObject::connect(
            &playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, owner_, [this](bool available) {
                if (!available)
                    updateTrack();
            });
        // The system moves the progress on by itself from the rate; it only
        // needs the position again where it jumps or stops.
        QObject::connect(
            &playback_, &Playback::PlaybackController::playingChanged, owner_, [this](bool) { updatePosition(); });
        QObject::connect(
            &playback_, &Playback::PlaybackController::seeked, owner_, [this](qint64) { updatePosition(); });
        QObject::connect(
            &playback_, &Playback::PlaybackController::playModeChanged, owner_, [this]() { updatePlayMode(); });
        QObject::connect(
            &nowPlaying_, &ViewModel::NowPlaying::feedbackChanged, owner_, [this]() { updateFeedback(); });
        QObject::connect(
            &covers_, &Covers::CoverArtCache::pixmapReady, owner_, [this](const QString& url) { onCoverReady(url); });

        updateTrack();
        updatePlayMode();
        updateFeedback();
    }

    ~Impl()
    {
        for (const auto& [command, target] : targets_)
            [command removeTarget:target];
        MPNowPlayingInfoCenter* center = MPNowPlayingInfoCenter.defaultCenter;
        center.nowPlayingInfo = nil;
        center.playbackState = MPNowPlayingPlaybackStateStopped;
    }

private:
    // --- commands from the system -------------------------------------

    // `action` runs on the next turn of Qt's event loop, as SmtcService's
    // handlers do: the system waits for the handler's answer, and a
    // command may change what it is answering about.
    void handle(MPRemoteCommand* command, std::function<bool(MPRemoteCommandEvent*)> accept,
        std::function<void(MPRemoteCommandEvent*)> action)
    {
        command.enabled = YES;
        id target = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            if (!accept(event))
                return MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
            QMetaObject::invokeMethod(
                owner_,
                [this, action, event]() {
                    emit owner_->commandReceived();
                    action(event);
                },
                Qt::QueuedConnection);
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        targets_.emplace_back(command, target);
    }

    void registerCommands()
    {
        MPRemoteCommandCenter* center = MPRemoteCommandCenter.sharedCommandCenter;
        const auto hasTrack = [this](MPRemoteCommandEvent*) { return playback_.hasCurrentTrack(); };
        const auto always = [](MPRemoteCommandEvent*) { return true; };

        handle(center.playCommand, hasTrack, [this](MPRemoteCommandEvent*) {
            if (!playback_.isPlaying())
                nowPlaying_.togglePause();
        });
        handle(center.pauseCommand, hasTrack, [this](MPRemoteCommandEvent*) {
            if (playback_.isPlaying())
                nowPlaying_.togglePause();
        });
        handle(center.togglePlayPauseCommand, hasTrack, [this](MPRemoteCommandEvent*) { nowPlaying_.togglePause(); });
        // Paused, not stopped: as MprisPlayerAdaptor::Stop() — stopping
        // would drop the track, and Now Playing with it.
        handle(center.stopCommand, hasTrack, [this](MPRemoteCommandEvent*) {
            if (playback_.isPlaying())
                nowPlaying_.togglePause();
        });
        handle(center.nextTrackCommand, always, [this](MPRemoteCommandEvent*) { nowPlaying_.next(); });
        handle(center.previousTrackCommand, always, [this](MPRemoteCommandEvent*) { nowPlaying_.previous(); });
        handle(
            center.changePlaybackPositionCommand, [this](MPRemoteCommandEvent*) { return seekable(); },
            [this](MPRemoteCommandEvent* event) {
                if (!seekable())
                    return;
                const auto* change = static_cast<MPChangePlaybackPositionCommandEvent*>(event);
                const qint64 positionMs = qint64(change.positionTime * 1000);
                nowPlaying_.seek(std::clamp<qint64>(positionMs, 0, playback_.currentTrack().durationMs));
            });
        handle(center.changeShuffleModeCommand, always, [this](MPRemoteCommandEvent* event) {
            const auto* change = static_cast<MPChangeShuffleModeCommandEvent*>(event);
            nowPlaying_.setShuffle(change.shuffleType != MPShuffleTypeOff);
        });
        handle(center.changeRepeatModeCommand, always, [this](MPRemoteCommandEvent* event) {
            const auto* change = static_cast<MPChangeRepeatModeCommandEvent*>(event);
            switch (change.repeatType) {
            case MPRepeatTypeOne:
                nowPlaying_.setRepeatMode(Playback::RepeatMode::One);
                break;
            case MPRepeatTypeAll:
                nowPlaying_.setRepeatMode(Playback::RepeatMode::All);
                break;
            case MPRepeatTypeOff:
                nowPlaying_.setRepeatMode(Playback::RepeatMode::Off);
                break;
            }
        });
        // A feedback command's event is "negative" when it takes the
        // like/dislike back.
        handle(center.likeCommand, hasTrack, [this](MPRemoteCommandEvent* event) {
            nowPlaying_.setLiked(!static_cast<MPFeedbackCommandEvent*>(event).negative);
        });
        handle(center.dislikeCommand, hasTrack, [this](MPRemoteCommandEvent* event) {
            nowPlaying_.setDisliked(!static_cast<MPFeedbackCommandEvent*>(event).negative);
        });
    }

    bool seekable() const { return playback_.hasCurrentTrack() && playback_.currentTrack().durationMs > 0; }

    // --- our state pushed out to the system ---------------------------

    void updateTrack()
    {
        MPRemoteCommandCenter.sharedCommandCenter.changePlaybackPositionCommand.enabled = seekable();
        if (!playback_.hasCurrentTrack()) {
            info_ = nil;
            pendingCoverUrl_.clear();
            MPNowPlayingInfoCenter* center = MPNowPlayingInfoCenter.defaultCenter;
            center.nowPlayingInfo = nil;
            center.playbackState = MPNowPlayingPlaybackStateStopped;
            return;
        }

        const Track& track = playback_.currentTrack();
        QString artists;
        for (int i = 0; i < track.artists.size(); ++i) {
            if (i > 0)
                artists += QStringLiteral(", ");
            artists += track.artists[i].name;
        }
        info_ = [NSMutableDictionary dictionary];
        info_[MPMediaItemPropertyTitle] = track.title.toNSString();
        info_[MPMediaItemPropertyArtist] = artists.toNSString();
        if (track.album.has_value())
            info_[MPMediaItemPropertyAlbumTitle] = track.album->title.toNSString();
        if (track.durationMs > 0)
            info_[MPMediaItemPropertyPlaybackDuration] = @(track.durationMs / 1000.0);
        info_[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);

        QString coverUrl = track.coverUrl.value_or(QString());
        if (coverUrl.isEmpty() && track.album.has_value())
            coverUrl = track.album->coverUrl.value_or(QString());
        pendingCoverUrl_.clear();
        if (!coverUrl.isEmpty()) {
            const QPixmap cover = covers_.pixmap(coverUrl, kCoverSize);
            if (!cover.isNull())
                setArtwork(cover);
            else
                pendingCoverUrl_ = coverUrl; // onCoverReady() adds it
        }
        updatePosition();
    }

    // Elapsed time and rate together: the system extrapolates from both.
    void updatePosition()
    {
        if (!info_)
            return;
        const bool playing = playback_.isPlaying();
        info_[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(playback_.positionMs() / 1000.0);
        info_[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
        MPNowPlayingInfoCenter* center = MPNowPlayingInfoCenter.defaultCenter;
        center.nowPlayingInfo = [info_ copy];
        center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
    }

    void updatePlayMode()
    {
        MPRemoteCommandCenter* center = MPRemoteCommandCenter.sharedCommandCenter;
        // A radio can't shuffle or repeat its whole list (see
        // PlaybackController::isRadio()).
        const bool radio = playback_.isRadio();
        center.changeShuffleModeCommand.enabled = !radio;
        center.changeShuffleModeCommand.currentShuffleType
            = playback_.shuffleActive() ? MPShuffleTypeItems : MPShuffleTypeOff;
        center.changeRepeatModeCommand.enabled = !radio;
        switch (playback_.effectiveRepeatMode()) {
        case Playback::RepeatMode::One:
            center.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeOne;
            break;
        case Playback::RepeatMode::All:
            center.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeAll;
            break;
        case Playback::RepeatMode::Off:
            center.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeOff;
            break;
        }
    }

    // Like/Dislike only where the track's source takes them.
    void updateFeedback()
    {
        const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
        MPRemoteCommandCenter* center = MPRemoteCommandCenter.sharedCommandCenter;
        center.likeCommand.enabled = feedback.likeSupported;
        center.likeCommand.active = feedback.liked;
        center.likeCommand.localizedTitle = NowPlayingCenter::tr("Like").toNSString();
        center.likeCommand.localizedShortTitle = NowPlayingCenter::tr("Like").toNSString();
        center.dislikeCommand.enabled = feedback.dislikeSupported;
        center.dislikeCommand.active = feedback.disliked;
        center.dislikeCommand.localizedTitle = NowPlayingCenter::tr("Dislike").toNSString();
        center.dislikeCommand.localizedShortTitle = NowPlayingCenter::tr("Dislike").toNSString();
    }

    // --- cover art ----------------------------------------------------

    void onCoverReady(const QString& url)
    {
        if (pendingCoverUrl_.isEmpty() || url != pendingCoverUrl_)
            return;
        const QPixmap cover = covers_.pixmap(url, kCoverSize);
        if (cover.isNull())
            return; // another size of the same url landed; ours is still coming
        pendingCoverUrl_.clear();
        setArtwork(cover);
        updatePosition();
    }

    // Handed over as an image, never the cover's URL: as for MPRIS and
    // SMTC, the system couldn't fetch it the way the source connects.
    void setArtwork(const QPixmap& cover)
    {
        CGImageRef cgImage = cover.toImage().toCGImage();
        if (!cgImage)
            return;
        NSImage* image = [[NSImage alloc] initWithCGImage:cgImage size:NSZeroSize];
        CGImageRelease(cgImage);
        info_[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                                            requestHandler:^NSImage*(CGSize) {
                                                                                return image;
                                                                            }];
    }

    Playback::PlaybackController& playback_;
    ViewModel::NowPlaying& nowPlaying_;
    Covers::CoverArtCache& covers_;
    NowPlayingCenter* owner_;
    // The commands handled, with the token each handler was added under.
    std::vector<std::pair<MPRemoteCommand*, id>> targets_;
    // What Now Playing shows, kept to update the position in it.
    NSMutableDictionary* info_ = nil;
    QString pendingCoverUrl_;
};

NowPlayingCenter::NowPlayingCenter(Playback::PlaybackController& playback, ViewModel::NowPlaying& nowPlaying,
    Covers::CoverArtCache& covers, QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(playback, nowPlaying, covers, this))
{
}

NowPlayingCenter::~NowPlayingCenter() = default;

} // namespace Integration
