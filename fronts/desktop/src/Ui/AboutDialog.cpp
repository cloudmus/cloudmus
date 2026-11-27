#include "AboutDialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

#include "AudioPulse.h"
#include "CloudsBackdrop.h"
#include "CopyButton.h"
#include "DialogButtons.h"
#include "Icons.h"
#include "OverlayScrollBar.h"
#include "People.h"
#include "PulsingLogo.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "SpriteAnimation.h"
#include "StackTransition.h"
#include "TabOrder.h"
#include "Tokens.h"
#include "Typography.h"
#include "Version.h"

namespace Ui {

namespace {

constexpr int kLogoSide = 72;
// The width the dialog opens at; its height is whatever the layout needs
// at that width. Resizable from the window frame.
constexpr int kDialogWidth = 760;
// How long the window takes to the height of the tab picked: as long as the
// page takes to slide in (StackTransition), so they end together.
constexpr int kHeightMs = 260;

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

// A card for each one: the person icon, then their name, email and page, a line
// each; two to a row.
QWidget* peoplePage(const QList<About::Person>& people, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* grid = new QGridLayout(page);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(Theme::Spacing::space3);
    constexpr int kColumns = 2;
    for (int i = 0; i < people.size(); ++i) {
        const About::Person& person = people[i];
        auto* card = new QFrame(page);
        card->setObjectName(QStringLiteral("personCard")); // see StyleSheet.cpp's settingsBlock()
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(
            Theme::Spacing::space3, Theme::Spacing::space3, Theme::Spacing::space4, Theme::Spacing::space3);
        row->setSpacing(Theme::Spacing::space3);

        auto* icon = new QLabel(card);
        icon->setAlignment(Qt::AlignTop);
        Theme::followTheme(icon, [icon]() {
            icon->setPixmap(Theme::icon(QStringLiteral("person"), Theme::IconColor::Accent, 32).pixmap(32, 32));
        });
        row->addWidget(icon);

        auto* lines = new QVBoxLayout;
        lines->setSpacing(Theme::Spacing::space1);
        lines->addWidget(makeLabel(person.name.toHtmlEscaped(), Theme::TextStyle::Title, card));
        const auto addLink = [card, lines](const QString& href, const QString& text) {
            auto* label = makeLabel(link(href, text), Theme::TextStyle::BodySecondary, card);
            label->setTextFormat(Qt::RichText);
            label->setOpenExternalLinks(true);
            label->setWordWrap(false); // one short line — its minimum is then its real size
            lines->addWidget(label);
        };
        if (!person.email.isEmpty())
            addLink(QStringLiteral("mailto:") + person.email, person.email);
        if (!person.url.isEmpty())
            addLink(person.url, About::shownUrl(person.url));
        row->addLayout(lines, 1);

        grid->addWidget(card, i / kColumns, i % kColumns, Qt::AlignTop);
    }
    for (int column = 0; column < kColumns; ++column)
        grid->setColumnStretch(column, 1);
    grid->setRowStretch(int((people.size() + kColumns - 1) / kColumns), 1);
    return page;
}

} // namespace

AboutDialog::AboutDialog(ViewModel::AudioPulse& pulse, ViewModel::LogoSound& sound, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About CloudMus"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why
    heightAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    heightAnimation_.setDuration(kHeightMs);
    connect(&heightAnimation_, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& height) { resize(width(), height.toInt()); });
    connect(&heightAnimation_, &QVariantAnimation::finished, this, [this]() { updateMinimumHeight(); });
    // Modal however it's opened (not only via exec()): the rest of the app
    // stays blocked while it's up.
    setWindowModality(Qt::ApplicationModal);

    // --- header: logo, name, version (copyable)
    auto* logoLabel = new PulsingLogo(pulse, sound, kLogoSide, this);
    logoLabel->setObjectName(QStringLiteral("aboutLogo"));
    logoLabel->setToolTip(tr("Play the CloudMus sound"));

    QFont titleFont = Theme::font(Theme::TextStyle::Display);
    titleFont.setPixelSize(32);
    auto* titleLabel = new QLabel(QStringLiteral("CloudMus"), this);
    titleLabel->setFont(titleFont);

    const QString version = QStringLiteral(CLOUDMUS_VERSION);
    auto* versionLabel = makeLabel(
        tr("Version %1").arg(version), Theme::TextStyle::BodySecondary, this, QStringLiteral("secondaryLabel"));
    versionLabel->setWordWrap(false);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* copyButton = new CopyButton(this);
    copyButton->setToolTip(tr("Copy version"));
    copyButton->setSource([version]() { return version; });

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
    description_ = makeLabel(tr("Enjoy listening to your clouds"), Theme::TextStyle::Body, this);
    description_->setAlignment(Qt::AlignCenter);

    const QString projectLinks = QStringList {
        link(QStringLiteral("https://cloudmus.app"), QStringLiteral("cloudmus.app")),
        link(QStringLiteral("https://github.com/cloudmus/cloudmus"), QStringLiteral("GitHub")),
        link(QStringLiteral("https://t.me/cloudmus_app"), tr("Telegram chat")),
    }.join(QStringLiteral(" · "));
    auto* projectLabel = makeLabel(projectLinks, Theme::TextStyle::Body, this);
    projectLabel->setTextFormat(Qt::RichText);
    projectLabel->setOpenExternalLinks(true);
    projectLabel->setAlignment(Qt::AlignCenter);
    projectLabel->setWordWrap(false); // one short line — its minimum is then its real size

    // --- Authors / Contributors / Technologies / License tabs
    QWidget* authorsPage = peoplePage(About::authors(), this);
    authorsPage->setObjectName(QStringLiteral("authorsPage"));
    QWidget* contributorsPage = peoplePage(About::contributors(), this);
    contributorsPage->setObjectName(QStringLiteral("contributorsPage"));

    // The AI agents that wrote code for it, and what it's made with, each by
    // its logo (resources/technologies/); the agents' mascots, the ones they
    // have in their own apps (resources/agents/make_sprites.py), wave.
    auto* technologiesPage = new QWidget(this);
    auto* technologiesGrid = new QGridLayout(technologiesPage);
    technologiesGrid->setContentsMargins(0, 0, 0, 0);
    technologiesGrid->setHorizontalSpacing(Theme::Spacing::space4);
    technologiesGrid->setVerticalSpacing(Theme::Spacing::space2);
    struct Technology {
        QString logo; // an SVG, or a strip of frames
        bool tinted = false; // drawn in the text's color (a black logo, for the dark theme)
        int frames = 0; // a strip's
        int frameMs = 0;
        bool pixelArt = false;
        QString name;
        QString url;
        QString what;
    };
    const Technology technologies[] = {
        { .logo = QStringLiteral(":/about/clawd-waving.png"),
            .frames = 9,
            .frameMs = 220,
            .pixelArt = true,
            .name = QStringLiteral("Claude"),
            .url = QStringLiteral("https://claude.com/product/claude-code"),
            .what = tr("AI coding agent by Anthropic") },
        { .logo = QStringLiteral(":/about/codex-waving.png"),
            .frames = 10,
            .frameMs = 140,
            .name = QStringLiteral("Codex"),
            .url = QStringLiteral("https://openai.com/codex"),
            .what = tr("AI coding agent by OpenAI") },
        { .logo = QStringLiteral(":/about/qt.svg"),
            .name = QStringLiteral("Qt"),
            .url = QStringLiteral("https://www.qt.io"),
            .what = tr("The interface, on the desktop and on phones") },
        { .logo = QStringLiteral(":/about/mpv.svg"),
            .name = QStringLiteral("mpv"),
            .url = QStringLiteral("https://mpv.io"),
            .what = tr("Plays the music") },
        { .logo = QStringLiteral(":/about/cpp.svg"),
            .name = QStringLiteral("C++"),
            .url = QStringLiteral("https://isocpp.org"),
            .what = tr("The app itself") },
        { .logo = QStringLiteral(":/about/rust.svg"),
            .tinted = true,
            .name = QStringLiteral("Rust"),
            .url = QStringLiteral("https://www.rust-lang.org"),
            .what = tr("The music services' backends") },
        { .logo = QStringLiteral(":/about/python.svg"),
            .name = QStringLiteral("Python"),
            .url = QStringLiteral("https://www.python.org"),
            .what = tr("The music services' backends") },
    };
    // A picture's size, as the logo's is: what each is scaled to, kept to its
    // proportions.
    constexpr int kTechnologySide = 40;
    const QSize logoSize(kTechnologySide, kTechnologySide);
    for (int i = 0; i < int(std::size(technologies)); ++i) {
        const Technology& technology = technologies[i];
        QWidget* logo = nullptr;
        if (technology.frames > 0) {
            logo = new SpriteAnimation(technology.logo, technology.frames, technology.frameMs, logoSize,
                technology.pixelArt, technologiesPage);
            logo->setObjectName(QStringLiteral("agentMascot"));
        } else {
            auto* image = new QLabel(technologiesPage);
            image->setObjectName(QStringLiteral("technologyLogo"));
            image->setAlignment(Qt::AlignCenter);
            image->setFixedSize(logoSize); // a picture's size, see above
            const auto draw = [image, technology, logoSize]() {
                QPixmap pixmap = QIcon(technology.logo).pixmap(logoSize, image->devicePixelRatioF());
                if (technology.tinted) {
                    QPainter painter(&pixmap);
                    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    painter.fillRect(pixmap.rect(), Theme::palette().ink);
                }
                image->setPixmap(pixmap);
            };
            Theme::followTheme(image, draw);
            draw();
            logo = image;
        }
        auto* text = makeLabel(
            QStringLiteral("%1 · %2").arg(link(technology.url, technology.name), technology.what.toHtmlEscaped()),
            Theme::TextStyle::Body, technologiesPage);
        text->setTextFormat(Qt::RichText);
        text->setOpenExternalLinks(true);
        text->setWordWrap(false); // short lines — its minimum is then its real size
        technologiesGrid->addWidget(logo, i, 0);
        technologiesGrid->addWidget(text, i, 1);
    }
    technologiesGrid->setColumnStretch(1, 1);
    technologiesGrid->setRowStretch(int(std::size(technologies)), 1);

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
    pages->addWidget(authorsPage);
    pages->addWidget(contributorsPage);
    pages->addWidget(technologiesPage);
    pages->addWidget(licenseText);
    // Only the CURRENT page counts for the stack's size (it would otherwise
    // reserve room for its tallest page): hidden pages get an Ignored size
    // policy. The dialog then opens as tall as the short authors list
    // needs, and fits itself to whichever tab gets selected.
    // The tab picked slides in from its side as the one left slides out.
    transition_ = new StackTransition(pages);
    const auto showPage = [this, pages](int index) {
        // The page picked may need more than the window has: not for the layout
        // to settle by growing it at once (the minimum is what it'd go by), but
        // for animateHeightTo() to take it there.
        if (isVisible())
            setMinimumHeight(1);
        for (int i = 0; i < pages->count(); ++i) {
            pages->widget(i)->setSizePolicy(
                QSizePolicy::Preferred, i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        }
        transition_->switchTo(index, [this]() {
            if (layout() == nullptr || !isVisible())
                return -1; // the initial size is set at the end of the constructor
            layout()->activate();
            const int target = heightForWidth(width());
            animateHeightTo(target);
            return target;
        });
    };
    showPage(0);

    auto* tabRow = new QHBoxLayout;
    tabRow->setSpacing(Theme::Spacing::space4);
    auto* tabs = new QButtonGroup(this);
    const QStringList tabTitles { tr("Authors"), tr("Contributors"), tr("Technologies"), tr("License") };
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
    styleDialogButton(buttons->button(QDialogButtonBox::Close), "primary");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space6, Theme::Spacing::space5);
    root->setSpacing(Theme::Spacing::space2);
    root->addLayout(header);
    root->addSpacing(Theme::Spacing::space4);
    root->addWidget(description_);
    root->addWidget(projectLabel);
    root->addSpacing(Theme::Spacing::space4);
    root->addLayout(tabRow);
    root->addWidget(pages, 1); // takes any extra height when resized
    root->addSpacing(Theme::Spacing::space4);
    root->addWidget(buttons);
    chainTabOrder(this); // the license text is created before the tabs above it

    // Behind everything else, over the whole dialog; the clouds fly out of
    // the logo.
    backdrop_ = new CloudsBackdrop(pulse, sound, logoLabel, this);
    backdrop_->lower();
    backdrop_->setGeometry(rect());

    // Sized here, not left to show()'s adjustSize(): show() creates the
    // native window first, at QWidget's default 100×30, and on Wayland,
    // when the dialog opens on a screen with another scale than the one Qt
    // guessed (e.g. a 150% laptop panel next to a 100% monitor), the
    // screen change replays that stale 100×30 as a resize. At that width
    // the wrapped description made updateMinimumHeight() demand ~1000px,
    // and the dialog opened narrow and absurdly tall.
    resize(sizeHint());
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
    if (backdrop_ != nullptr)
        backdrop_->setGeometry(rect());
    // The layout has just put the stack in its place: the pages' snapshots
    // moving over it go with it.
    if (transition_ != nullptr)
        transition_->syncCanvas();
    // A narrower window wraps the description onto more lines. Not while it
    // moves to another height: the minimum is for where it's heading, and
    // would pull it there at once.
    if (heightAnimation_.state() != QAbstractAnimation::Running)
        updateMinimumHeight();
}

void AboutDialog::animateHeightTo(int height)
{
    {
        // Set up quietly: stopping one that's going says it's finished, and
        // new values on one that has run its course make it say it's at the
        // new end, at once — the window would jump there.
        const QSignalBlocker quiet(heightAnimation_);
        heightAnimation_.stop();
        heightAnimation_.setStartValue(this->height());
        heightAnimation_.setEndValue(height);
    }
    if (height == this->height()) {
        updateMinimumHeight();
        return;
    }
    // Any height is allowed on the way (1, not 0: an explicit minimum keeps the
    // layout from setting its own).
    setMinimumHeight(1);
    heightAnimation_.start();
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
