#include "AuthCard.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "Icons.h"
#include "MiniHtml.h"
#include "OverlayScrollBar.h"
#include "PasswordReveal.h"
#include "QrCodeView.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "TabOrder.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {
constexpr int kStandaloneIconSize = 32;
constexpr int kEmbeddedIconSize = 20;
constexpr int kMultilineFieldMinHeight = 140;
} // namespace

AuthCard::AuthCard(Look look, QWidget* parent)
    : QWidget(parent)
{
    const bool embedded = look == Look::Embedded;
    const int iconSize = embedded ? kEmbeddedIconSize : kStandaloneIconSize;

    auto* iconLabel = new QLabel(this);
    Theme::followTheme(iconLabel, [iconLabel, iconSize]() {
        iconLabel->setPixmap(
            Theme::icon(QStringLiteral("warning"), Theme::IconColor::Accent, iconSize).pixmap(iconSize, iconSize));
    });

    messageLabel_ = new QLabel(this);
    messageLabel_->setWordWrap(true);
    // Selectable + link-clickable for the deviceCode case; harmless for
    // plain error text (still just lets the user select/copy it).
    messageLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    messageLabel_->setOpenExternalLinks(true);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(iconLabel);
    headerRow->addWidget(messageLabel_, 1);

    codeLabel_ = new QLabel(this);
    QFont codeFont(QStringLiteral("monospace"));
    codeFont.setBold(true);
    codeFont.setPointSize(codeFont.pointSize() + 3);
    codeLabel_->setFont(codeFont);
    codeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel_->hide();

    copyCodeButton_ = new QPushButton(tr("Copy code"), this);
    copyCodeButton_->setProperty("variant", "secondary");
    copyCodeButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(copyCodeButton_, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(codeLabel_->text());
        emit codeCopied();
    });
    copyCodeButton_->hide();

    formLayout_ = new QHBoxLayout;
    multilineFieldsLayout_ = new QVBoxLayout;

    submitButton_ = new QPushButton(tr("Submit"), this);
    submitButton_->setProperty("variant", "primary");
    submitButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(submitButton_, &QPushButton::clicked, this, [this]() {
        QJsonObject fields;
        for (auto it = fieldEdits_.constBegin(); it != fieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->text());
        }
        for (auto it = multilineFieldEdits_.constBegin(); it != multilineFieldEdits_.constEnd(); ++it) {
            fields.insert(it.key(), it.value()->toPlainText());
        }
        emit submitRequested(fields);
    });
    submitButton_->hide();

    openBrowserButton_ = new QPushButton(tr("Open Browser"), this);
    openBrowserButton_->setProperty("variant", "secondary");
    openBrowserButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(openBrowserButton_, &QPushButton::clicked, this,
        [this]() { QDesktopServices::openUrl(QUrl(pendingOAuthUrl_)); });
    openBrowserButton_->hide();

    retryButton_ = new QPushButton(tr("Retry"), this);
    retryButton_->setProperty("variant", "secondary");
    retryButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(retryButton_, &QPushButton::clicked, this, [this]() { emit retryRequested(); });
    retryButton_->hide();

    busyIndicator_ = new QProgressBar(this);
    busyIndicator_->setProperty("themed", true); // see StyleSheet.cpp's progressBarBlock()
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

    // For opening the address on a phone rather than typing it.
    qrCode_ = new QrCodeView(this);
    qrCode_->hide();

    auto* cardLayout = new QVBoxLayout(this);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(embedded ? Theme::Spacing::space2 : 10);
    cardLayout->addLayout(headerRow);
    cardLayout->addWidget(codeLabel_);
    cardLayout->addLayout(multilineFieldsLayout_);
    cardLayout->addLayout(buttonRow);
    cardLayout->addWidget(qrCode_, 0, Qt::AlignLeft);
}

void AuthCard::clearFormFields()
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

void AuthCard::reset()
{
    clearFormFields();
    codeLabel_->hide();
    codeLabel_->clear();
    copyCodeButton_->hide();
    qrCode_->hide();
    submitButton_->hide();
    openBrowserButton_->hide();
    retryButton_->hide();
    setBusy(false);
}

void AuthCard::showPrompt(const QJsonObject& params)
{
    reset();

    const QString flow = params.value(QStringLiteral("flow")).toString();
    if (flow == QStringLiteral("deviceCode")) {
        const QString url = params.value(QStringLiteral("url")).toString();
        const QString code = params.value(QStringLiteral("code")).toString();
        messageLabel_->setTextFormat(Qt::RichText);
        // Accent-colored inline: a rich-text link otherwise takes the
        // palette's stock blue, foreign to the app's theme.
        const QString link = QStringLiteral("<a href=\"%1\" style=\"color: %2\">%1</a>")
                                 .arg(url.toHtmlEscaped(), Theme::palette().accent.name());
        // Worded as the mobile front's sign-in card.
        messageLabel_->setText(tr("Open %1 on any device and enter this code:").arg(link));
        codeLabel_->setText(code);
        codeLabel_->show();
        copyCodeButton_->show();
        qrCode_->setContent(url);
        qrCode_->setVisible(!url.isEmpty());
    } else if (flow == QStringLiteral("usernamePassword")) {
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        bool hasMultilineField = false;
        for (const QJsonValue& v : fields) {
            if (v.toObject().value(QStringLiteral("multiline")).toBool()) {
                hasMultilineField = true;
                break;
            }
        }
        // What to put in the fields is the source's to say — the prompt's
        // own message, below; a generic form can't know.
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(hasMultilineField ? tr("Sign in by pasting what's asked for below.") : tr("Sign in"));
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
                auto* edit = new QPlainTextEdit(this);
                edit->setPlaceholderText(tr("Paste here"));
                edit->setMinimumHeight(kMultilineFieldMinHeight);
                SmoothScroller::attach(edit);
                OverlayScrollBar::attach(edit);
                multilineFieldsLayout_->addWidget(edit);
                multilineFieldEdits_.insert(name, edit);
            } else {
                auto* edit = new QLineEdit(this);
                if (field.value(QStringLiteral("secret")).toBool()) {
                    addPasswordReveal(edit);
                }
                edit->setPlaceholderText(name);
                formLayout_->addWidget(edit);
                fieldEdits_.insert(name, edit);
            }
        }
        submitButton_->show();
        chainTabOrder(this); // the fields were created after the buttons
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
    // The source's own instructions (docs/protocol.md §10), when it gives
    // any, over the generic wording above.
    const QString message = params.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        messageLabel_->setTextFormat(Qt::RichText);
        messageLabel_->setText(miniHtmlToRichText(message, Theme::palette().accent));
    }
}

void AuthCard::showError(const QString& message)
{
    reset();
    // PlainText, not the default AutoText: an arbitrary backend-supplied
    // error message shouldn't be interpreted as markup.
    messageLabel_->setTextFormat(Qt::PlainText);
    messageLabel_->setText(message);
    retryButton_->setText(tr("Retry"));
    retryButton_->show();
}

void AuthCard::showSignInNeeded()
{
    reset();
    messageLabel_->setTextFormat(Qt::PlainText);
    messageLabel_->setText(tr("Not signed in"));
    retryButton_->setText(tr("Sign in"));
    retryButton_->show();
}

void AuthCard::setBusy(bool busy)
{
    submitButton_->setEnabled(!busy);
    retryButton_->setEnabled(!busy);
    busyIndicator_->setVisible(busy);
}

} // namespace Ui
