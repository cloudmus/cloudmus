#pragma once

#include <QPointer>

#include "BackendManifest.h"
#include "Coro.h"
#include "Settings/Page.h"

class QCheckBox;
class QLabel;
class QPushButton;

namespace Config {
class Settings;
}

namespace Rpc {
class AuthStates;
class SourceManager;
} // namespace Rpc

namespace Ui {
class AuthCard;
class ToastNotifier;
} // namespace Ui

namespace Ui::Settings {

// One backend's settings: what every source has — whether it runs, and
// (for one that needs it) its account: status, sign in, sign out. Signing
// in or out happens right away; only "Enabled" waits for Apply. Stays
// current while shown: follows the source starting/stopping and its
// sign-in state (Rpc::AuthStates) changing, from here or anywhere else.
class SourcePage : public Page {
    Q_OBJECT

public:
    // `toasts`: the Settings dialog's own, so what this page reports shows
    // in the window the user is looking at.
    SourcePage(Config::Settings& settings, Rpc::SourceManager& sourceManager, Rpc::AuthStates& authStates,
        ToastNotifier& toasts, Rpc::BackendManifest manifest, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("source:") + manifest_.id; }
    QString title() const override { return manifest_.name; }
    QString iconPath() const override { return manifest_.iconPath; }
    QString sidebarSection() const override;
    int estimatedHeight() const override { return 150; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    void apply() override;

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
    // The status line and its one action button.
    void showStatus(const QString& text, QPushButton* action);

    Config::Settings& settings_;
    Rpc::SourceManager& sourceManager_;
    Rpc::AuthStates& authStates_;
    ToastNotifier& toasts_;
    const Rpc::BackendManifest manifest_;

    // Null until createWidget(), and again once the dialog is gone —
    // coroutines resuming after that must not touch anything.
    QPointer<QWidget> widget_;
    QLabel* descriptionLabel_ = nullptr;
    QCheckBox* enabledCheck_ = nullptr;
    QLabel* offHint_ = nullptr;
    QWidget* accountSection_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* signOutButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    AuthCard* authCard_ = nullptr;
};

} // namespace Ui::Settings
