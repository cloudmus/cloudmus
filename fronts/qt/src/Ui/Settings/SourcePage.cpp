#include "Settings/SourcePage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "AuthCard.h"
#include "AuthStates.h"
#include "RpcMethods.h"
#include "Settings.h"
#include "Settings/SettingsForm.h"
#include "SourceManager.h"
#include "Spacing.h"
#include "ToastNotifier.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {

QLabel* makeHint(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("hint", true);
    label->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    label->setWordWrap(true);
    return label;
}

QPushButton* makeButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setProperty("variant", "secondary");
    button->setFont(Theme::font(Theme::TextStyle::Button));
    button->hide();
    return button;
}

} // namespace

SourcePage::SourcePage(Config::Settings& settings, Rpc::SourceManager& sourceManager, Rpc::AuthStates& authStates,
    ToastNotifier& toasts, Rpc::BackendManifest manifest, QObject* parent)
    : Page(parent)
    , settings_(settings)
    , sourceManager_(sourceManager)
    , authStates_(authStates)
    , toasts_(toasts)
    , manifest_(std::move(manifest))
{
}

QString SourcePage::sidebarSection() const { return tr("Sources"); }

QWidget* SourcePage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    widget_ = widget;

    descriptionLabel_ = makeHint(QString(), widget);

    enabledCheck_ = new QCheckBox(tr("Enabled"), widget);
    enabledCheck_->setFont(Theme::font(Theme::TextStyle::Body));
    enabledCheck_->setChecked(sourceManager_.isEnabled(manifest_.id));
    connect(enabledCheck_, &QCheckBox::toggled, this, &Page::dirtyChanged);

    offHint_ = makeHint(QString(), widget);

    accountSection_ = new QWidget(widget);
    auto* accountTitle = new QLabel(tr("Account").toUpper(), accountSection_);
    accountTitle->setProperty("hint", true);
    accountTitle->setFont(Theme::font(Theme::TextStyle::LabelUpper));
    statusLabel_ = new QLabel(accountSection_);
    statusLabel_->setFont(Theme::font(Theme::TextStyle::Body));
    statusLabel_->setWordWrap(true);
    signOutButton_ = makeButton(tr("Sign out"), accountSection_);
    cancelButton_ = makeButton(tr("Cancel"), accountSection_);
    connect(signOutButton_, &QPushButton::clicked, this, [this]() { signOutAsync().detach(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() { cancelSignInAsync().detach(); });

    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(Theme::Spacing::space3);
    statusRow->addWidget(statusLabel_, 1);
    statusRow->addWidget(signOutButton_);
    statusRow->addWidget(cancelButton_);

    authCard_ = new AuthCard(AuthCard::Look::Embedded, accountSection_);
    authCard_->hide();
    connect(authCard_, &AuthCard::submitRequested, this,
        [this](const QJsonObject& fields) { submitAsync(fields).detach(); });
    connect(authCard_, &AuthCard::retryRequested, this, [this]() { signInAsync().detach(); });
    connect(authCard_, &AuthCard::codeCopied, this, [this]() { toasts_.showInfo(tr("Code copied")); });

    auto* accountLayout = new QVBoxLayout(accountSection_);
    accountLayout->setContentsMargins(0, Theme::Spacing::space3, 0, 0);
    accountLayout->setSpacing(Theme::Spacing::space2);
    accountLayout->addWidget(accountTitle);
    accountLayout->addLayout(statusRow);
    accountLayout->addWidget(authCard_);

    settingsSection_ = new QWidget(widget);
    settingsHint_ = makeHint(QString(), settingsSection_);
    auto* settingsLayout = new QVBoxLayout(settingsSection_);
    settingsLayout->setContentsMargins(0, Theme::Spacing::space3, 0, 0);
    settingsLayout->setSpacing(Theme::Spacing::space2);
    settingsLayout->addWidget(settingsHint_);
    settingsSection_->hide();

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space2);
    layout->addWidget(descriptionLabel_);
    layout->addWidget(enabledCheck_);
    layout->addWidget(offHint_);
    layout->addWidget(accountSection_);
    layout->addWidget(settingsSection_);

    const auto refreshIfOurs = [this](const QString& sourceId) {
        if (sourceId == manifest_.id)
            refresh();
    };
    connect(&sourceManager_, &Rpc::SourceManager::sourceStarting, widget,
        [refreshIfOurs](const Rpc::BackendManifest& manifest) { refreshIfOurs(manifest.id); });
    connect(&sourceManager_, &Rpc::SourceManager::sourceReady, widget,
        [refreshIfOurs](Rpc::RpcClient* client) { refreshIfOurs(client->sourceId()); });
    connect(&sourceManager_, &Rpc::SourceManager::sourceStopped, widget, refreshIfOurs);
    connect(&sourceManager_, &Rpc::SourceManager::sourceUnavailable, widget,
        [refreshIfOurs](const QString& manifestId, const QString&, const QStringList&) { refreshIfOurs(manifestId); });
    connect(&authStates_, &Rpc::AuthStates::changed, widget, refreshIfOurs);

    refresh();
    return widget;
}

void SourcePage::refresh()
{
    const Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    const bool running = client != nullptr && client->available();

    const QString description = running ? client->sourceDescription() : QString();
    descriptionLabel_->setText(description);
    descriptionLabel_->setVisible(!description.isEmpty());

    QString offText;
    if (!sourceManager_.isEnabled(manifest_.id))
        offText = tr("A switched-off source doesn't start and isn't shown in the sidebar.");
    else if (!running)
        offText = sourceManager_.isUnavailable(manifest_.id) ? tr("The source couldn't be started.") : tr("Starting…");
    offHint_->setText(offText);
    offHint_->setVisible(!offText.isEmpty());

    const bool hasSettings = running && client->capabilities().value(QStringLiteral("settings")).toBool();
    settingsSection_->setVisible(hasSettings);
    if (hasSettings && settingsClient_ != client) {
        settingsClient_ = client;
        loadSettingsAsync().detach();
    }

    const bool needsAuth = running
        && client->capabilities().value(QStringLiteral("auth")).toObject().value(QStringLiteral("required")).toBool();
    accountSection_->setVisible(needsAuth);
    if (!needsAuth)
        return;

    const Rpc::AuthStates::State state = authStates_.state(manifest_.id);
    if (!state.prompt.isEmpty()) {
        authCard_->showPrompt(state.prompt);
        authCard_->show();
        showStatus(tr("Signing in…"), cancelButton_);
    } else if (!state.errorMessage.isEmpty()) {
        authCard_->showError(state.errorMessage);
        authCard_->show();
        showStatus(tr("Couldn't sign in"), nullptr);
    } else {
        // Nothing specific known here — ask the source.
        refreshAuthStatusAsync().detach();
    }
}

void SourcePage::showStatus(const QString& text, QPushButton* action)
{
    statusLabel_->setText(text);
    statusLabel_->setVisible(!text.isEmpty());
    signOutButton_->setVisible(action == signOutButton_);
    cancelButton_->setVisible(action == cancelButton_);
}

Rpc::Task<void> SourcePage::refreshAuthStatusAsync()
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    const QPointer<QWidget> alive = widget_;
    QString status;
    QString detail;
    try {
        const GetStatusResult result = co_await Rpc::authGetStatus(*client);
        status = result.status;
        detail = result.detail.value_or(QString());
    } catch (const std::exception& e) {
        status = QStringLiteral("error");
        detail = QString::fromStdString(e.what());
    }
    // The dialog may be gone (and this page with it — nothing of `this`
    // may be touched then), or a prompt/error may have arrived meanwhile
    // (then refresh() already showed that instead).
    if (!alive)
        co_return;
    const Rpc::AuthStates::State state = authStates_.state(manifest_.id);
    if (!state.prompt.isEmpty() || !state.errorMessage.isEmpty())
        co_return;

    if (status == QStringLiteral("authenticated")) {
        authCard_->hide();
        showStatus(tr("Signed in"), signOutButton_);
    } else if (status == QStringLiteral("pending")) {
        authCard_->hide();
        showStatus(tr("Signing in…"), cancelButton_);
    } else if (status == QStringLiteral("error")) {
        authCard_->showError(detail.isEmpty() ? tr("Sign-in failed") : detail);
        authCard_->show();
        showStatus(tr("Couldn't sign in"), nullptr);
    } else {
        authCard_->showSignInNeeded();
        authCard_->show();
        showStatus(QString(), nullptr);
    }
}

Rpc::Task<void> SourcePage::signInAsync()
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    const QPointer<QWidget> alive = widget_;
    authCard_->setBusy(true);
    try {
        // The prompt, if the flow has one, arrives as auth/prompt —
        // through Rpc::AuthStates, into refresh().
        co_await Rpc::authStart(*client);
    } catch (const std::exception& e) {
        if (alive)
            authCard_->showError(QString::fromStdString(e.what()));
    }
    if (alive)
        authCard_->setBusy(false);
}

Rpc::Task<void> SourcePage::submitAsync(QJsonObject fields)
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    const QPointer<QWidget> alive = widget_;
    authCard_->setBusy(true);
    try {
        SubmitParams params;
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            params.fields.insert(it.key(), it.value().toString());
        // Success arrives as auth/statusChanged, like any other sign-in.
        co_await Rpc::authSubmit(*client, params);
    } catch (const std::exception& e) {
        if (alive)
            authCard_->showError(QString::fromStdString(e.what()));
    }
    if (alive)
        authCard_->setBusy(false);
}

Rpc::Task<void> SourcePage::signOutAsync()
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    const QPointer<QWidget> alive = widget_;
    // Copied out of `this`: recorded below even if this page is gone by
    // then — the rest of the app must learn the source signed out
    // (auth.logout sends no notification).
    Rpc::AuthStates& authStates = authStates_;
    const QString sourceId = manifest_.id;
    signOutButton_->setEnabled(false);
    QString failure;
    try {
        co_await Rpc::authLogout(*client);
    } catch (const std::exception& e) {
        failure = QString::fromStdString(e.what());
    }
    if (failure.isEmpty())
        authStates.setSignedOut(sourceId);
    if (!alive)
        co_return;
    signOutButton_->setEnabled(true);
    if (!failure.isEmpty())
        showStatus(tr("Couldn't sign out: %1").arg(failure), signOutButton_);
}

Rpc::Task<void> SourcePage::cancelSignInAsync()
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    // Copied out of `this`, which may be gone once the call returns.
    Rpc::AuthStates& authStates = authStates_;
    const QString sourceId = manifest_.id;
    try {
        co_await Rpc::authCancel(*client);
    } catch (const std::exception&) {
        // Best effort: the flow may have just finished or failed on its own.
    }
    authStates.setSignedOut(sourceId);
}

Rpc::Task<void> SourcePage::loadSettingsAsync()
{
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (client == nullptr)
        co_return;
    const QPointer<QWidget> alive = widget_;
    delete settingsForm_;
    settingsForm_ = nullptr;
    settingsHint_->setText(tr("Loading settings…"));
    settingsHint_->show();
    emit dirtyChanged();

    std::optional<SettingsDescription> description;
    QString failure;
    try {
        description = co_await Rpc::settingsDescribe(*client);
    } catch (const std::exception& e) {
        failure = QString::fromStdString(e.what());
    }
    // Gone, or superseded by a newer process's describe.
    if (!alive || settingsClient_ != client)
        co_return;
    if (!description) {
        settingsHint_->setText(tr("Couldn't load the settings: %1").arg(failure));
        co_return;
    }
    settingsHint_->hide();
    settingsForm_ = new SettingsForm(*description, settingsSection_);
    settingsSection_->layout()->addWidget(settingsForm_);
    connect(settingsForm_, &SettingsForm::changed, this, &Page::dirtyChanged);
}

bool SourcePage::isDirty() const
{
    if (!enabledCheck_)
        return false;
    return enabledCheck_->isChecked() != sourceManager_.isEnabled(manifest_.id)
        || (settingsForm_ != nullptr && settingsForm_->isDirty());
}

Rpc::Task<bool> SourcePage::apply()
{
    if (!isDirty())
        co_return true;

    // The source's own settings first — while it's still running, should
    // "Enabled" be switching it off in the same Apply.
    Rpc::RpcClient* client = sourceManager_.client(manifest_.id);
    if (settingsForm_ != nullptr && settingsForm_->isDirty() && client != nullptr && client->available()) {
        const QPointer<QWidget> alive = widget_;
        const QMap<QString, QJsonValue> values = settingsForm_->changedValues();
        settingsForm_->showError(QString(), QString());
        QString failedKey;
        QString failure;
        try {
            co_await Rpc::settingsUpdate(*client, UpdateParams { values });
        } catch (const Rpc::RpcCallException& e) {
            // docs/protocol.md §7.7: data.key/data.message name the field.
            failedKey = e.error().data.value(QStringLiteral("key")).toString();
            failure = e.error().data.value(QStringLiteral("message")).toString();
            if (failure.isEmpty())
                failure = e.error().message;
        } catch (const std::exception& e) {
            failure = QString::fromStdString(e.what());
        }
        if (!alive)
            co_return false;
        if (!failure.isEmpty()) {
            settingsForm_->showError(failedKey, failure);
            toasts_.showError(tr("%1: %2").arg(manifest_.name, failure));
            co_return false;
        }
        settingsForm_->markApplied(values);
        if (settingsForm_->needsRestart(values.keys()))
            sourceManager_.restart(manifest_.id);
        else
            sourceManager_.notifySettingsChanged(manifest_.id);
    }

    if (enabledCheck_->isChecked() == sourceManager_.isEnabled(manifest_.id))
        co_return true;
    const bool enabled = enabledCheck_->isChecked();
    // Keeps ids of backends that aren't installed right now: reinstalling
    // one shouldn't silently switch it back on.
    QStringList disabled = settings_.disabledSources();
    disabled.removeAll(manifest_.id);
    if (!enabled)
        disabled.append(manifest_.id);
    settings_.setDisabledSources(disabled);
    sourceManager_.setEnabled(manifest_.id, enabled);
    refresh();
    co_return true;
}

} // namespace Ui::Settings
