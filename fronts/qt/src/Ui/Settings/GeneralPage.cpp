#include "Settings/GeneralPage.h"

#include <QCheckBox>
#include <QVBoxLayout>

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

    closeToTrayCheck_ = new QCheckBox(tr("Closing the window minimizes to the tray instead of quitting"), widget);
    closeToTrayCheck_->setChecked(settings_.closeMinimizesToTray());
    closeToTrayCheck_->setFont(Theme::font(Theme::TextStyle::Body));
    connect(closeToTrayCheck_, &QCheckBox::toggled, this, &Page::dirtyChanged);

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space2);
    layout->addWidget(closeToTrayCheck_);
    return widget;
}

bool GeneralPage::isDirty() const
{
    return closeToTrayCheck_ && closeToTrayCheck_->isChecked() != settings_.closeMinimizesToTray();
}

void GeneralPage::apply()
{
    if (closeToTrayCheck_)
        settings_.setCloseMinimizesToTray(closeToTrayCheck_->isChecked());
}

} // namespace Ui::Settings
