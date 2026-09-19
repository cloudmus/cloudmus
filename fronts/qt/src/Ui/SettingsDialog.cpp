#include "SettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Typography.h"

namespace Ui {

SettingsDialog::SettingsDialog(Config::Settings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(tr("Settings"));

    closeToTrayCheck_ = new QCheckBox(tr("Closing the window minimizes to the tray instead of quitting"), this);
    closeToTrayCheck_->setChecked(settings_.closeMinimizesToTray());
    closeToTrayCheck_->setFont(Theme::font(Theme::TextStyle::Body));

    downloadDirEdit_ = new QLineEdit(settings_.downloadDirectory(), this);
    downloadDirEdit_->setFont(Theme::font(Theme::TextStyle::Body));
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    browseButton->setProperty("variant", "secondary");
    browseButton->setFont(Theme::font(Theme::TextStyle::Button));
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Download folder"), downloadDirEdit_->text());
        if (!dir.isEmpty())
            downloadDirEdit_->setText(dir);
    });
    auto* downloadRow = new QHBoxLayout;
    downloadRow->addWidget(downloadDirEdit_);
    downloadRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->addRow(tr("Download folder:"), downloadRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setProperty("variant", "primary");
    buttons->button(QDialogButtonBox::Cancel)->setProperty("variant", "secondary");
    for (QAbstractButton* button : buttons->buttons())
        button->setFont(Theme::font(Theme::TextStyle::Button));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addWidget(closeToTrayCheck_);
    root->addLayout(form);
    root->addWidget(buttons);
}

void SettingsDialog::save()
{
    settings_.setCloseMinimizesToTray(closeToTrayCheck_->isChecked());
    settings_.setDownloadDirectory(downloadDirEdit_->text());
}

} // namespace Ui
