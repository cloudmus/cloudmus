#include "Settings/DownloadsPage.h"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

#include "Settings.h"
#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

DownloadsPage::DownloadsPage(Config::Settings& settings, QObject* parent)
    : Page(parent)
    , settings_(settings)
{
}

QString DownloadsPage::title() const { return tr("Downloads"); }

QWidget* DownloadsPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);

    downloadDirEdit_ = new QLineEdit(settings_.downloadDirectory(), widget);
    downloadDirEdit_->setPlaceholderText(Config::Settings::defaultDownloadDirectory());
    downloadDirEdit_->setFont(Theme::font(Theme::TextStyle::Body));
    connect(downloadDirEdit_, &QLineEdit::textChanged, this, &Page::dirtyChanged);

    auto* browseButton = new QPushButton(tr("Browse…"), widget);
    browseButton->setProperty("variant", "secondary");
    browseButton->setFont(Theme::font(Theme::TextStyle::Button));
    connect(browseButton, &QPushButton::clicked, widget, [this, widget]() {
        // Not QFileDialog::getExistingDirectory(...): that convenience
        // function constructs, execs, and destroys the dialog internally,
        // giving no chance to call Theme::useSystemFont() on it — needed
        // because QApplication::setFont()'s app-wide Manrope default has
        // no subtree opt-out (see Typography.h), and this dialog should
        // stay fully native-looking like any other system file picker.
        QFileDialog dialog(widget, tr("Download folder"), downloadDirEdit_->text());
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly);
        Theme::useSystemFont(&dialog);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            downloadDirEdit_->setText(dialog.selectedFiles().constFirst());
    });

    auto* row = new QHBoxLayout;
    row->setSpacing(Theme::Spacing::space2);
    row->addWidget(downloadDirEdit_, 1);
    row->addWidget(browseButton);

    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Download folder:"), row);
    return widget;
}

bool DownloadsPage::isDirty() const
{
    // Compared the way setDownloadDirectory() stores it: empty means the
    // default folder.
    if (!downloadDirEdit_)
        return false;
    const QString text = downloadDirEdit_->text().trimmed();
    const QString effective = text.isEmpty() ? Config::Settings::defaultDownloadDirectory() : text;
    return QDir::cleanPath(effective) != QDir::cleanPath(settings_.downloadDirectory());
}

void DownloadsPage::apply()
{
    if (downloadDirEdit_)
        settings_.setDownloadDirectory(downloadDirEdit_->text());
}

} // namespace Ui::Settings
