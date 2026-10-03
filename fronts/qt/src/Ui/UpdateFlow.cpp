#include "UpdateFlow.h"

#include <QEvent>
#include <QLocale>
#include <QTimer>

#include "InfoDialog.h"
#include "MainWindow.h"
#include "UpdateChecker.h"
#include "UpdateDialog.h"
#include "WindowHost.h"

namespace Ui {

namespace {
constexpr int kStartupCheckDelayMs = 5000;
} // namespace

UpdateFlow::UpdateFlow(Update::UpdateChecker& checker, WindowHost& windowHost, QObject* parent)
    : QObject(parent)
    , checker_(checker)
    , windowHost_(windowHost)
{
    connect(&windowHost_, &WindowHost::checkForUpdatesRequested, this, [this]() {
        // One is open already, maybe mid-download: that's the answer.
        if (dialog_) {
            dialog_->raise();
            dialog_->activateWindow();
            return;
        }
        checker_.check(true);
    });
    connect(&checker_, &Update::UpdateChecker::updateAvailable, this, &UpdateFlow::offer);
    connect(&checker_, &Update::UpdateChecker::upToDate, this, [this](const std::optional<Update::Release>& own) {
        // Rich text (InfoDialog's label detects it): the versions in bold.
        const auto bold = [](const QString& text) { return QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()); };
        QString text = tr("Version %1 is the latest one.").arg(bold(checker_.currentVersion()));
        if (own && own->published.isValid()) {
            const QString date = QLocale().toString(own->published, QStringLiteral("d MMMM yyyy"));
            // A build between releases has no date of its own.
            text += checker_.isDevBuild() ? tr(" It's built on %1, released on %2.").arg(bold(own->name), date)
                                          : tr(" Released on %1.").arg(date);
        }
        InfoDialog(tr("Check for Updates"), tr("CloudMus is up to date"), text, QString(), windowHost_.window()).exec();
    });
    connect(&checker_, &Update::UpdateChecker::checkFailed, this, [this](const QString& error, bool manual) {
        if (!manual)
            return;
        InfoDialog(tr("Check for Updates"), tr("Couldn't check for updates"), error, QString(), windowHost_.window())
            .exec();
    });
    connect(&windowHost_, &WindowHost::windowCreated, this, &UpdateFlow::watchWindow);
    watchWindow();
}

void UpdateFlow::checkAtStartup()
{
    if (checker_.isDevBuild())
        return;
    QTimer::singleShot(kStartupCheckDelayMs, this, [this]() { checker_.check(false); });
}

bool UpdateFlow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show && watched == windowHost_.window() && deferred_) {
        // After the window is up, not in the middle of its showing.
        QTimer::singleShot(0, this, [this]() {
            if (!deferred_)
                return;
            const Update::PendingUpdate update = *deferred_;
            deferred_.reset();
            showDialog(update);
        });
    }
    return QObject::eventFilter(watched, event);
}

void UpdateFlow::offer(const Update::PendingUpdate& update, bool manual)
{
    if (dialog_)
        return;
    if (!manual && !windowHost_.window()->isVisible()) {
        deferred_ = update;
        return;
    }
    showDialog(update);
}

void UpdateFlow::showDialog(const Update::PendingUpdate& update)
{
    if (dialog_)
        return;
    dialog_ = new UpdateDialog(checker_, update, windowHost_.window());
    connect(dialog_, &UpdateDialog::quitRequested, &windowHost_, &WindowHost::quit, Qt::QueuedConnection);
    dialog_->show();
}

void UpdateFlow::watchWindow() { windowHost_.window()->installEventFilter(this); }

} // namespace Ui
