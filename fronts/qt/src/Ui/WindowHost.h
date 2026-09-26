#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace App {
class Core;
}

namespace Ui {

class MainWindow;

// Owns the main window, and can replace it with a new one — e.g. to switch
// the glass background, which is fixed when a window is created. Nothing
// is lost doing that: all the state lives in App::Core, and a new window
// shows it as it is at once (see docs/mvvm-plan.md). Everything that has
// to reach "the window" — the tray, MPRIS, notifications, main() — goes
// through here rather than holding on to one.
class WindowHost : public QObject {
    Q_OBJECT

public:
    explicit WindowHost(App::Core& core, QObject* parent = nullptr);
    ~WindowHost() override;

    MainWindow* window() const { return window_.get(); }

    void show();
    // See MainWindow's own.
    bool isOnScreen() const;
    void toggleShown();
    void bringToFront(const QString& activationToken = QString());
    // Quit for real — backends shut down first (aboutToReallyQuit()).
    void quit();

    // Replaces the window with a new one in the same place and state (shown
    // or not) — once control is back in the event loop, so it's safe to
    // ask for from inside the window itself.
    void recreate();

signals:
    void aboutToReallyQuit();
    // A new window is up (the first one included).
    void windowCreated(Ui::MainWindow* window);

private:
    void create();

    App::Core& core_;
    std::unique_ptr<MainWindow> window_;
    bool recreatePending_ = false;
};

} // namespace Ui
