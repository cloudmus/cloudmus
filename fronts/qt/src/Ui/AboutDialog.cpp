#include "AboutDialog.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "OverlayScrollBar.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"
#include "Version.h"

namespace Ui {

namespace {

constexpr int kLogoSide = 72;
// The width the dialog opens at; its height is whatever the layout needs
// at that width. Resizable from the window frame.
constexpr int kDialogWidth = 760;

QLabel* makeLabel(const QString& text, Theme::TextStyle style, QWidget* parent, const QString& objectName = QString())
{
    auto* label = new QLabel(text, parent);
    label->setFont(Theme::font(style));
    label->setWordWrap(true);
    if (!objectName.isEmpty())
        label->setObjectName(objectName); // colors: see StyleSheet.cpp's panelsBlock()
    return label;
}

QString link(const QString& href, const QString& text)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(href, Theme::palette().accent.name(), text.toHtmlEscaped());
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About CloudMus"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why
    // Modal however it's opened (not only via exec()): the rest of the app
    // stays blocked while it's up.
    setWindowModality(Qt::ApplicationModal);

    // --- header: logo, name, version (copyable)
    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(QIcon(QStringLiteral(":/icons/icons/logo.svg")).pixmap(kLogoSide, kLogoSide));
    logoLabel->setFixedSize(kLogoSide, kLogoSide);

    QFont titleFont = Theme::font(Theme::TextStyle::Display);
    titleFont.setPixelSize(32);
    auto* titleLabel = new QLabel(QStringLiteral("CloudMus"), this);
    titleLabel->setFont(titleFont);

    const QString version = QStringLiteral(CLOUDMUS_VERSION);
    auto* versionLabel = makeLabel(
        tr("Version %1").arg(version), Theme::TextStyle::BodySecondary, this, QStringLiteral("secondaryLabel"));
    versionLabel->setWordWrap(false);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* copyButton = new QToolButton(this);
    copyButton->setProperty("variant", "icon");
    copyButton->setProperty("filled", true);
    copyButton->setProperty("compact", true); // 20px circle — see StyleSheet.cpp's buttonsBlock()
    copyButton->setFixedSize(20, 20);
    copyButton->setIconSize(QSize(12, 12));
    copyButton->setToolTip(tr("Copy version"));
    const auto setCopyIcon = [copyButton](bool copied) {
        copyButton->setIcon(Theme::icon(copied ? QStringLiteral("check") : QStringLiteral("content_copy"),
            copied ? Theme::IconColor::Accent : Theme::IconColor::InkSecondary, 12));
    };
    setCopyIcon(false);
    connect(copyButton, &QToolButton::clicked, this, [copyButton, version, setCopyIcon]() {
        QGuiApplication::clipboard()->setText(version);
        // Brief confirmation right on the button.
        setCopyIcon(true);
        QTimer::singleShot(1500, copyButton, [setCopyIcon]() { setCopyIcon(false); });
    });

    auto* versionRow = new QHBoxLayout;
    versionRow->setSpacing(Theme::Spacing::space2);
    versionRow->addWidget(versionLabel);
    versionRow->addWidget(copyButton);
    versionRow->addStretch(1);

    // Logo beside the name + version, the group centered.
    auto* nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(0);
    nameColumn->addStretch(1);
    nameColumn->addWidget(titleLabel);
    nameColumn->addLayout(versionRow);
    nameColumn->addStretch(1);
    auto* header = new QHBoxLayout;
    header->setSpacing(Theme::Spacing::space4);
    header->addStretch(1);
    header->addWidget(logoLabel);
    header->addLayout(nameColumn);
    header->addStretch(1);

    // --- description
    description_
        = makeLabel(tr("A lightweight music player for your cloud music services. Every service is a pluggable "
                       "backend — a separate program speaking a small JSON-RPC protocol — so new ones can be "
                       "written in any programming language."),
            Theme::TextStyle::Body, this);
    description_->setAlignment(Qt::AlignCenter);

    // --- Authors / License tabs
    auto* authorsLabel = makeLabel(
        QStringLiteral("Vladislav Navrocky · %1<br>Claude · %2")
            .arg(link(QStringLiteral("mailto:navrocky.vlad@gmail.com"), QStringLiteral("navrocky.vlad@gmail.com")),
                tr("AI pair programmer by Anthropic")),
        Theme::TextStyle::Body, this);
    authorsLabel->setTextFormat(Qt::RichText);
    authorsLabel->setOpenExternalLinks(true);
    authorsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    authorsLabel->setWordWrap(false); // short lines — its minimum is then its real size

    // QTextEdit, not QPlainTextEdit: the latter's layout ignores the block
    // margins used below to space paragraphs.
    auto* licenseText = new QTextEdit(this);
    licenseText->setReadOnly(true);
    licenseText->setAcceptRichText(false);
    licenseText->setFont(Theme::font(Theme::TextStyle::Caption));
    QFile licenseFile(QStringLiteral(":/about/LICENSE"));
    if (licenseFile.open(QIODevice::ReadOnly)) {
        // LICENSE is hard-wrapped at 80 columns; rejoin each paragraph's
        // lines so it reflows to this narrow box instead of reading ragged.
        QStringList paragraphs = QString::fromUtf8(licenseFile.readAll()).split(QStringLiteral("\n\n"));
        for (QString& paragraph : paragraphs)
            paragraph = paragraph.simplified();
        // One block per paragraph with a small gap after it, rather than
        // blank lines between them — compact.
        licenseText->setPlainText(paragraphs.join(QLatin1Char('\n')));
        QTextBlockFormat paragraphFormat;
        paragraphFormat.setBottomMargin(Theme::Spacing::space2);
        QTextCursor cursor(licenseText->document());
        cursor.select(QTextCursor::Document);
        cursor.mergeBlockFormat(paragraphFormat);
    }
    licenseText->document()->setDocumentMargin(Theme::Spacing::space2);
    SmoothScroller::attach(licenseText);
    OverlayScrollBar::attach(licenseText);

    auto* pages = new QStackedWidget(this);
    pages->addWidget(authorsLabel);
    pages->addWidget(licenseText);
    // Only the CURRENT page counts for the stack's size (it would otherwise
    // reserve room for its tallest page): hidden pages get an Ignored size
    // policy. The dialog then opens as tall as the short authors list
    // needs, and fits itself to whichever tab gets selected.
    const auto showPage = [this, pages](int index) {
        for (int i = 0; i < pages->count(); ++i) {
            pages->widget(i)->setSizePolicy(
                QSizePolicy::Preferred, i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        }
        pages->setCurrentIndex(index);
        if (layout() == nullptr || !isVisible())
            return; // the initial size is set at the end of the constructor
        layout()->activate();
        updateMinimumHeight();
        resize(width(), heightForWidth(width()));
    };
    showPage(0);

    auto* tabRow = new QHBoxLayout;
    tabRow->setSpacing(Theme::Spacing::space4);
    auto* tabs = new QButtonGroup(this);
    const QStringList tabTitles { tr("Authors"), tr("License") };
    for (int i = 0; i < tabTitles.size(); ++i) {
        auto* tab = new QPushButton(tabTitles[i], this);
        tab->setProperty("variant", "tab"); // see StyleSheet.cpp's textButtonsBlock()
        tab->setFont(Theme::font(Theme::TextStyle::Button));
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tabs->addButton(tab, i);
        tabRow->addWidget(tab);
    }
    tabRow->addStretch(1);
    connect(tabs, &QButtonGroup::idClicked, this, showPage);

    // --- close
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* closeButton = buttons->button(QDialogButtonBox::Close);
    closeButton->setProperty("variant", "primary");
    closeButton->setFont(Theme::font(Theme::TextStyle::Button));
    // See SettingsDialog.cpp's identical call for why: some platform
    // themes inject a standard icon onto dialog buttons regardless of the
    // active QStyle.
    closeButton->setIcon(QIcon());
    // See SettingsDialog.cpp's identical call for why: QDialogButtonBox
    // appears to polish its standard buttons before "variant" is set
    // above, so the QSS rule depending on it never gets re-evaluated
    // without this.
    closeButton->style()->unpolish(closeButton);
    closeButton->style()->polish(closeButton);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space5);
    root->setSpacing(Theme::Spacing::space2);
    root->addLayout(header);
    root->addSpacing(Theme::Spacing::space4);
    root->addWidget(description_);
    root->addSpacing(Theme::Spacing::space4);
    root->addLayout(tabRow);
    root->addWidget(pages, 1); // takes any extra height when resized
    root->addSpacing(Theme::Spacing::space4);
    root->addWidget(buttons);
}

QSize AboutDialog::sizeHint() const
{
    // What Qt opens (and centers) the dialog at: this width, and as tall
    // as the layout needs at it — the layout's own hint would count the
    // word-wrapped description at its unwrapped-width guess.
    return { kDialogWidth, heightForWidth(kDialogWidth) };
}

void AboutDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    // A narrower window wraps the description onto more lines.
    updateMinimumHeight();
}

void AboutDialog::updateMinimumHeight()
{
    if (layout() == nullptr)
        return;
    // The layout's own minimum, except for the one height-for-width item:
    // Qt counts a word-wrapped label as a single line there, so add the
    // extra lines the description actually takes at its current width.
    // Measured at the width the layout gives it (it spans the layout's
    // full content width) — not description_->width(), which lags: this
    // runs from resizeEvent, before the layout has re-laid-out children.
    const QMargins margins = layout()->contentsMargins();
    const int descriptionWidth = width() - margins.left() - margins.right();
    const int wrappedExtra = description_->heightForWidth(descriptionWidth) - description_->minimumSizeHint().height();
    const int needed = layout()->totalMinimumSize().height() + qMax(0, wrappedExtra);
    if (minimumHeight() != needed)
        setMinimumHeight(needed);
}

} // namespace Ui
