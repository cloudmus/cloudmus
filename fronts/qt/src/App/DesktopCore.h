#pragma once

#include "Core.h"
#include "CuePlayer.h"
#include "Dispatcher.h"
#include "Registry.h"
#include "StarPrompt.h"
#include "UpdateChecker.h"

namespace App {

// App::Core plus what only the desktop has: hotkeys and the sounds that
// answer them, updates of the app, and the prompt for a star.
class DesktopCore : public Core {
    Q_OBJECT

public:
    explicit DesktopCore(QObject* parent = nullptr);

    Hotkeys::Registry& hotkeys() { return hotkeys_; }
    Hotkeys::Dispatcher& hotkeyDispatcher() { return hotkeyDispatcher_; }
    Update::UpdateChecker& updates() { return updates_; }
    StarPrompt& starPrompt() { return starPrompt_; }

private:
    Hotkeys::Registry hotkeys_;
    Hotkeys::Dispatcher hotkeyDispatcher_;
    Playback::CuePlayer cuePlayer_;
    Update::UpdateChecker updates_;
    StarPrompt starPrompt_;
};

} // namespace App
