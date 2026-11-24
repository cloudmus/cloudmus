#pragma once

#include <QObject>
#include <QPointer>

namespace App {
class Analytics;
}
namespace ViewModel {
class UpdateScreen;
}

namespace Ui {

class UpdateDialog;
class WindowHost;

// Turns ViewModel::UpdateScreen's answers into dialogs: an update found
// opens Ui::UpdateDialog, and a manual check also says when there's
// nothing new or the check failed. An update found while the window is
// hidden in the tray waits for the window to show, rather than popping up
// on its own over the desktop.
class UpdateFlow : public QObject {
    Q_OBJECT

public:
    UpdateFlow(
        ViewModel::UpdateScreen& screen, WindowHost& windowHost, App::Analytics& analytics, QObject* parent = nullptr);

    // A while after startup, so as not to compete with it — and not for a
    // dev build at all (see Update::UpdateChecker::isDevBuild()).
    void checkAtStartup();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void offer(bool manual);
    void showDialog();
    void watchWindow();

    ViewModel::UpdateScreen& screen_;
    WindowHost& windowHost_;
    App::Analytics& analytics_;
    QPointer<UpdateDialog> dialog_;
    // Found while the window was hidden: shown with it.
    bool deferred_ = false;
};

} // namespace Ui
