#include "SourcePanel.h"

#include <QDesktopServices>
#include <QFont>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "Models.h"
#include "PlaylistHeader.h"

namespace Ui {

namespace {
constexpr int kIconSize = 32;
constexpr int kCardMaxWidth = 520;
constexpr int kMultilineFieldMinHeight = 140;

QString joinNonEmpty(const QStringList& parts)
{
    QStringList kept;
    for (const QString& p : parts) {
        if (!p.isEmpty())
            kept.append(p);
    }
    return kept.join(QStringLiteral(" · "));
}
} // namespace

SourcePanel::SourcePanel(CoverArtCache* coverCache, QWidget* parent)
    : QWidget(parent)
{
    hero_ = new PlaylistHeader(coverCache, this);
    hero_->setPlayButtonVisible(false);

    capabilitiesLabel_ = new QLabel(this);
    capabilitiesLabel_->setWordWrap(true);
    // A real QPalette role via setForegroundRole(), not a stylesheet color
    // string — "muted secondary text" isn't one of the named roles the Qt
    // Style Sheets palette() function recognizes, but QPalette::PlaceholderText
    // is a real enum value the style already knows how to render, same
    // theme-following spirit as ToastNotifier's palette(...) roles.
    capabilitiesLabel_->setForegroundRole(QPalette::PlaceholderText);

    authCard_ = new QWidget(this);
    authCard_->setObjectName(QStringLiteral("sourceAuthCard"));
    // palette(...) roles, not hardcoded colors — this sits on a plain
    // background (unlike PlaylistHeader's cover-art overlay, which is the
    // deliberate exception), so it should follow system theme like
    // ToastNotifier does. See ToastNotifier.cpp for the same convention.
    authCard_->setStyleSheet(QStringLiteral("#sourceAuthCard { background: palette(base); "
                                             "border: 1px solid palette(mid); border-radius: 10px; }"));
    authCard_->setMaximumWidth(kCardMaxWidth);

    auto* iconLabel = new QLabel(authCard_);
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(kIconSize, kIconSize));

    messageLabel_ = new QLabel(authCard_);
    messageLabel_->setWordWrap(true);
    // Selectable + link-clickable for the deviceCode case; harmless for
    // plain error text (still just lets the user select/copy it).
    messageLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    messageLabel_->setOpenExternalLinks(true);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(iconLabel);
    headerRow->addWidget(messageLabel_, 1);

    codeLabel_ = new QLabel(authCard_);
    QFont codeFont(QStringLiteral("monospace"));
    codeFont.setBold(true);
    codeFont.setPointSize(codeFont.pointSize() + 3);
    codeLabel_->setFont(codeFont);
    codeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel_->hide();

    copyCodeButton_ = new QPushButton(tr("Copy code"), authCard_);
    connect(copyCodeButton_, &QPushButton::clicked, this,
            [this]() { QGuiApplication::clipboard()->setText(codeLabel_->text()); });
    copyCodeButton_->hide();

    formLayout_ = new QHBoxLayout;
    multilineFieldsLayout_ = new QVBoxLayout;

    submitButton_ = new QPushButton(tr("Submit"), authCard_);
    connect(submitButton_, &QPushButton::clicked, this, [this]() {
        QJsonObject fields;
        for (auto it = fieldEdits_.constBegin(); it != fieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->text());
        }
        for (auto it = multilineFieldEdits_.constBegin(); it != multilineFieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->toPlainText());
        }
        emit submitRequested(currentSourceId_, fields);
    });
    submitButton_->hide();

    openBrowserButton_ = new QPushButton(tr("Open Browser"), authCard_);
    connect(openBrowserButton_, &QPushButton::clicked, this,
            [this]() { QDesktopServices::openUrl(QUrl(pendingOAuthUrl_)); });
    openBrowserButton_->hide();

    retryButton_ = new QPushButton(tr("Retry"), authCard_);
    connect(retryButton_, &QPushButton::clicked, this, [this]() { emit retryRequested(currentSourceId_); });
    retryButton_->hide();

    busyIndicator_ = new QProgressBar(authCard_);
    busyIndicator_->setRange(0, 0); // indeterminate
    busyIndicator_->setFixedWidth(80);
    busyIndicator_->setMaximumHeight(6);
    busyIndicator_->hide();

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(copyCodeButton_);
    buttonRow->addLayout(formLayout_);
    buttonRow->addWidget(submitButton_);
    buttonRow->addWidget(openBrowserButton_);
    buttonRow->addWidget(retryButton_);
    buttonRow->addWidget(busyIndicator_);
    buttonRow->addStretch(1);

    auto* cardLayout = new QVBoxLayout(authCard_);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(10);
    cardLayout->addLayout(headerRow);
    cardLayout->addWidget(codeLabel_);
    cardLayout->addLayout(multilineFieldsLayout_);
    cardLayout->addLayout(buttonRow);
    authCard_->hide();

    // Left-anchored under the hero's own left-aligned text, not centered —
    // this card is a secondary section, not the whole panel's content.
    auto* authRow = new QHBoxLayout;
    authRow->addWidget(authCard_);
    authRow->addStretch(1);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(16, 12, 16, 16);
    content->setSpacing(10);
    content->addWidget(capabilitiesLabel_);
    content->addLayout(authRow);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(hero_);
    outer->addLayout(content);
    outer->addStretch(1);

    hide();
}

void SourcePanel::clearFormFields()
{
    qDeleteAll(fieldEdits_);
    fieldEdits_.clear();
    qDeleteAll(multilineFieldEdits_);
    multilineFieldEdits_.clear();
    QLayoutItem* item = nullptr;
    while ((item = formLayout_->takeAt(0)) != nullptr) {
        delete item;
    }
    while ((item = multilineFieldsLayout_->takeAt(0)) != nullptr) {
        delete item;
    }
}

QString SourcePanel::capabilitiesSummary(const QJsonObject& capabilities)
{
    const QJsonObject auth = capabilities.value(QStringLiteral("auth")).toObject();
    const QJsonObject browse = capabilities.value(QStringLiteral("browse")).toObject();
    QStringList parts;
    if (auth.value(QStringLiteral("required")).toBool())
        parts.append(tr("Sign-in required"));
    if (browse.value(QStringLiteral("playlists")).toBool())
        parts.append(tr("Playlists"));
    if (browse.value(QStringLiteral("likedTracks")).toBool())
        parts.append(tr("Liked Songs"));
    if (browse.value(QStringLiteral("radio")).toBool())
        parts.append(tr("Radio"));
    if (browse.value(QStringLiteral("search")).toBool())
        parts.append(tr("Search"));
    if (capabilities.value(QStringLiteral("download")).toBool())
        parts.append(tr("Downloads"));
    return joinNonEmpty(parts);
}

void SourcePanel::setSource(const QString& sourceId, const QString& sourceName, const QString& description,
                             const QJsonObject& capabilities)
{
    currentSourceId_ = sourceId;
    const std::optional<QString> playlistDescription
        = description.isEmpty() ? std::nullopt : std::optional<QString>(description);
    hero_->setPlaylist(Playlist { sourceId, sourceName, playlistDescription, std::nullopt, 0, QStringLiteral("source") });
    const QString summary = capabilitiesSummary(capabilities);
    capabilitiesLabel_->setText(summary);
    capabilitiesLabel_->setVisible(!summary.isEmpty());
    show();
}

void SourcePanel::resetAuthChrome()
{
    clearFormFields();
    codeLabel_->hide();
    codeLabel_->clear();
    copyCodeButton_->hide();
    submitButton_->hide();
    openBrowserButton_->hide();
    retryButton_->hide();
    setAuthActionBusy(false);
}

void SourcePanel::showPrompt(const QJsonObject& params)
{
    resetAuthChrome();

    const QString flow = params.value(QStringLiteral("flow")).toString();
    if (flow == QStringLiteral("deviceCode")) {
        const QString url = params.value(QStringLiteral("url")).toString();
        const QString code = params.value(QStringLiteral("code")).toString();
        messageLabel_->setTextFormat(Qt::RichText);
        messageLabel_->setText(
            tr("Open <a href=\"%1\">%2</a> and enter the code below").arg(url.toHtmlEscaped(), url.toHtmlEscaped()));
        codeLabel_->setText(code);
        codeLabel_->show();
        copyCodeButton_->show();
    } else if (flow == QStringLiteral("usernamePassword")) {
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        bool hasMultilineField = false;
        for (const QJsonValue& v : fields) {
            if (v.toObject().value(QStringLiteral("multiline")).toBool()) {
                hasMultilineField = true;
                break;
            }
        }
        messageLabel_->setTextFormat(Qt::PlainText);
        // A generic "no source-specific UI" form (docs/protocol.md §10.2)
        // can't know it's specifically a browser-headers paste, but a
        // multiline field is a strong enough signal to justify a more
        // useful instruction than a bare "Sign in" — the DevTools steps
        // aren't otherwise discoverable from the form alone.
        messageLabel_->setText(hasMultilineField
                ? tr("Open music.youtube.com in your browser while signed in, open DevTools → Network tab, "
                     "click any request to music.youtube.com, and paste its Request Headers below.")
                : tr("Sign in"));
        for (const QJsonValue& v : fields) {
            const QJsonObject field = v.toObject();
            const QString name = field.value(QStringLiteral("name")).toString();
            if (field.value(QStringLiteral("multiline")).toBool()) {
                // A pasted-blob field (e.g. raw browser request headers) —
                // a QLineEdit would technically still round-trip the text
                // correctly (Qt doesn't strip embedded newlines from a
                // paste, only their on-screen rendering), but squished onto
                // one visible line the user has no way to actually read or
                // verify what they pasted before submitting something this
                // sensitive.
                auto* edit = new QPlainTextEdit(authCard_);
                edit->setPlaceholderText(tr("Paste here"));
                edit->setMinimumHeight(kMultilineFieldMinHeight);
                multilineFieldsLayout_->addWidget(edit);
                multilineFieldEdits_.insert(name, edit);
            } else {
                auto* edit = new QLineEdit(authCard_);
                if (field.value(QStringLiteral("secret")).toBool()) {
                    edit->setEchoMode(QLineEdit::Password);
                }
                edit->setPlaceholderText(name);
                formLayout_->addWidget(edit);
                fieldEdits_.insert(name, edit);
            }
        }
        submitButton_->show();
    } else if (flow == QStringLiteral("oauthRedirect")) {
        pendingOAuthUrl_ = params.value(QStringLiteral("url")).toString();
        QDesktopServices::openUrl(QUrl(pendingOAuthUrl_));
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(tr("Continue in your browser"));
        openBrowserButton_->show();
    } else {
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(tr("Sign-in required"));
    }
    authCard_->show();
}

void SourcePanel::showError(const QString& message)
{
    resetAuthChrome();
    // PlainText, not the default AutoText: an arbitrary backend-supplied
    // error message shouldn't be interpreted as markup.
    messageLabel_->setTextFormat(Qt::PlainText);
    messageLabel_->setText(message);
    retryButton_->show();
    authCard_->show();
}

void SourcePanel::clearAuthSection()
{
    resetAuthChrome();
    authCard_->hide();
}

void SourcePanel::setAuthActionBusy(bool busy)
{
    submitButton_->setEnabled(!busy);
    retryButton_->setEnabled(!busy);
    busyIndicator_->setVisible(busy);
}

} // namespace Ui
