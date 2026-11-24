#include "UpdateFlow.h"

#include <QEvent>
#include <QTimer>

#include "Analytics.h"
#include "InfoDialog.h"
#include "MainWindow.h"
#include "UpdateDialog.h"
#include "UpdateScreen.h"
#include "WindowHost.h"

namespace Ui {

namespace {
constexpr int kStartupCheckDelayMs = 5000;
} // namespace

UpdateFlow::UpdateFlow(
    ViewModel::UpdateScreen& screen, WindowHost& windowHost, App::Analytics& analytics, QObject* parent)
    : QObject(parent)
    , screen_(screen)
    , windowHost_(windowHost)
    , analytics_(analytics)
{
    connect(&windowHost_, &WindowHost::checkForUpdatesRequested, this, [this]() {
        // One is open already, maybe mid-download: that's the answer.
        if (dialog_) {
            dialog_->raise();
            dialog_->activateWindow();
            return;
        }
        screen_.check();
    });
    connect(&screen_, &ViewModel::UpdateScreen::offered, this, &UpdateFlow::offer);
    connect(&screen_, &ViewModel::UpdateScreen::checkingChanged, this, [this]() {
        if (screen_.checking() || screen_.checkHeading().isEmpty())
            return;
        // Rich text (InfoDialog's label detects it): the versions in bold.
        const QString heading = screen_.checkHeading();
        const QString text = screen_.checkText();
        screen_.clearCheck();
        InfoDialog(tr("Check for Updates"), heading, text, QString(), windowHost_.window()).exec();
    });
    connect(&screen_, &ViewModel::UpdateScreen::stepTaken, this,
        [this](const QString& step) { analytics_.recordUpdate(step); });
    connect(&windowHost_, &WindowHost::windowCreated, this, &UpdateFlow::watchWindow);
    watchWindow();
}

void UpdateFlow::checkAtStartup()
{
    QTimer::singleShot(kStartupCheckDelayMs, this, [this]() { screen_.checkAutomatically(); });
}

bool UpdateFlow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show && watched == windowHost_.window() && deferred_) {
        // After the window is up, not in the middle of its showing.
        QTimer::singleShot(0, this, [this]() {
            if (!deferred_)
                return;
            deferred_ = false;
            if (screen_.step() == ViewModel::UpdateScreen::Offer)
                showDialog();
        });
    }
    return QObject::eventFilter(watched, event);
}

void UpdateFlow::offer(bool manual)
{
    if (dialog_)
        return;
    analytics_.recordUpdate(QStringLiteral("offered"), manual);
    if (!manual && !windowHost_.window()->isVisible()) {
        deferred_ = true;
        return;
    }
    showDialog();
}

void UpdateFlow::showDialog()
{
    if (dialog_)
        return;
    dialog_ = new UpdateDialog(screen_, windowHost_.window());
    connect(dialog_, &UpdateDialog::quitRequested, &windowHost_, &WindowHost::quit, Qt::QueuedConnection);
    dialog_->show();
}

void UpdateFlow::watchWindow() { windowHost_.window()->installEventFilter(this); }

} // namespace Ui
