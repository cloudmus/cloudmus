#include "UpdateDialog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

#include "DialogButtons.h"
#include "Installer.h"
#include "OverlayScrollBar.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"
#include "UpdateChecker.h"
#include "UpdateDownloader.h"

namespace Ui {

namespace {

// "3 October 2026" — QLocale::LongFormat would add the weekday.
QString releaseDate(const QDate& date) { return QLocale().toString(date, QStringLiteral("d MMMM yyyy")); }

// Every release's notes since the running version, newest first, each
// under its own version.
QString joinedNotes(const Update::PendingUpdate& update)
{
    QString markdown;
    for (const Update::Release& release : update.releases) {
        markdown += QStringLiteral("**%1**").arg(release.name);
        if (release.published.isValid())
            markdown += QStringLiteral(" · %1").arg(releaseDate(release.published));
        markdown += QStringLiteral("\n\n");
        markdown += release.notes.isEmpty() ? QCoreApplication::translate("Ui::UpdateDialog", "No description.")
                                            : release.notes;
        markdown += QStringLiteral("\n\n");
    }
    return markdown;
}

QString timeLeft(int seconds)
{
    if (seconds < 0)
        return QString();
    if (seconds < 60)
        return QCoreApplication::translate("Ui::UpdateDialog", "about %n s left", nullptr, qMax(seconds, 1));
    return QCoreApplication::translate("Ui::UpdateDialog", "about %n min left", nullptr, (seconds + 59) / 60);
}

} // namespace

UpdateDialog::UpdateDialog(Update::UpdateChecker& checker, Update::PendingUpdate update, QWidget* parent)
    : QDialog(parent)
    , checker_(checker)
    , update_(std::move(update))
{
    setWindowTitle(tr("CloudMus Update"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why
    // Modal while it asks something (see setStep()); not during the long
    // download, when the player should stay usable.
    setAttribute(Qt::WA_DeleteOnClose);

    heading_ = new QLabel(this);
    heading_->setFont(Theme::font(Theme::TextStyle::Display));
    // No word wrap on the two labels: a wrapped QLabel's height depends on
    // its width, which QLayout can't turn into a window minimum — the
    // lines would overlap in a narrow window. Unwrapped, they set the
    // window's minimum width instead, and anything long (a reason, an
    // error) goes in the scrolling box below.
    heading_->setWordWrap(false);

    text_ = new QLabel(this);
    text_->setFont(Theme::font(Theme::TextStyle::Body));
    text_->setWordWrap(false);
    text_->setTextFormat(Qt::RichText); // the versions are in bold

    notes_ = new QTextBrowser(this);
    notes_->setOpenExternalLinks(true);
    notes_->setFont(Theme::font(Theme::TextStyle::Body));
    notes_->setFrameShape(QFrame::NoFrame); // only the stylesheet's border
    notes_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QPalette notesPalette = notes_->palette();
    notesPalette.setColor(QPalette::Link, Theme::palette().accent);
    notes_->setPalette(notesPalette);
    notes_->document()->setDocumentMargin(Theme::Spacing::space2);
    SmoothScroller::attach(notes_);
    OverlayScrollBar::attach(notes_);

    progressBar_ = new QProgressBar(this);
    progressBar_->setProperty("themed", true); // see StyleSheet.cpp's progressBarBlock()
    progressBar_->setTextVisible(false);
    progressBar_->setMaximumHeight(6); // a thin bar; it has no natural height of its own
    progressLabel_ = new QLabel(this);
    progressLabel_->setObjectName(QStringLiteral("secondaryLabel")); // colors: see StyleSheet.cpp's panelsBlock()
    progressLabel_->setFont(Theme::font(Theme::TextStyle::BodySecondary));

    buttons_ = new QDialogButtonBox(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space5);
    root->setSpacing(Theme::Spacing::space3);
    // The window's size limits follow the layout's. Everything but the
    // changes box has a fixed height (Maximum vertically: grows no further than its
    // hint), so without the box — the download — the window can't be
    // stretched vertically.
    root->setSizeConstraint(QLayout::SetMinAndMaxSize);
    for (QWidget* widget : { static_cast<QWidget*>(heading_), static_cast<QWidget*>(text_),
             static_cast<QWidget*>(progressBar_), static_cast<QWidget*>(progressLabel_) })
        widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    root->addWidget(heading_);
    root->addWidget(text_);
    root->addWidget(notes_, 1); // all the extra height goes here
    root->addWidget(progressBar_);
    root->addWidget(progressLabel_);
    root->addSpacing(Theme::Spacing::space3);
    root->addWidget(buttons_);

    showOffer();
}

void UpdateDialog::reject()
{
    if (downloader_ != nullptr && downloader_->isRunning())
        downloader_->cancel();
    if (!downloadedPath_.isEmpty()) {
        QFile::remove(downloadedPath_);
        downloadedPath_.clear();
    }
    QDialog::reject();
}

QString UpdateDialog::currentVersionText() const
{
    const QDate date = update_.currentPublished;
    return date.isValid() ? tr("%1 of %2").arg(checker_.currentVersion(), releaseDate(date))
                          : checker_.currentVersion();
}

void UpdateDialog::showOffer()
{
    const Update::Release& latest = update_.latest();
    const QString reason = Update::Installer::unavailableReason();
    setStep(Step::Offer, tr("A new version of CloudMus is available"),
        tr("Version <b>%1</b> is out — you have <b>%2</b>.")
            .arg(latest.name.toHtmlEscaped(), currentVersionText().toHtmlEscaped()));
    QString markdown = joinedNotes(update_);
    if (!reason.isEmpty())
        markdown.prepend(QStringLiteral("**%1**\n\n").arg(reason));
    notes_->setMarkdown(markdown);
    // Air above each release but the first: the Markdown import gives a
    // version line the same small gap as any paragraph.
    QStringList versionLines;
    for (const Update::Release& release : update_.releases)
        versionLines.append(release.name);
    QTextBlockFormat gap;
    gap.setTopMargin(Theme::Spacing::space5);
    QTextCursor cursor(notes_->document());
    if (!reason.isEmpty()) {
        // The warning is the first block: tinted, with a gap below it.
        QColor tint = Theme::palette().accent;
        tint.setAlphaF(0.18);
        QTextBlockFormat warning;
        warning.setBackground(tint);
        warning.setBottomMargin(Theme::Spacing::space5);
        cursor.setPosition(notes_->document()->begin().position());
        cursor.mergeBlockFormat(warning);
    }
    for (QTextBlock block = notes_->document()->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        const bool isVersionLine = !block.textList() && text != versionLines.value(0)
            && std::any_of(versionLines.cbegin(), versionLines.cend(),
                [&text](const QString& name) { return text == name || text.startsWith(name + QStringLiteral(" · ")); });
        if (isVersionLine) {
            cursor.setPosition(block.position());
            cursor.mergeBlockFormat(gap);
        }
    }

    QPushButton* skip = addButton(tr("Skip This Version"), "secondary");
    connect(skip, &QPushButton::clicked, this, [this]() {
        checker_.skip(update_.latest().name);
        reject();
    });
    if (reason.isEmpty()) {
        QPushButton* install = addButton(tr("Install"), "primary");
        connect(install, &QPushButton::clicked, this, &UpdateDialog::startDownload);
        install->setDefault(true);
    } else {
        QPushButton* open = addButton(tr("Open Release Page"), "primary");
        connect(open, &QPushButton::clicked, this, &UpdateDialog::openReleasePage);
        open->setDefault(true);
    }
}

void UpdateDialog::startDownload()
{
    const Update::Release& latest = update_.latest();
    setStep(Step::Downloading, tr("Downloading CloudMus %1").arg(latest.name), QString());
    progressBar_->setRange(0, 0); // until the size is known
    progressLabel_->setText(tr("Connecting…"));
    QPushButton* cancel = addButton(tr("Cancel"), "secondary");
    connect(cancel, &QPushButton::clicked, this, &UpdateDialog::reject);

    if (downloader_ == nullptr) {
        downloader_ = new Update::UpdateDownloader(*checker_.network(), this);
        connect(downloader_, &Update::UpdateDownloader::progress, this, &UpdateDialog::showProgress);
        connect(downloader_, &Update::UpdateDownloader::finished, this, [this](const QString& filePath) {
            // No second question: Install was already the answer.
            downloadedPath_ = filePath;
            install();
        });
        connect(downloader_, &Update::UpdateDownloader::failed, this, &UpdateDialog::showFailure);
    }
    downloader_->start(latest.assetUrl, latest.assetSize, Update::Installer::downloadPath(latest));
}

void UpdateDialog::showProgress(qint64 received, qint64 total, int secondsLeft)
{
    const QLocale locale;
    QString text;
    if (total > 0) {
        // Per mille: a bar's range is an int, and a file may not fit one.
        progressBar_->setRange(0, 1000);
        progressBar_->setValue(int(received * 1000 / total));
        text = tr("%1 of %2").arg(locale.formattedDataSize(received), locale.formattedDataSize(total));
    } else {
        text = locale.formattedDataSize(received);
    }
    const QString left = timeLeft(secondsLeft);
    if (!left.isEmpty())
        text += QStringLiteral(" · ") + left;
    progressLabel_->setText(text);
}

void UpdateDialog::showFailure(const QString& error)
{
    setStep(Step::Failed, tr("The update didn't work out"), QString());
    notes_->setPlainText(tr("%1\n\nYou can try again later or download the new version yourself.").arg(error));
    QPushButton* open = addButton(tr("Open Release Page"), "secondary");
    connect(open, &QPushButton::clicked, this, &UpdateDialog::openReleasePage);
    QPushButton* close = addButton(tr("Close"), "primary");
    connect(close, &QPushButton::clicked, this, &UpdateDialog::reject);
    close->setDefault(true);
}

void UpdateDialog::install()
{
    QString error;
    const QString path = downloadedPath_;
    // From here on the file belongs to the installer, whatever happens.
    downloadedPath_.clear();
    if (!Update::Installer::launch(path, update_.latest(), &error)) {
        QFile::remove(path);
        showFailure(error);
        return;
    }
    emit quitRequested();
    accept();
}

void UpdateDialog::openReleasePage()
{
    // The dialog stays open: the desktop's portal opens the page for this
    // window asynchronously, and closing (deleting) it right away can
    // cancel the request before the browser has been asked.
    const QUrl url = update_.latest().pageUrl;
    if (!QDesktopServices::openUrl(url)) {
        qWarning() << "Update: couldn't open" << url;
        showFailure(tr("Couldn't open %1 in a browser").arg(url.toString()));
    }
}

void UpdateDialog::setStep(Step step, const QString& heading, const QString& text)
{
    const Qt::WindowModality modality = step == Step::Downloading ? Qt::NonModal : Qt::ApplicationModal;
    if (modality != windowModality()) {
        // A shown window takes a new modality only when shown again.
        const bool shown = isVisible();
        if (shown)
            hide();
        setWindowModality(modality);
        if (shown)
            show();
    }
    heading_->setText(heading);
    text_->setText(text);
    text_->setVisible(!text.isEmpty());
    notes_->setVisible(step == Step::Offer || step == Step::Failed);
    progressBar_->setVisible(step == Step::Downloading);
    progressLabel_->setVisible(step == Step::Downloading);
    // Not clear(), which deletes them at once: this may run from one's
    // own click.
    for (QAbstractButton* button : buttons_->buttons()) {
        buttons_->removeButton(button);
        button->hide();
        button->deleteLater();
    }
    layout()->activate();
    adjustSize(); // to the layout's hint
}

QPushButton* UpdateDialog::addButton(const QString& text, const char* variant)
{
    // ActionRole for all: their order is the order added, not the
    // platform's idea of where a Cancel or an OK goes.
    QPushButton* button = buttons_->addButton(text, QDialogButtonBox::ActionRole);
    styleDialogButton(button, variant);
    return button;
}

} // namespace Ui
