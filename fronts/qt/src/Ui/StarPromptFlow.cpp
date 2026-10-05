#include "StarPromptFlow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QTimer>
#include <QUrl>

#include "Icons.h"
#include "InfoDialog.h"
#include "MainWindow.h"
#include "StarPrompt.h"
#include "WindowHost.h"

namespace Ui {

namespace {
constexpr int kDueDelayMs = 30000;
constexpr int kPreviewDelayMs = 2000;
} // namespace

StarPromptFlow::StarPromptFlow(App::StarPrompt& prompt, WindowHost& windowHost, bool preview, QObject* parent)
    : QObject(parent)
    , prompt_(prompt)
    , windowHost_(windowHost)
    , preview_(preview)
{
    connect(&windowHost_, &WindowHost::windowCreated, this, &StarPromptFlow::watchWindow);
    watchWindow();
    if (preview_) {
        schedule(kPreviewDelayMs);
        return;
    }
    connect(&prompt_, &App::StarPrompt::due, this, [this]() { schedule(kDueDelayMs); });
    // Today counts as a day of use from the start, not only once a track plays.
    prompt_.recordUsage();
    if (prompt_.isDue())
        schedule(kDueDelayMs);
}

bool StarPromptFlow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show && watched == windowHost_.window() && pending_) {
        pending_ = false;
        // Not straight away: give the window a moment of its own first.
        schedule(preview_ ? kPreviewDelayMs : kDueDelayMs);
    }
    return QObject::eventFilter(watched, event);
}

void StarPromptFlow::schedule(int delayMs)
{
    if (shown_ || scheduled_)
        return;
    scheduled_ = true;
    QTimer::singleShot(delayMs, this, [this]() {
        scheduled_ = false;
        tryShow();
    });
}

void StarPromptFlow::tryShow()
{
    if (shown_)
        return;
    MainWindow* window = windowHost_.window();
    if (!window->isVisible()) {
        pending_ = true;
        return;
    }
    // Another dialog (an update, the settings) is up: ask after it's gone.
    if (QApplication::activeModalWidget() != nullptr) {
        schedule(kDueDelayMs);
        return;
    }
    shown_ = true;
    // Done as it opens: however it's closed, it isn't asked again.
    if (!preview_)
        prompt_.markShown();
    InfoDialog dialog(tr("Support CloudMus"), tr("Enjoying CloudMus?"),
        tr("If the player has found a place in your day, please give it a star on GitHub. It's easy for you and it "
           "makes the author happy."),
        tr("Star on GitHub"), window, tr("No, thanks"),
        Theme::icon(QStringLiteral("favorite"), Theme::IconColor::Accent, 56));
    const bool starred = dialog.exec() == QDialog::Accepted;
    if (!preview_)
        prompt_.markAnswered(starred);
    if (starred)
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/cloudmus/cloudmus")));
}

void StarPromptFlow::watchWindow() { windowHost_.window()->installEventFilter(this); }

} // namespace Ui
