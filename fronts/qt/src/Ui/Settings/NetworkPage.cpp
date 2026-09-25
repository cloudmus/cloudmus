#include "Settings/NetworkPage.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <tuple>

#include "PasswordReveal.h"
#include "ProxyRouting.h"
#include "Settings/RestartRequests.h"
#include "SourceManager.h"
#include "Spacing.h"
#include "ToastNotifier.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {

// Answers 204 with an empty body; small and always up — a test of the
// proxy, not of any one music service.
const char kCheckUrl[] = "https://www.gstatic.com/generate_204";
constexpr int kCheckTimeoutMs = 10000;

QPushButton* makeButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setProperty("variant", "secondary");
    button->setFont(Theme::font(Theme::TextStyle::Button));
    return button;
}

QLabel* makeFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(Theme::font(Theme::TextStyle::Body));
    return label;
}

} // namespace

// One proxy's fields, in a bordered box of its own within the page's card.
// Plain callbacks rather than signals: it lives and dies with this page.
class ProxyEditor : public QFrame {
public:
    ProxyEditor(const Config::ProxyConfig& proxy, QWidget* parent)
        : QFrame(parent)
        , id_(proxy.id)
    {
        setObjectName(QStringLiteral("settingsSubCard")); // see StyleSheet.cpp's settingsBlock()

        nameEdit_ = new QLineEdit(proxy.name, this);
        nameEdit_->setPlaceholderText(tr("e.g. Home SOCKS"));
        typeCombo_ = new QComboBox(this);
        // See DownloadsPage: only a QStyledItemDelegate honors the ::item QSS.
        typeCombo_->setItemDelegate(new QStyledItemDelegate(typeCombo_));
        typeCombo_->addItem(QStringLiteral("HTTP"), int(Config::ProxyConfig::Type::Http));
        typeCombo_->addItem(QStringLiteral("SOCKS5"), int(Config::ProxyConfig::Type::Socks5));
        typeCombo_->setCurrentIndex(typeCombo_->findData(int(proxy.type)));
        hostEdit_ = new QLineEdit(proxy.host, this);
        hostEdit_->setPlaceholderText(tr("host or IP"));
        portSpin_ = new QSpinBox(this);
        portSpin_->setRange(1, 65535);
        portSpin_->setValue(proxy.port > 0 ? proxy.port : 1080);
        userEdit_ = new QLineEdit(proxy.username, this);
        userEdit_->setPlaceholderText(tr("optional"));
        passwordEdit_ = new QLineEdit(proxy.password, this);
        addPasswordReveal(passwordEdit_);
        passwordEdit_->setPlaceholderText(tr("optional"));
        for (QWidget* w :
            std::initializer_list<QWidget*> { nameEdit_, typeCombo_, hostEdit_, portSpin_, userEdit_, passwordEdit_ })
            w->setFont(Theme::font(Theme::TextStyle::Body));

        for (QLineEdit* edit : { nameEdit_, hostEdit_, userEdit_, passwordEdit_ })
            connect(edit, &QLineEdit::textChanged, this, [this]() { notifyChanged(); });
        connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this]() { notifyChanged(); });
        connect(portSpin_, &QSpinBox::valueChanged, this, [this]() { notifyChanged(); });

        checkButton_ = makeButton(tr("Check"), this);
        auto* removeButton = makeButton(tr("Remove"), this);
        connect(checkButton_, &QPushButton::clicked, this, [this]() { check(); });
        connect(removeButton, &QPushButton::clicked, this, [this]() {
            if (onRemove)
                onRemove();
        });

        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(Theme::Spacing::space3);
        grid->setVerticalSpacing(Theme::Spacing::space2);
        grid->addWidget(makeFieldLabel(tr("Name:"), this), 0, 0);
        grid->addWidget(nameEdit_, 0, 1);
        grid->addWidget(makeFieldLabel(tr("Type:"), this), 0, 2);
        grid->addWidget(typeCombo_, 0, 3);
        grid->addWidget(makeFieldLabel(tr("Host:"), this), 1, 0);
        grid->addWidget(hostEdit_, 1, 1);
        grid->addWidget(makeFieldLabel(tr("Port:"), this), 1, 2);
        grid->addWidget(portSpin_, 1, 3);
        grid->addWidget(makeFieldLabel(tr("Username:"), this), 2, 0);
        grid->addWidget(userEdit_, 2, 1);
        grid->addWidget(makeFieldLabel(tr("Password:"), this), 2, 2);
        grid->addWidget(passwordEdit_, 2, 3);
        grid->setColumnStretch(1, 3);
        grid->setColumnStretch(3, 2);

        auto* buttons = new QHBoxLayout;
        buttons->addStretch(1);
        buttons->addWidget(checkButton_);
        buttons->addWidget(removeButton);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Theme::Spacing::space3, Theme::Spacing::space3, Theme::Spacing::space3, Theme::Spacing::space3);
        layout->setSpacing(Theme::Spacing::space3);
        layout->addLayout(grid);
        layout->addLayout(buttons);
    }

    Config::ProxyConfig value() const
    {
        Config::ProxyConfig proxy;
        proxy.id = id_;
        proxy.name = nameEdit_->text().trimmed();
        proxy.type = Config::ProxyConfig::Type(typeCombo_->currentData().toInt());
        proxy.host = hostEdit_->text().trimmed();
        proxy.port = portSpin_->value();
        proxy.username = userEdit_->text();
        proxy.password = passwordEdit_->text();
        return proxy;
    }

    std::function<void()> onChanged;
    std::function<void()> onRemove;
    std::function<void(const QString& message, bool ok)> onChecked;

private:
    void notifyChanged()
    {
        if (onChanged)
            onChanged();
    }

    // One request through the proxy as currently entered, saved or not.
    void check()
    {
        const Config::ProxyConfig proxy = value();
        const QString name = proxy.name.isEmpty() ? proxy.host : proxy.name;
        if (proxy.host.isEmpty()) {
            if (onChecked)
                onChecked(tr("%1: enter the proxy's host first").arg(name), false);
            return;
        }
        auto* network = new QNetworkAccessManager(this);
        network->setProxy(Net::networkProxy(proxy));
        QNetworkRequest request { QUrl(QString::fromLatin1(kCheckUrl)) };
        request.setTransferTimeout(kCheckTimeoutMs);
        QNetworkReply* reply = network->get(request);
        checkButton_->setEnabled(false);
        connect(reply, &QNetworkReply::finished, this, [this, reply, network, name]() {
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString message = ok ? tr("%1 works").arg(name) : tr("%1: %2").arg(name, reply->errorString());
            checkButton_->setEnabled(true);
            network->deleteLater();
            if (onChecked)
                onChecked(message, ok);
        });
    }

    const QString id_;
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QPushButton* checkButton_ = nullptr;
};

NetworkPage::NetworkPage(Config::Settings& settings, Rpc::SourceManager& sourceManager, ToastNotifier& toasts,
    RestartRequests& restarts, QObject* parent)
    : Page(parent)
    , settings_(settings)
    , sourceManager_(sourceManager)
    , toasts_(toasts)
    , restarts_(restarts)
{
}

QString NetworkPage::title() const { return tr("Network"); }

QWidget* NetworkPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    widget_ = widget;

    auto* intro = new QLabel(tr("Proxies a source can connect through — choose one on the source's own page, "
                                "under Sources. Everything of that source goes through it: the source itself, "
                                "its streams and its covers."),
        widget);
    intro->setProperty("hint", true);
    intro->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    intro->setWordWrap(true);

    editorsLayout_ = new QVBoxLayout;
    editorsLayout_->setContentsMargins(0, 0, 0, 0);
    editorsLayout_->setSpacing(Theme::Spacing::space3);

    auto* addButton = makeButton(tr("Add proxy"), widget);
    connect(addButton, &QPushButton::clicked, this, [this]() {
        Config::ProxyConfig proxy;
        proxy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        proxy.port = 1080;
        addEditor(proxy);
        emit draftChanged();
        emit dirtyChanged();
    });

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space3);
    layout->addWidget(intro);
    layout->addLayout(editorsLayout_);
    layout->addWidget(addButton, 0, Qt::AlignLeft);

    for (const Config::ProxyConfig& proxy : settings_.proxies())
        addEditor(proxy);
    return widget;
}

ProxyEditor* NetworkPage::addEditor(const Config::ProxyConfig& proxy)
{
    auto* editor = new ProxyEditor(proxy, widget_);
    editor->onChanged = [this]() {
        emit draftChanged();
        emit dirtyChanged();
    };
    editor->onRemove = [this, editor]() {
        editors_.removeAll(editor);
        editor->deleteLater();
        emit draftChanged();
        emit dirtyChanged();
    };
    editor->onChecked = [this](const QString& message, bool ok) {
        if (ok)
            toasts_.showSuccess(message);
        else
            toasts_.showError(message);
    };
    editors_.append(editor);
    editorsLayout_->addWidget(editor);
    return editor;
}

QList<Config::ProxyConfig> NetworkPage::draft() const
{
    if (!widget_)
        return settings_.proxies();
    QList<Config::ProxyConfig> proxies;
    for (const ProxyEditor* editor : editors_)
        proxies.append(editor->value());
    return proxies;
}

bool NetworkPage::isDirty() const { return widget_ && draft() != settings_.proxies(); }

Rpc::Task<bool> NetworkPage::apply()
{
    const QList<Config::ProxyConfig> proxies = draft();
    for (const Config::ProxyConfig& proxy : proxies) {
        if (proxy.host.isEmpty()) {
            toasts_.showError(tr("Every proxy needs a host"));
            co_return false;
        }
    }

    // Sources going through a proxy that changed or went away have to
    // restart to pick that up (their environment is set at start).
    const QList<Config::ProxyConfig> before = settings_.proxies();
    QSet<QString> affected;
    for (const Config::ProxyConfig& old : before) {
        const auto now = std::find_if(
            proxies.begin(), proxies.end(), [&old](const Config::ProxyConfig& p) { return p.id == old.id; });
        // A new name alone changes nothing on the wire.
        const auto route
            = [](const Config::ProxyConfig& p) { return std::tie(p.type, p.host, p.port, p.username, p.password); };
        if (now == proxies.end() || route(*now) != route(old))
            affected.insert(old.id);
    }
    settings_.setProxies(proxies);
    for (const Rpc::BackendManifest& manifest : sourceManager_.manifests()) {
        if (affected.contains(settings_.sourceConnection(manifest.id)))
            restarts_.sourceIds.insert(manifest.id);
    }
    co_return true;
}

} // namespace Ui::Settings
