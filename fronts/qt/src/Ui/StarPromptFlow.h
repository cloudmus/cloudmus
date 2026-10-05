#pragma once

#include <QObject>

namespace App {
class StarPrompt;
}

namespace Ui {

class WindowHost;

// Shows the GitHub star prompt once App::StarPrompt says it's due: a while
// after that, so as not to compete with startup or the update dialog, and
// only over the visible window with no other dialog open — otherwise it
// waits for the window to show or the dialog to close.
class StarPromptFlow : public QObject {
    Q_OBJECT

public:
    // `preview` (--star-prompt): show the dialog shortly after the window
    // does, whatever the day count, and record nothing — to try it out.
    StarPromptFlow(App::StarPrompt& prompt, WindowHost& windowHost, bool preview, QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void schedule(int delayMs);
    void tryShow();
    void watchWindow();

    App::StarPrompt& prompt_;
    WindowHost& windowHost_;
    const bool preview_;
    bool pending_ = false; // due, waiting for the window to show
    bool scheduled_ = false; // a tryShow() is on its timer
    bool shown_ = false;
};

} // namespace Ui
