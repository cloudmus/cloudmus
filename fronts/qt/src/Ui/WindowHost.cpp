#include "WindowHost.h"

#include <QTimer>

#include "Core.h"
#include "MainWindow.h"
#include "Tokens.h"

namespace Ui {

WindowHost::WindowHost(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
{
    create();
    // Glass is set up as a window is created — switching it takes a new one.
    connect(&Theme::notifier(), &Theme::Notifier::glassChanged, this, &WindowHost::recreate);
}

WindowHost::~WindowHost() = default;

void WindowHost::create()
{
    window_ = std::make_unique<MainWindow>(core_);
    connect(window_.get(), &MainWindow::aboutToReallyQuit, this, &WindowHost::aboutToReallyQuit);
    emit windowCreated(window_.get());
}

void WindowHost::show() { window_->show(); }

bool WindowHost::isOnScreen() const { return window_->isOnScreen(); }

void WindowHost::toggleShown() { window_->toggleShown(); }

void WindowHost::bringToFront(const QString& activationToken) { window_->bringToFront(activationToken); }

void WindowHost::quit() { window_->quitForReal(); }

void WindowHost::recreate()
{
    if (recreatePending_)
        return;
    recreatePending_ = true;
    QTimer::singleShot(0, this, [this]() {
        recreatePending_ = false;
        const bool visible = window_->isVisible();
        // The old window saves its geometry as it goes; the new one picks
        // it up, maximized or not.
        window_.reset();
        create();
        if (visible)
            window_->show();
    });
}

} // namespace Ui
