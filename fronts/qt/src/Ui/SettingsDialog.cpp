#include "SettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "Typography.h"

namespace Ui {

SettingsDialog::SettingsDialog(Config::Settings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(tr("Settings"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why

    closeToTrayCheck_ = new QCheckBox(tr("Closing the window minimizes to the tray instead of quitting"), this);
    closeToTrayCheck_->setChecked(settings_.closeMinimizesToTray());
    closeToTrayCheck_->setFont(Theme::font(Theme::TextStyle::Body));

    downloadDirEdit_ = new QLineEdit(settings_.downloadDirectory(), this);
    downloadDirEdit_->setPlaceholderText(Config::Settings::defaultDownloadDirectory());
    downloadDirEdit_->setFont(Theme::font(Theme::TextStyle::Body));
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    browseButton->setProperty("variant", "secondary");
    browseButton->setFont(Theme::font(Theme::TextStyle::Button));
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        // Not QFileDialog::getExistingDirectory(...): that convenience
        // function constructs, execs, and destroys the dialog internally,
        // giving no chance to call Theme::useSystemFont() on it — needed
        // because QApplication::setFont()'s app-wide Manrope default has
        // no subtree opt-out (see Typography.h), and this dialog should
        // stay fully native-looking like any other system file picker.
        QFileDialog dialog(this, tr("Download folder"), downloadDirEdit_->text());
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly);
        Theme::useSystemFont(&dialog);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            downloadDirEdit_->setText(dialog.selectedFiles().constFirst());
    });
    auto* downloadRow = new QHBoxLayout;
    downloadRow->addWidget(downloadDirEdit_);
    downloadRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->addRow(tr("Download folder:"), downloadRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setProperty("variant", "primary");
    buttons->button(QDialogButtonBox::Cancel)->setProperty("variant", "secondary");
    for (QAbstractButton* button : buttons->buttons()) {
        button->setFont(Theme::font(Theme::TextStyle::Button));
        // Some platform themes (KDE's in particular) inject a standard
        // checkmark/cross icon onto Ok/Cancel regardless of the active
        // QStyle — clashes with the flat, icon-less button look everywhere
        // else in the app.
        button->setIcon(QIcon());
        // QDialogButtonBox's own construction appears to polish its
        // standard buttons before we get a chance to set "variant" above,
        // so the [variant="..."] QSS rule never gets (re-)evaluated
        // against it — Qt's documented fix for "a QSS-relevant dynamic
        // property changed after the widget was polished."
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
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
