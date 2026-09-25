#pragma once

#include <QDialog>
#include <QModelIndex>

#include "Coro.h"

#include <vector>

class QListView;
class QPushButton;
class QStandardItemModel;

namespace Config {
class Settings;
}

namespace Rpc {
class AuthStates;
class SourceManager;
} // namespace Rpc

namespace Ui {

class NavItemDelegate;
class ToastNotifier;

namespace Settings {
class Page;
class PageStack;
} // namespace Settings

// Settings… from the hamburger menu: a sidebar of setting groups on the
// left, every group as one section of a single scrolling column on the
// right (Settings::PageStack — built lazily as it scrolls), Ok/Apply/
// Cancel below. Custom global-hotkey rebinding UI is not built yet
// (Integration::GlobalShortcuts uses a fixed default binding set for now) —
// hardware media keys work regardless via MPRIS, which doesn't need any
// UI here.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `openAt`: a Settings::Page::id() to show first instead of the top.
    SettingsDialog(Config::Settings& settings, Rpc::SourceManager& sourceManager, Rpc::AuthStates& authStates,
        QWidget* parent = nullptr, const QString& openAt = { });

    void done(int result) override;

    // The dialog's own toasts, for whatever happens while it's open — a
    // modal dialog would otherwise hide the main window's behind it.
    ToastNotifier* toastNotifier() const { return toastNotifier_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void addPage(Settings::Page* page);
    // Applies every dirty page in turn; false if any refused.
    Rpc::Task<bool> applyAllAsync();
    Rpc::Task<void> acceptAsync();
    void updateApplyButton();
    void updateSidebarHover(const QModelIndex& index);

    Config::Settings& settings_;
    std::vector<Settings::Page*> pages_;
    // pages_[i]'s row in sidebarModel_.
    std::vector<int> sidebarRowForPage_;
    QString lastSidebarSection_;
    QListView* sidebarView_ = nullptr;
    QStandardItemModel* sidebarModel_ = nullptr;
    NavItemDelegate* sidebarDelegate_ = nullptr;
    Settings::PageStack* pageStack_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QWidget* buttons_ = nullptr;
    bool applying_ = false;
    ToastNotifier* toastNotifier_ = nullptr;
    // Set while the sidebar's selection follows the column's scrolling, so
    // that doesn't bounce back as a jump request.
    bool syncingSidebar_ = false;
};

} // namespace Ui
