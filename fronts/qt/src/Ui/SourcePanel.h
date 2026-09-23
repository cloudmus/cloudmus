#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "Models.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;
class QProgressBar;

namespace Ui {

class CoverArtCache;

// A backend's page, shown in PlaylistSheet (which supplies the header:
// back button, icon, name, description — see PlaylistSheet::showSource()).
// Three compact, top-aligned sections that scroll together:
//  - STATUS: "Connected", or the auth card (prompt / error + Retry) while
//    there's something to act on — showPrompt()/showError()/clearAuthSection().
//  - FEATURES: capability chips.
//  - PLAYLISTS: this source's playlists (click opens one), with Refresh.
//
// MainWindow caches auth state per source itself (see
// MainWindow::SourceAuthState) and calls setSource() once per selection,
// then showPrompt()/showError()/clearAuthSection() as that state changes —
// this widget holds no source-identity bookkeeping beyond currentSourceId_,
// needed only to stamp its outgoing signals.
class SourcePanel : public QWidget {
    Q_OBJECT

public:
    explicit SourcePanel(CoverArtCache* coverCache, QWidget* parent = nullptr);

    // Call once per source selection: renders the capability chips. Does
    // not touch the auth section — call showPrompt()/showError()/
    // clearAuthSection() separately (MainWindow does so right after, from
    // cached state), nor the playlists — see setPlaylists().
    void setSource(const QString& sourceId, const QJsonObject& capabilities);
    // `loading`: a fetch is in flight — shown instead of "No playlists"
    // while the list is still empty.
    void setPlaylists(const QList<Playlist>& playlists, bool loading);

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
    void playlistActivated(QString sourceId, Playlist playlist);
    void refreshRequested(QString sourceId);

private:
    void clearFormFields();
    // Hides every flow-specific auth-section widget from whatever the
    // previous showPrompt()/showError() call left visible — shared by both
    // entry points (and clearAuthSection(), which just stops there).
    void resetAuthChrome();
    void refreshPlaylistsSection();

    QString currentSourceId_;

    QWidget* statusRow_ = nullptr; // "✓ Connected"
    QHBoxLayout* chipsLayout_ = nullptr;
    QWidget* playlistRows_ = nullptr;
    QWidget* playlistsHint_ = nullptr;
    QToolButton* refreshButton_ = nullptr;
    QList<Playlist> playlists_;
    bool playlistsLoading_ = false;

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
