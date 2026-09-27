#include "Settings/GeneralPage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "Analytics.h"
#include "Autostart.h"
#include "Settings.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"
#include "WindowGlass.h"

namespace Ui::Settings {

GeneralPage::GeneralPage(Config::Settings& settings, App::Analytics& analytics, QObject* parent)
    : Page(parent)
    , settings_(settings)
    , analytics_(analytics)
{
}

QString GeneralPage::title() const { return tr("General"); }

QWidget* GeneralPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    const auto makeCheck = [this, widget](const QString& text, bool checked) {
        auto* check = new QCheckBox(text, widget);
        check->setChecked(checked);
        check->setFont(Theme::font(Theme::TextStyle::Body));
        connect(check, &QCheckBox::toggled, this, &Page::dirtyChanged);
        return check;
    };

    launchAtLogin_ = Integration::Autostart::isEnabled();
    launchAtLoginCheck_ = makeCheck(tr("Launch CloudMus when you log in"), launchAtLogin_);
    startHiddenCheck_ = makeCheck(tr("Start hidden in the tray"), settings_.startHiddenAtLogin());
    startHiddenCheck_->setEnabled(launchAtLogin_);
    connect(launchAtLoginCheck_, &QCheckBox::toggled, startHiddenCheck_, &QCheckBox::setEnabled);
    closeToTrayCheck_ = makeCheck(
        tr("Closing the window minimizes to the tray instead of quitting"), settings_.closeMinimizesToTray());

    glassCheck_ = makeCheck(tr("Glass background: blur what's behind the window"), glassWanted());
    // Where the app can't ask for the blur itself, the window can still be
    // made see-through for a desktop extension to blur — said so, since
    // without one it's just see-through. Where it can't be translucent at
    // all, the window stays opaque whatever this says.
    using Integration::WindowGlass::Support;
    const Support glassSupport = Integration::WindowGlass::support();
    glassCheck_->setEnabled(glassSupport != Support::None);
    auto* glassHint = new QLabel(widget);
    glassHint->setProperty("hint", true);
    glassHint->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    glassHint->setWordWrap(true);
    if (glassSupport == Support::SeeThrough)
        glassHint->setText(tr("This desktop doesn't blur behind windows by itself, so without help the window is "
                              "just see-through. On GNOME, install the Blur my Shell extension and add "
                              "\"cloudmus-qt\" to its application blur list."));
    else
        glassHint->setText(tr("Not supported by this desktop."));
    glassHint->setVisible(glassSupport != Support::Blur);

    analyticsCheck_ = makeCheck(tr("Send usage statistics to Google Analytics"), settings_.analyticsEnabled());

    // "Start hidden" only applies to a login launch — indented under it.
    auto* startHiddenRow = new QHBoxLayout;
    startHiddenRow->setContentsMargins(Theme::Spacing::space5, 0, 0, 0);
    startHiddenRow->addWidget(startHiddenCheck_);

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space2);
    layout->addWidget(launchAtLoginCheck_);
    layout->addLayout(startHiddenRow);
    layout->addWidget(closeToTrayCheck_);
    layout->addSpacing(Theme::Spacing::space3);
    layout->addWidget(glassCheck_);
    layout->addWidget(glassHint);
    layout->addSpacing(Theme::Spacing::space3);
    layout->addWidget(analyticsCheck_);
    return widget;
}

bool GeneralPage::glassWanted() const { return Theme::glassWanted(settings_.glassBackground()); }

bool GeneralPage::isDirty() const
{
    return launchAtLoginCheck_
        && (launchAtLoginCheck_->isChecked() != launchAtLogin_
            || startHiddenCheck_->isChecked() != settings_.startHiddenAtLogin()
            || closeToTrayCheck_->isChecked() != settings_.closeMinimizesToTray()
            || glassCheck_->isChecked() != glassWanted()
            || analyticsCheck_->isChecked() != settings_.analyticsEnabled());
}

Rpc::Task<bool> GeneralPage::apply()
{
    if (!launchAtLoginCheck_)
        co_return true;
    if (launchAtLoginCheck_->isChecked() != launchAtLogin_
        && Integration::Autostart::setEnabled(launchAtLoginCheck_->isChecked()))
        launchAtLogin_ = launchAtLoginCheck_->isChecked();
    settings_.setStartHiddenAtLogin(startHiddenCheck_->isChecked());
    settings_.setCloseMinimizesToTray(closeToTrayCheck_->isChecked());
    // Stored only once the user goes against the default, so an untouched
    // setting keeps following the desktop's light/dark scheme.
    if (glassCheck_->isChecked() != glassWanted())
        settings_.setGlassBackground(glassCheck_->isChecked());
    if (analyticsCheck_->isChecked() != settings_.analyticsEnabled())
        analytics_.setEnabled(analyticsCheck_->isChecked());
    // Switched once the Settings window closes — see
    // Ui::MainWindow::showSettingsDialog().
    co_return true;
}

} // namespace Ui::Settings
