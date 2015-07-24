#include "applicationoptionspage.h"

#include "ui_applicationoptionspage.h"
#include "options.h"
#include "scopedtools.h"

ApplicationOptionsPage::ApplicationOptionsPage(QWidget *parent)
    : OptionsPage(parent)
    , ui(new Ui_ApplicationOptionsPage)
    , blockUpdate_(false)
    , blockChange_(false)
{
    ui->setupUi(this);

    connect(ui->startHiddenCheck, SIGNAL(toggled(bool)), SLOT(changedByUser()));
    connect(ui->closeToTrayCheck, SIGNAL(toggled(bool)), SLOT(changedByUser()));

    updateOptions();
    connect(Options::instance(), SIGNAL(changed(const char*)), SLOT(updateOptions()));
}

ApplicationOptionsPage::~ApplicationOptionsPage()
{
    delete ui;
}

void ApplicationOptionsPage::changedByUser()
{
    if (blockChange_)
        return;
    SCOPE_COCK_FLAG(blockUpdate_);
    Options::setValue<Options::StartHiddenOption>(ui->startHiddenCheck->isChecked());
    Options::setValue<Options::CloseToTrayOption>(ui->closeToTrayCheck->isChecked());
}

void ApplicationOptionsPage::updateOptions()
{
    if (blockUpdate_)
        return;
    SCOPE_COCK_FLAG(blockChange_);
    ui->startHiddenCheck->setChecked(Options::value<Options::StartHiddenOption>());
    ui->closeToTrayCheck->setChecked(Options::value<Options::CloseToTrayOption>());
}

