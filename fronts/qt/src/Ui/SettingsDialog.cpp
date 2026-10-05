#include "SettingsDialog.h"

#include <algorithm>

#include <QCursor>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "DialogButtons.h"
#include "NavItemDelegate.h"
#include "OverlayScrollBar.h"
#include "ScrollEdgeFade.h"
#include "Settings.h"
#include "Settings/DownloadsPage.h"
#include "Settings/GeneralPage.h"
#include "Settings/HotkeysPage.h"
#include "Settings/NetworkPage.h"
#include "Settings/PageStack.h"
#include "Settings/SourcePage.h"
#include "SidebarModel.h"
#include "SmoothScroller.h"
#include "SourceManager.h"
#include "Spacing.h"
#include "TabOrder.h"
#include "ToastNotifier.h"
#include "Tokens.h"

namespace Ui {

namespace {
constexpr int kSidebarWidth = 200;
// A sidebar row's index into pages_ — rows and pages differ once section
// headings are in (see addPage()). Past ViewModel::SidebarModel's own roles.
constexpr int kPageIndexRole = Qt::UserRole + 100;
} // namespace

SettingsDialog::SettingsDialog(Config::Settings& settings, App::Analytics& analytics, Rpc::SourceManager& sourceManager,
    Rpc::AuthStates& authStates, ViewModel::Downloads& downloads, Hotkeys::Registry& hotkeys, QWidget* parent,
    const QString& openAt)
    : QDialog(parent)
    , settings_(settings)
    , sourceManager_(sourceManager)
{
    setWindowTitle(tr("Settings"));
    setProperty("themed", true); // see StyleSheet.cpp's dialogsBlock() for why
    setMinimumSize(640, 420);
    if (!restoreGeometry(settings_.settingsDialogGeometry()))
        resize(820, 600);

    // --- sidebar
    sidebarModel_ = new QStandardItemModel(this);
    sidebarView_ = new QListView(this);
    sidebarView_->setObjectName(QStringLiteral("settingsSidebar")); // see StyleSheet.cpp's settingsBlock()
    sidebarView_->setModel(sidebarModel_);
    sidebarDelegate_ = new NavItemDelegate(sidebarView_);
    sidebarView_->setItemDelegate(sidebarDelegate_);
    sidebarView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sidebarView_->setSelectionMode(QAbstractItemView::SingleSelection);
    sidebarView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    sidebarView_->setFixedWidth(kSidebarWidth);
    SmoothScroller::attach(sidebarView_);
    // Before OverlayScrollBar::attach — see ScrollEdgeFade's class doc.
    ScrollEdgeFade::attach(sidebarView_, [] { return Theme::palette().surface100; });
    OverlayScrollBar::attach(sidebarView_);
    // Hover driven from real mouse moves and scrolling, as in MainWindow's
    // sidebar — see NavItemDelegate's class doc for why not State_MouseOver.
    sidebarView_->viewport()->setMouseTracking(true);
    sidebarView_->viewport()->installEventFilter(this);
    connect(sidebarView_->verticalScrollBar(), &QScrollBar::valueChanged, sidebarView_, [this]() {
        const QPoint viewportPos = sidebarView_->viewport()->mapFromGlobal(QCursor::pos());
        const bool inside = sidebarView_->viewport()->rect().contains(viewportPos);
        updateSidebarHover(inside ? sidebarView_->indexAt(viewportPos) : QModelIndex());
    });

    // --- pages
    pageStack_ = new Settings::PageStack(this);
    // Over the settings column, clear of the Ok/Apply/Cancel row.
    toastNotifier_ = new ToastNotifier(pageStack_);
    addPage(new Settings::GeneralPage(settings_, analytics, this));
    addPage(new Settings::DownloadsPage(settings_, downloads, this));
    addPage(new Settings::HotkeysPage(hotkeys, this));
    // Ahead of the sources: applied first, so a proxy added and picked by
    // a source in the same Apply exists by the time the source saves it.
    auto* network = new Settings::NetworkPage(settings_, sourceManager, *toastNotifier_, restarts_, this);
    addPage(network);
    for (const Rpc::BackendManifest& manifest : sourceManager.manifests()) {
        auto* page = new Settings::SourcePage(
            settings_, sourceManager, authStates, *toastNotifier_, restarts_, manifest, this);
        connect(network, &Settings::NetworkPage::draftChanged, page,
            [page, network]() { page->setProxyChoices(network->draft()); });
        addPage(page);
    }

    // currentChanged, not clicked: arrow keys in the sidebar jump too.
    connect(
        sidebarView_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
            if (!syncingSidebar_ && current.isValid())
                pageStack_->scrollToPage(current.data(kPageIndexRole).toInt());
        });
    // Also on a click of the row that's already current: its section may
    // be only partly in view.
    connect(sidebarView_, &QListView::clicked, this,
        [this](const QModelIndex& index) { pageStack_->scrollToPage(index.data(kPageIndexRole).toInt()); });
    connect(pageStack_, &Settings::PageStack::currentPageChanged, this, [this](int index) {
        syncingSidebar_ = true;
        sidebarView_->setCurrentIndex(sidebarModel_->index(sidebarRowForPage_[index], 0));
        syncingSidebar_ = false;
    });

    // --- buttons
    auto* buttons
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    styleDialogButton(buttons->button(QDialogButtonBox::Ok), "primary");
    styleDialogButton(buttons->button(QDialogButtonBox::Apply), "secondary");
    styleDialogButton(buttons->button(QDialogButtonBox::Cancel), "secondary");
    applyButton_ = buttons->button(QDialogButtonBox::Apply);
    buttons_ = buttons;
    connect(applyButton_, &QPushButton::clicked, this, [this]() { applyAllAsync().detach(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { acceptAsync().detach(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateApplyButton();

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(sidebarView_);
    body->addWidget(pageStack_, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(
        Theme::Spacing::space4, Theme::Spacing::space3, Theme::Spacing::space4, Theme::Spacing::space3);
    buttonRow->addWidget(buttons);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(body, 1);
    root->addLayout(buttonRow);

    // Pages are built lazily and their forms load later, all after the
    // buttons: left alone, Tab would reach Ok/Cancel before the settings.
    // Sidebar, pages in order, then the buttons.
    const auto chainTabs = [this]() {
        QWidget* last = chainTabOrderAfter(sidebarView_, pageStack_);
        chainTabOrderAfter(last, buttons_);
    };
    connect(pageStack_, &Settings::PageStack::contentChanged, this, chainTabs);
    chainTabs();

    int openIndex = 0;
    for (int i = 0; i < int(pages_.size()); ++i) {
        if (pages_[i]->id() == openAt)
            openIndex = i;
    }
    pageStack_->scrollToPage(openIndex, false);
}

void SettingsDialog::addPage(Settings::Page* page)
{
    // A heading row ahead of the first page of each section. Not a page
    // itself: no page index, and neither selectable nor focusable, so
    // clicks and arrow keys pass over it.
    const QString section = page->sidebarSection();
    if (!section.isEmpty() && section != lastSidebarSection_) {
        auto* heading = new QStandardItem(section.toUpper());
        heading->setData(int(ViewModel::SidebarModel::Kind::PlaylistsHeader), ViewModel::SidebarModel::KindRole);
        heading->setFlags(Qt::NoItemFlags);
        sidebarModel_->appendRow(heading);
    }
    lastSidebarSection_ = section;

    sidebarRowForPage_.push_back(sidebarModel_->rowCount());
    auto* item = new QStandardItem(page->title());
    item->setData(int(ViewModel::SidebarModel::Kind::Playlist), ViewModel::SidebarModel::KindRole);
    item->setData(page->iconName(), ViewModel::SidebarModel::ThemeIconRole);
    item->setData(page->iconPath(), ViewModel::SidebarModel::SourceIconPathRole);
    item->setData(int(pages_.size()), kPageIndexRole);
    sidebarModel_->appendRow(item);

    pages_.push_back(page);
    pageStack_->addPage(page);

    connect(page, &Settings::Page::dirtyChanged, this, &SettingsDialog::updateApplyButton);
}

Rpc::Task<bool> SettingsDialog::applyAllAsync()
{
    if (applying_)
        co_return false;
    // done() refuses to close while this runs, so neither the dialog nor
    // its pages go away under the awaits below.
    applying_ = true;
    buttons_->setEnabled(false);
    bool ok = true;
    for (Settings::Page* page : pages_) {
        if (page->isDirty() && !co_await page->apply())
            ok = false;
    }
    // Once, for everything the pages changed about each source.
    for (const QString& sourceId : std::as_const(restarts_.sourceIds))
        sourceManager_.restart(sourceId);
    restarts_.sourceIds.clear();
    applying_ = false;
    buttons_->setEnabled(true);
    updateApplyButton();
    co_return ok;
}

Rpc::Task<void> SettingsDialog::acceptAsync()
{
    // Refused somewhere: stay open, the page shows what and why.
    if (co_await applyAllAsync())
        accept();
}

void SettingsDialog::updateApplyButton()
{
    const bool dirty = std::any_of(pages_.begin(), pages_.end(), [](const Settings::Page* p) { return p->isDirty(); });
    applyButton_->setEnabled(dirty);
}

void SettingsDialog::done(int result)
{
    if (applying_)
        return;
    settings_.setSettingsDialogGeometry(saveGeometry());
    QDialog::done(result);
}

bool SettingsDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == sidebarView_->viewport()) {
        if (event->type() == QEvent::MouseMove)
            updateSidebarHover(sidebarView_->indexAt(static_cast<QMouseEvent*>(event)->pos()));
        else if (event->type() == QEvent::Leave)
            updateSidebarHover(QModelIndex());
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::updateSidebarHover(const QModelIndex& index)
{
    const QModelIndex previous = sidebarDelegate_->hoveredIndex();
    if (previous == index)
        return;
    sidebarDelegate_->setHoveredIndex(index);
    if (previous.isValid())
        sidebarView_->viewport()->update(sidebarView_->visualRect(previous));
    if (index.isValid())
        sidebarView_->viewport()->update(sidebarView_->visualRect(index));
}

} // namespace Ui
