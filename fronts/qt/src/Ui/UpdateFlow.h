#pragma once

#include <QObject>
#include <QPointer>

#include <optional>

#include "ReleaseFeed.h"

namespace App {
class Analytics;
}
namespace Update {
class UpdateChecker;
}

namespace Ui {

class UpdateDialog;
class WindowHost;

// Turns Update::UpdateChecker's answers into dialogs: an update found
// opens Ui::UpdateDialog, and a manual check also says when there's
// nothing new or the check failed. An update found while the window is
// hidden in the tray waits for the window to show, rather than popping up
// on its own over the desktop.
class UpdateFlow : public QObject {
    Q_OBJECT

public:
    UpdateFlow(
        Update::UpdateChecker& checker, WindowHost& windowHost, App::Analytics& analytics, QObject* parent = nullptr);

    // A while after startup, so as not to compete with it — and not for a
    // dev build at all (see UpdateChecker::isDevBuild()).
    void checkAtStartup();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void offer(const Update::PendingUpdate& update, bool manual);
    void showDialog(const Update::PendingUpdate& update);
    void watchWindow();

    Update::UpdateChecker& checker_;
    WindowHost& windowHost_;
    App::Analytics& analytics_;
    QPointer<UpdateDialog> dialog_;
    std::optional<Update::PendingUpdate> deferred_;
};

} // namespace Ui
