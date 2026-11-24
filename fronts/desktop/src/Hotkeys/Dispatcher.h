#pragma once

#include <QObject>
#include <QString>

#include "Actions.h"
#include "CuePlayer.h"

namespace ViewModel {
class NowPlaying;
}

namespace Hotkeys {

class Registry;

// What a hotkey does once pressed — from wherever it came (a global hotkey,
// one inside the window) — through the same view model the buttons use.
class Dispatcher : public QObject {
    Q_OBJECT

public:
    // What an action wants to tell the user, for a notification.
    struct Notice {
        QString title;
        QString body;
        QString coverUrl;
    };

    Dispatcher(ViewModel::NowPlaying& nowPlaying, Registry& registry, QObject* parent = nullptr);

    // `activationToken` (may be empty) lets a window be raised where the
    // compositor demands one.
    void trigger(Action action, const QString& activationToken = { });
    void trigger(const QString& actionId, const QString& activationToken = { });

signals:
    // The window is the UI's: it decides whether to show or hide.
    void showPlayerRequested(const QString& activationToken);
    void noticeRequested(const Hotkeys::Dispatcher::Notice& notice);
    void cueRequested(Playback::Cue cue);

private:
    Notice trackNotice(const QString& title) const;

    ViewModel::NowPlaying& nowPlaying_;
    Registry& registry_;
};

} // namespace Hotkeys
