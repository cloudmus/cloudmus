#pragma once

#include <QPointer>

#include "BackendManifest.h"
#include "Coro.h"
#include "Settings.h"
#include "Settings/Page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace Config {
class Settings;
}

namespace Rpc {
class AuthStates;
class RpcClient;
class SourceManager;
} // namespace Rpc

namespace Ui {
class AuthCard;
class ToastNotifier;
} // namespace Ui

namespace Ui::Settings {

class SettingsForm;
struct RestartRequests;

// One backend's settings: what every source has — whether it runs, and
// (for one that needs it) its account: status, sign in, sign out — then
// whatever it describes itself (settings.describe, as a SettingsForm). Signing
// in or out happens right away; only "Enabled" waits for Apply. Stays
// current while shown: follows the source starting/stopping and its
// sign-in state (Rpc::AuthStates) changing, from here or anywhere else.
class SourcePage : public Page {
    Q_OBJECT

public:
    // `toasts`: the Settings dialog's own, so what this page reports shows
    // in the window the user is looking at.
    // `restarts`: where to leave "restart me" for once the whole Apply is
    // done (a new connection only takes effect in a fresh process).
    SourcePage(Config::Settings& settings, Rpc::SourceManager& sourceManager, Rpc::AuthStates& authStates,
        ToastNotifier& toasts, RestartRequests& restarts, Rpc::BackendManifest manifest, QObject* parent = nullptr);

    // The proxies Connection offers — NetworkPage's list as edited, saved
    // or not, so a proxy just added can be picked in the same go.
    void setProxyChoices(const QList<Config::ProxyConfig>& proxies);

    QString id() const override { return QStringLiteral("source:") + manifest_.id; }
    QString title() const override { return manifest_.name; }
    QString iconPath() const override { return manifest_.iconPath; }
    QString sidebarSection() const override;
    int estimatedHeight() const override { return 150; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

private:
    // Repaints everything from the source's current state; asks the
    // source for its auth status when there's nothing more specific
    // (a prompt, an error) to show.
    void refresh();
    Rpc::Task<void> refreshAuthStatusAsync();
    Rpc::Task<void> signInAsync();
    Rpc::Task<void> submitAsync(QJsonObject fields);
    Rpc::Task<void> signOutAsync();
    Rpc::Task<void> cancelSignInAsync();
    // Refills connectionCombo_ from proxyChoices_, keeping the selection
    // where it still exists.
    void rebuildConnectionChoices();
    QString selectedConnection() const;
    void updateConnectionHint();
    // The status line and its one action button.
    void showStatus(const QString& text, QPushButton* action);
    // Builds the form from settings.describe, for the source's current
    // process (once per process: a restarted one may describe itself anew).
    Rpc::Task<void> loadSettingsAsync();

    Config::Settings& settings_;
    Rpc::SourceManager& sourceManager_;
    Rpc::AuthStates& authStates_;
    ToastNotifier& toasts_;
    RestartRequests& restarts_;
    const Rpc::BackendManifest manifest_;

    // Null until createWidget(), and again once the dialog is gone —
    // coroutines resuming after that must not touch anything.
    QPointer<QWidget> widget_;
    QLabel* descriptionLabel_ = nullptr;
    QCheckBox* enabledCheck_ = nullptr;
    QComboBox* connectionCombo_ = nullptr;
    QLabel* connectionHint_ = nullptr;
    QWidget* connectionRow_ = nullptr;
    bool usesNetwork_ = true;
    QList<Config::ProxyConfig> proxyChoices_;
    // The saved choice names a proxy no longer in proxyChoices_.
    bool connectionOrphaned_ = false;
    QLabel* offHint_ = nullptr;
    QWidget* accountSection_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* signOutButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    AuthCard* authCard_ = nullptr;
    // Holds settingsForm_, or a "Loading…"/error line in its place.
    QWidget* settingsSection_ = nullptr;
    QLabel* settingsHint_ = nullptr;
    SettingsForm* settingsForm_ = nullptr;
    // The process settingsForm_ was described by.
    const Rpc::RpcClient* settingsClient_ = nullptr;
};

} // namespace Ui::Settings
