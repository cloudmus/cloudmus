#pragma once

#include <QHash>
#include <QJsonObject>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace Ui {

// A source's sign-in card: renders an auth/prompt (deviceCode's URL and
// code, usernamePassword's fields, oauthRedirect's browser hand-off —
// discriminated by params["flow"], see Rpc::RpcClient::onAuthPromptRaw's
// doc comment for why this is raw JSON rather than a generated struct), an
// error with Retry, or a plain "sign in" invitation. Holds no source
// identity: whoever shows it knows which source its signals are about.
// Used by SourcePanel and by the source's Settings page.
class AuthCard : public QWidget {
    Q_OBJECT

public:
    enum class Look {
        // A raised card of its own (surface-200, border, generous padding),
        // standing alone on a page — SourcePanel.
        Card,
        // No chrome or padding of its own, for a place that already frames
        // it — the source's card in the Settings dialog.
        Embedded,
    };

    explicit AuthCard(Look look, QWidget* parent = nullptr);

    void showPrompt(const QJsonObject& params);
    // A plain error message plus a Retry button.
    void showError(const QString& message);
    // Not signed in, nothing gone wrong: a "Sign in" button.
    void showSignInNeeded();
    // Disables whichever action button is currently relevant (Submit,
    // Retry or Sign in — only one is ever visible at a time) and shows an
    // indeterminate progress indicator, so a click can't be repeated while
    // its RPC round-trip is in flight and the user sees something started.
    void setBusy(bool busy);

signals:
    void submitRequested(QJsonObject fields);
    // Retry, or Sign in: either way, (re)start the sign-in flow.
    void retryRequested();
    // "Copy code" put the device code on the clipboard — for the owner to
    // confirm with a toast in its own window.
    void codeCopied();

private:
    void clearFormFields();
    // Hides every flow-specific widget whatever the previous show*() call
    // left visible.
    void reset();

    QLabel* messageLabel_ = nullptr;
    QLabel* codeLabel_ = nullptr;
    QPushButton* copyCodeButton_ = nullptr;
    QHBoxLayout* formLayout_ = nullptr;
    // Fields whose auth/prompt descriptor sets multiline:true (e.g. a
    // pasted-headers blob) — too long/unwieldy for the single-line fields'
    // QLineEdit-in-formLayout_ row, so they get a full-width QPlainTextEdit
    // of their own here instead. See showPrompt()'s usernamePassword branch.
    QVBoxLayout* multilineFieldsLayout_ = nullptr;
    QPushButton* submitButton_ = nullptr;
    QPushButton* openBrowserButton_ = nullptr;
    QPushButton* retryButton_ = nullptr;
    QProgressBar* busyIndicator_ = nullptr;
    QHash<QString, QLineEdit*> fieldEdits_;
    QHash<QString, QPlainTextEdit*> multilineFieldEdits_;
    // oauthRedirect's URL, kept so the manual "Open Browser" button can
    // re-open it if the automatic open-on-prompt was missed/blocked.
    QString pendingOAuthUrl_;
};

} // namespace Ui
