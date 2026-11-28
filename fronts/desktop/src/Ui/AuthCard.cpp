#include "AuthCard.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "CopyButton.h"
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
    // Selectable + link-clickable for the code method; harmless for
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

    copyCodeButton_ = new CopyButton(this);
    copyCodeButton_->setToolTip(tr("Copy code"));
    copyCodeButton_->setAccessibleName(tr("Copy code"));
    copyCodeButton_->setSource([this]() { return codeLabel_->text(); });
    copyCodeButton_->hide();

    auto* codeRow = new QHBoxLayout;
    codeRow->setSpacing(Theme::Spacing::space2);
    codeRow->addWidget(codeLabel_);
    codeRow->addWidget(copyCodeButton_);
    codeRow->addStretch(1);

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
    buttonRow->addLayout(formLayout_);
    buttonRow->addWidget(submitButton_);
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
    cardLayout->addLayout(codeRow);
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
    retryButton_->hide();
    setBusy(false);
}

void AuthCard::showPrompt(const Rpc::AuthStates::State& state)
{
    reset();

    const std::optional<AuthMethod> method = state.method({ QStringLiteral("code"), QStringLiteral("form") });
    if (!method) {
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(tr("Sign-in required"));
        return;
    }
    if (method->kind == QStringLiteral("code")) {
        const QString url = method->url.value_or(QString());
        messageLabel_->setTextFormat(Qt::RichText);
        // Accent-colored inline: a rich-text link otherwise takes the
        // palette's stock blue, foreign to the app's theme.
        const QString link = QStringLiteral("<a href=\"%1\" style=\"color: %2\">%1</a>")
                                 .arg(url.toHtmlEscaped(), Theme::palette().accent.name());
        // Worded as the mobile front's sign-in card.
        messageLabel_->setText(tr("Open %1 on any device and enter this code:").arg(link));
        codeLabel_->setText(method->code.value_or(QString()));
        codeLabel_->show();
        copyCodeButton_->show();
        qrCode_->setContent(url);
        qrCode_->setVisible(true);
    } else {
        const QList<AuthField> fields = method->fields.value_or(QList<AuthField>());
        const bool hasMultilineField = std::any_of(
            fields.cbegin(), fields.cend(), [](const AuthField& field) { return field.multiline.value_or(false); });
        // What to put in the fields is the source's to say — the method's
        // own message, below; a generic form can't know.
        messageLabel_->setTextFormat(Qt::PlainText);
        messageLabel_->setText(hasMultilineField ? tr("Sign in by pasting what's asked for below.") : tr("Sign in"));
        for (const AuthField& field : fields) {
            const QString caption = field.label.value_or(QString());
            if (field.multiline.value_or(false)) {
                // A pasted-blob field (e.g. raw browser request headers) —
                // a QLineEdit would technically still round-trip the text
                // correctly (Qt doesn't strip embedded newlines from a
                // paste, only their on-screen rendering), but squished onto
                // one visible line the user has no way to actually read or
                // verify what they pasted before submitting something this
                // sensitive.
                auto* edit = new QPlainTextEdit(this);
                edit->setPlaceholderText(caption.isEmpty() ? tr("Paste here") : caption);
                edit->setMinimumHeight(kMultilineFieldMinHeight);
                SmoothScroller::attach(edit);
                OverlayScrollBar::attach(edit);
                multilineFieldsLayout_->addWidget(edit);
                multilineFieldEdits_.insert(field.name, edit);
            } else {
                auto* edit = new QLineEdit(this);
                if (field.secret) {
                    addPasswordReveal(edit);
                }
                edit->setPlaceholderText(caption.isEmpty() ? field.name : caption);
                formLayout_->addWidget(edit);
                fieldEdits_.insert(field.name, edit);
            }
        }
        submitButton_->show();
        chainTabOrder(this); // the fields were created after the buttons
    }
    // The source's own instructions (docs/protocol.md §10), when it gives
    // any, over the generic wording above.
    const QString message = method->message.value_or(QString());
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
