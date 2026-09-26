#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <functional>

#include "Models.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;
class QProgressBar;

namespace Covers {
class CoverArtCache;
}

namespace Ui {

class AuthCard;

// A backend's page, shown in PlaylistSheet (which supplies the header:
// back button, icon, name, description — see PlaylistSheet::showSource()).
// Three compact, top-aligned sections that scroll together:
//  - STATUS: "Connected", or the auth card (prompt / error + Retry) while
//    there's something to act on — showPrompt()/showError()/clearAuthSection().
//  - FEATURES: capability chips.
//  - PLAYLISTS: this source's playlists (click opens one; the star puts
//    one in the sidebar or takes it out), with Refresh.
//
// MainWindow caches auth state per source itself (see
// MainWindow::SourceAuthState) and calls setSource() once per selection,
// then showPrompt()/showError()/clearAuthSection() as that state changes —
// this widget holds no source-identity bookkeeping beyond currentSourceId_,
// needed only to stamp its outgoing signals.
class SourcePanel : public QWidget {
    Q_OBJECT

public:
    explicit SourcePanel(Covers::CoverArtCache* coverCache, QWidget* parent = nullptr);

    // Call once per source selection: renders the capability chips. Does
    // not touch the auth section — call showPrompt()/showError()/
    // clearAuthSection() separately (MainWindow does so right after, from
    // cached state), nor the playlists — see setPlaylists().
    void setSource(const QString& sourceId, const QJsonObject& capabilities);
    // `loading`: a fetch is in flight — shown instead of "No playlists"
    // while the list is still empty.
    void setPlaylists(const QList<Playlist>& playlists, bool loading);
    // Whether a playlist is a sidebar favorite (SidebarModel::isFavorite()),
    // asked at paint time; call favoritesChanged() when the answer changes.
    void setFavoriteCheck(std::function<bool(const QString& sourceId, const QString& playlistId)> check);
    void favoritesChanged();

    // Renders the source's last-known prompt (deviceCode/usernamePassword/
    // oauthRedirect, discriminated by params["flow"] — see
    // Rpc::RpcClient::onAuthPromptRaw's doc comment for why this is raw
    // JSON rather than a generated struct).
    void showPrompt(const QJsonObject& params);
    // Renders a plain error message plus a Retry button.
    void showError(const QString& message);
    // Signed out on purpose: a "Sign in" button, no error.
    void showSignInNeeded();
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
    void favoriteToggled(QString sourceId, QString playlistId);
    void refreshRequested(QString sourceId);
    // "Settings…": the source's page in the Settings dialog.
    void settingsRequested(QString sourceId);
    // The sign-in card's "Copy code" was clicked.
    void codeCopied();

private:
    // Shows authCard_ in place of the "Connected" line.
    void showAuthCard();
    void refreshPlaylistsSection();

    QString currentSourceId_;

    QWidget* statusRow_ = nullptr; // "✓ Connected"
    QHBoxLayout* chipsLayout_ = nullptr;
    QWidget* playlistRows_ = nullptr;
    QWidget* playlistsHint_ = nullptr;
    QToolButton* refreshButton_ = nullptr;
    QList<Playlist> playlists_;
    bool playlistsLoading_ = false;
    std::function<bool(const QString&, const QString&)> favoriteCheck_;

    AuthCard* authCard_ = nullptr;
};

} // namespace Ui
