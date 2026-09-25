#include "Settings/DownloadsPage.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

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

    layoutCombo_ = new QComboBox(widget);
    layoutCombo_->setFont(Theme::font(Theme::TextStyle::Body));
    // A QStyledItemDelegate, not the combo's default one: only that one
    // honors the ::item QSS rules (see StyleSheet.cpp's settingsBlock()).
    layoutCombo_->setItemDelegate(new QStyledItemDelegate(layoutCombo_));
    using Layout = Config::Settings::DownloadLayout;
    layoutCombo_->addItem(tr("None"), int(Layout::Flat));
    layoutCombo_->addItem(tr("By source"), int(Layout::BySource));
    layoutCombo_->addItem(tr("By artist"), int(Layout::ByArtist));
    layoutCombo_->addItem(tr("By artist and album"), int(Layout::ByArtistAlbum));
    layoutCombo_->setCurrentIndex(layoutCombo_->findData(int(settings_.downloadLayout())));
    connect(layoutCombo_, &QComboBox::currentIndexChanged, this, &Page::dirtyChanged);

    exampleLabel_ = new QLabel(widget);
    exampleLabel_->setProperty("hint", true);
    exampleLabel_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    exampleLabel_->setWordWrap(true);
    exampleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connect(downloadDirEdit_, &QLineEdit::textChanged, this, &DownloadsPage::updateExample);
    connect(layoutCombo_, &QComboBox::currentIndexChanged, this, &DownloadsPage::updateExample);
    updateExample();

    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(Theme::Spacing::space3);
    form->setVerticalSpacing(Theme::Spacing::space2);
    form->addRow(tr("Download folder:"), row);
    // The example sits right under the choice it illustrates.
    auto* layoutColumn = new QVBoxLayout;
    layoutColumn->setSpacing(Theme::Spacing::space1);
    layoutColumn->addWidget(layoutCombo_);
    layoutColumn->addWidget(exampleLabel_);
    form->addRow(tr("Subfolders:"), layoutColumn);
    return widget;
}

Config::Settings::DownloadLayout DownloadsPage::selectedLayout() const
{
    return Config::Settings::DownloadLayout(layoutCombo_->currentData().toInt());
}

void DownloadsPage::updateExample()
{
    // Placeholders in angle brackets rather than a made-up track: it's the
    // folder shape being chosen here. The file name itself is up to each
    // source.
    QString root = downloadDirEdit_->text().trimmed();
    if (root.isEmpty())
        root = Config::Settings::defaultDownloadDirectory();
    QString path = QDir::cleanPath(root);
    switch (selectedLayout()) {
        case Config::Settings::DownloadLayout::Flat:
            break;
        case Config::Settings::DownloadLayout::BySource:
            path += tr("/<source>");
            break;
        case Config::Settings::DownloadLayout::ByArtist:
            path += tr("/<artist>");
            break;
        case Config::Settings::DownloadLayout::ByArtistAlbum:
            path += tr("/<artist>/<album>");
            break;
    }
    exampleLabel_->setText(tr("Tracks are saved to %1/").arg(path));
}

bool DownloadsPage::isDirty() const
{
    // Compared the way setDownloadDirectory() stores it: empty means the
    // default folder.
    if (!downloadDirEdit_)
        return false;
    if (selectedLayout() != settings_.downloadLayout())
        return true;
    const QString text = downloadDirEdit_->text().trimmed();
    const QString effective = text.isEmpty() ? Config::Settings::defaultDownloadDirectory() : text;
    return QDir::cleanPath(effective) != QDir::cleanPath(settings_.downloadDirectory());
}

void DownloadsPage::apply()
{
    if (!downloadDirEdit_)
        return;
    settings_.setDownloadDirectory(downloadDirEdit_->text());
    settings_.setDownloadLayout(selectedLayout());
}

} // namespace Ui::Settings
