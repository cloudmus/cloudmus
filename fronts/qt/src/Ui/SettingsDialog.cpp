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
#include "Settings/PageStack.h"
#include "SidebarModel.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "Tokens.h"

namespace Ui {

namespace {
constexpr int kSidebarWidth = 200;
} // namespace

SettingsDialog::SettingsDialog(Config::Settings& settings, QWidget* parent, const QString& openAt)
    : QDialog(parent)
    , settings_(settings)
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
    addPage(new Settings::GeneralPage(settings_, this));
    addPage(new Settings::DownloadsPage(settings_, this));

    // currentChanged, not clicked: arrow keys in the sidebar jump too.
    connect(
        sidebarView_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
            if (!syncingSidebar_ && current.isValid())
                pageStack_->scrollToPage(current.row());
        });
    // Also on a click of the row that's already current: its section may
    // be only partly in view.
    connect(sidebarView_, &QListView::clicked, this,
        [this](const QModelIndex& index) { pageStack_->scrollToPage(index.row()); });
    connect(pageStack_, &Settings::PageStack::currentPageChanged, this, [this](int index) {
        syncingSidebar_ = true;
        sidebarView_->setCurrentIndex(sidebarModel_->index(index, 0));
        syncingSidebar_ = false;
    });

    // --- buttons
    auto* buttons
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    styleDialogButton(buttons->button(QDialogButtonBox::Ok), "primary");
    styleDialogButton(buttons->button(QDialogButtonBox::Apply), "secondary");
    styleDialogButton(buttons->button(QDialogButtonBox::Cancel), "secondary");
    applyButton_ = buttons->button(QDialogButtonBox::Apply);
    connect(applyButton_, &QPushButton::clicked, this, &SettingsDialog::applyAll);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyAll();
        accept();
    });
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

    int openIndex = 0;
    for (int i = 0; i < int(pages_.size()); ++i) {
        if (pages_[i]->id() == openAt)
            openIndex = i;
    }
    pageStack_->scrollToPage(openIndex, false);
}

void SettingsDialog::addPage(Settings::Page* page)
{
    pages_.push_back(page);
    pageStack_->addPage(page);

    auto* item = new QStandardItem(page->title());
    item->setData(int(SidebarModel::Kind::Playlist), SidebarModel::KindRole);
    item->setData(page->iconName(), SidebarModel::ThemeIconRole);
    sidebarModel_->appendRow(item);

    connect(page, &Settings::Page::dirtyChanged, this, &SettingsDialog::updateApplyButton);
}

void SettingsDialog::applyAll()
{
    for (Settings::Page* page : pages_) {
        if (page->isDirty())
            page->apply();
    }
    updateApplyButton();
}

void SettingsDialog::updateApplyButton()
{
    const bool dirty = std::any_of(pages_.begin(), pages_.end(), [](const Settings::Page* p) { return p->isDirty(); });
    applyButton_->setEnabled(dirty);
}

void SettingsDialog::done(int result)
{
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
