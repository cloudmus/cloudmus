#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class QProgressBar;

namespace Ui {

class CoverArtCache;
class PlaylistHeader;

// Shown in the content area (replacing PlaylistHeader + the track list) when
// the user selects any source's sidebar header row — see
// SidebarModel::Kind::SourceHeader and MainWindow::showSourceStatusPanel().
// Unlike a real playlist, every source gets this, not just ones with an
// auth problem: a hero (cover/name/description, via an internally owned
// PlaylistHeader fed a synthetic Playlist — reusing its generated-cover
// machinery rather than reimplementing it) and a one-line capabilities
// summary are always shown; the auth section below (prompt/error/Retry) is
// the only conditional part, hidden unless there's actually something to
// act on.
//
// Replaces the old global AuthBanner (single currentSourceId_ slot, dropped
// events for any source but the most recently active one) and this
// session's first SourceStatusPanel iteration (whole panel was the auth
// section, so a problem-free source had nothing to show and the panel
// looked broken/empty). MainWindow caches auth state per source itself
// (see MainWindow::SourceAuthState) and calls setSource() once per
// selection, then showPrompt()/showError()/clearAuthSection() as that
// state changes — this widget holds no source-identity bookkeeping beyond
// currentSourceId_, needed only to stamp outgoing submitRequested/
// retryRequested signals.
class SourcePanel : public QWidget {
    Q_OBJECT

public:
    explicit SourcePanel(CoverArtCache* coverCache, QWidget* parent = nullptr);

    // Call once per source selection: renders the hero (name/description/
    // generated cover) and the capabilities summary line. Does not touch
    // the auth section — call showPrompt()/showError()/clearAuthSection()
    // separately (MainWindow does so right after, from cached state).
    void setSource(const QString& sourceId, const QString& sourceName, const QString& description,
                   const QJsonObject& capabilities);

    // Renders the source's last-known prompt (deviceCode/usernamePassword/
    // oauthRedirect, discriminated by params["flow"] — see
    // Rpc::RpcClient::onAuthPromptRaw's doc comment for why this is raw
    // JSON rather than a generated struct).
    void showPrompt(const QJsonObject& params);
    // Renders a plain error message plus a Retry button.
    void showError(const QString& message);
    // Hides the auth section entirely — nothing to act on (authenticated,
    // or auth not required at all).
    void clearAuthSection();
    // Disables whichever action button is currently relevant (Submit or
    // Retry — only one is ever visible at a time) and shows the shared
    // indeterminate progress indicator, so a click can't be repeated while
    // its RPC round-trip is in flight and the user sees something started.
    void setAuthActionBusy(bool busy);

signals:
    void submitRequested(QString sourceId, QJsonObject fields);
    void retryRequested(QString sourceId);

private:
    void clearFormFields();
    // Hides every flow-specific auth-section widget from whatever the
    // previous showPrompt()/showError() call left visible — shared by both
    // entry points (and clearAuthSection(), which just stops there).
    void resetAuthChrome();
    static QString capabilitiesSummary(const QJsonObject& capabilities);

    QString currentSourceId_;

    PlaylistHeader* hero_ = nullptr;
    QLabel* capabilitiesLabel_ = nullptr;

    QWidget* authCard_ = nullptr;
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
