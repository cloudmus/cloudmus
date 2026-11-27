#pragma once

#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include "AuthStates.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace Ui {

class CopyButton;
class QrCodeView;

// A source's sign-in card: renders an auth/prompt (the first of its methods
// the desktop can carry out: `code` with its URL — as a QR code too — and
// code, or `form`'s fields; it has no built-in browser for `browser`), an
// error with Retry, or a plain "sign in" invitation. Holds no source
// identity: whoever shows it knows which source its signals are about.
// Used by SourcePanel and by the source's Settings page.
class AuthCard : public QWidget {
    Q_OBJECT

public:
    enum class Look {
        // Standing on a page of its own, with a larger icon — SourcePanel.
        Standalone,
        // Inside a frame that's already there — the source's card in the
        // Settings dialog.
        Embedded,
    };

    // Neither look has padding of its own: it lines up with what's around it.
    explicit AuthCard(Look look, QWidget* parent = nullptr);

    void showPrompt(const Rpc::AuthStates::State& state);
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

private:
    void clearFormFields();
    // Hides every method-specific widget whatever the previous show*() call
    // left visible.
    void reset();

    QLabel* messageLabel_ = nullptr;
    QLabel* codeLabel_ = nullptr;
    CopyButton* copyCodeButton_ = nullptr;
    QrCodeView* qrCode_ = nullptr;
    QHBoxLayout* formLayout_ = nullptr;
    // Fields marked multiline (e.g. a
    // pasted-headers blob) — too long/unwieldy for the single-line fields'
    // QLineEdit-in-formLayout_ row, so they get a full-width QPlainTextEdit
    // of their own here instead. See showPrompt()'s form branch.
    QVBoxLayout* multilineFieldsLayout_ = nullptr;
    QPushButton* submitButton_ = nullptr;
    QPushButton* retryButton_ = nullptr;
    QProgressBar* busyIndicator_ = nullptr;
    QHash<QString, QLineEdit*> fieldEdits_;
    QHash<QString, QPlainTextEdit*> multilineFieldEdits_;
};

} // namespace Ui
