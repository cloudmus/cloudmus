#include "Settings/GeneralPage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "Autostart.h"
#include "Settings.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

GeneralPage::GeneralPage(Config::Settings& settings, QObject* parent)
    : Page(parent)
    , settings_(settings)
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
    return widget;
}

bool GeneralPage::isDirty() const
{
    return launchAtLoginCheck_
        && (launchAtLoginCheck_->isChecked() != launchAtLogin_
            || startHiddenCheck_->isChecked() != settings_.startHiddenAtLogin()
            || closeToTrayCheck_->isChecked() != settings_.closeMinimizesToTray());
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
    co_return true;
}

} // namespace Ui::Settings
