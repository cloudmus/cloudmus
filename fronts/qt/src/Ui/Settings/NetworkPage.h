#pragma once

#include <QList>
#include <QPointer>

#include "Settings.h"
#include "Settings/Page.h"

class QVBoxLayout;

namespace Rpc {
class SourceManager;
}

namespace Ui {
class ToastNotifier;
}

namespace Ui::Settings {

class ProxyEditor;
struct RestartRequests;

// The user's list of named proxies (HTTP or SOCKS5), which each source's
// page then picks from — some services are only reachable through one.
// Saving restarts the running sources whose proxy changed or went away.
class NetworkPage : public Page {
    Q_OBJECT

public:
    NetworkPage(Config::Settings& settings, Rpc::SourceManager& sourceManager, ToastNotifier& toasts,
        RestartRequests& restarts, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("network"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("public"); }
    int estimatedHeight() const override { return 120 + 190 * int(settings_.proxies().size()); }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

    // The list as currently edited — saved or not yet. Before
    // createWidget(), the saved one.
    QList<Config::ProxyConfig> draft() const;

signals:
    // Any edit, addition or removal: the sources' pages keep their
    // Connection choices in step with the list being edited.
    void draftChanged();

private:
    ProxyEditor* addEditor(const Config::ProxyConfig& proxy);

    Config::Settings& settings_;
    Rpc::SourceManager& sourceManager_;
    ToastNotifier& toasts_;
    RestartRequests& restarts_;

    QPointer<QWidget> widget_;
    QVBoxLayout* editorsLayout_ = nullptr;
    QList<ProxyEditor*> editors_;
};

} // namespace Ui::Settings
