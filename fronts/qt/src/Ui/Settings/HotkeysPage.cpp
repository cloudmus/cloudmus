#include "Settings/HotkeysPage.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "Spacing.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {
// How long typing must pause before the list is filtered.
constexpr int kFilterDelayMs = 250;

void setError(QLabel* label, bool error)
{
    label->setProperty("error", error);
    label->setProperty("hint", !error);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
} // namespace

HotkeysPage::HotkeysPage(Hotkeys::Registry& registry, QObject* parent)
    : Page(parent)
    , registry_(registry)
{
}

QString HotkeysPage::title() const { return tr("Keyboard shortcuts"); }

QWidget* HotkeysPage::createWidget(QWidget* parent)
{
    auto* widget = new QWidget(parent);

    globalHint_ = new QLabel(widget);
    globalHint_->setProperty("hint", true);
    globalHint_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    globalHint_->setWordWrap(true);

    filterEdit_ = new QLineEdit(widget);
    filterEdit_->setPlaceholderText(tr("Filter by action or shortcut"));
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setFont(Theme::font(Theme::TextStyle::Body));
    filterTimer_ = new QTimer(widget);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(kFilterDelayMs);
    connect(filterTimer_, &QTimer::timeout, this, &HotkeysPage::applyFilter);
    // Each keystroke restarts the wait.
    connect(filterEdit_, &QLineEdit::textChanged, filterTimer_, qOverload<>(&QTimer::start));

    noMatches_ = new QLabel(tr("No shortcuts match the filter."), widget);
    noMatches_->setProperty("hint", true);
    noMatches_->setFont(Theme::font(Theme::TextStyle::BodySecondary));
    noMatches_->hide();

    systemButton_ = new QPushButton(tr("Change in the system settings…"), widget);
    systemButton_->setProperty("variant", "secondary");
    systemButton_->setFont(Theme::font(Theme::TextStyle::Button));
    connect(systemButton_, &QPushButton::clicked, &registry_, &Hotkeys::Registry::requestConfigureInSystem);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(Theme::Spacing::space3);
    grid->setVerticalSpacing(Theme::Spacing::space1);
    grid->setColumnStretch(0, 1);
    const auto heading = [&](const QString& text, int column) {
        auto* label = new QLabel(text.toUpper(), widget);
        label->setProperty("hint", true);
        label->setFont(Theme::font(Theme::TextStyle::LabelUpper));
        grid->addWidget(label, 0, column);
    };
    heading(tr("Action"), 0);
    heading(tr("Shortcut"), 1);
    heading(tr("Global"), 2);
    heading(tr("Notify"), 3);

    loaded_ = registry_.bindings();
    int gridRow = 1;
    for (const Hotkeys::Binding& binding : std::as_const(loaded_)) {
        Row row;
        row.action = binding.action;
        const Hotkeys::ActionInfo& info = Hotkeys::info(binding.action);

        row.description = new QLabel(info.description, widget);
        row.description->setFont(Theme::font(Theme::TextStyle::Body));
        row.description->setWordWrap(true);

        row.keyEdit = new QKeySequenceEdit(widget);
        row.keyEdit->setMaximumSequenceLength(1);
        row.keyEdit->setClearButtonEnabled(true);
        row.keyEdit->setFont(Theme::font(Theme::TextStyle::Body));

        row.globalCheck = new QCheckBox(widget);
        row.globalCheck->setToolTip(tr("Works with the window out of focus"));
        row.notifyCheck = new QCheckBox(widget);
        row.notifyCheck->setToolTip(tr("Says what it did in a notification"));
        // Not every action has something to say.
        row.notifyCheck->setVisible(info.hasNotice);

        row.status = new QLabel(widget);
        row.status->setProperty("hint", true);
        row.status->setFont(Theme::font(Theme::TextStyle::BodySecondary));
        row.status->setWordWrap(true);
        row.status->hide();

        row.resetButton = new QPushButton(tr("Reset"), widget);
        row.resetButton->setProperty("variant", "secondary");
        row.resetButton->setFont(Theme::font(Theme::TextStyle::Button));
        row.resetButton->setToolTip(tr("Restore this shortcut's defaults"));
        connect(row.resetButton, &QPushButton::clicked, this, [this, action = binding.action]() {
            showBinding(rows_[int(action)], Hotkeys::Registry::defaultBindings()[int(action)]);
            updateStatuses();
            emit dirtyChanged();
        });

        showBinding(row, binding);
        connect(row.keyEdit, &QKeySequenceEdit::keySequenceChanged, this, [this]() {
            updateStatuses();
            emit dirtyChanged();
        });
        connect(row.globalCheck, &QCheckBox::toggled, this, [this]() {
            updateStatuses();
            emit dirtyChanged();
        });
        connect(row.notifyCheck, &QCheckBox::toggled, this, [this]() {
            updateStatuses();
            emit dirtyChanged();
        });

        grid->addWidget(row.description, gridRow, 0);
        grid->addWidget(row.keyEdit, gridRow, 1);
        grid->addWidget(row.globalCheck, gridRow, 2, Qt::AlignCenter);
        grid->addWidget(row.notifyCheck, gridRow, 3, Qt::AlignCenter);
        grid->addWidget(row.resetButton, gridRow, 4);
        grid->addWidget(row.status, gridRow + 1, 0, 1, 5);
        gridRow += 2;
        rows_.append(row);
    }

    auto* reset = new QPushButton(tr("Restore defaults"), widget);
    reset->setProperty("variant", "secondary");
    reset->setFont(Theme::font(Theme::TextStyle::Button));
    connect(reset, &QPushButton::clicked, this, [this]() {
        const QList<Hotkeys::Binding> defaults = Hotkeys::Registry::defaultBindings();
        for (int i = 0; i < rows_.size(); ++i)
            showBinding(rows_[i], defaults[i]);
        updateStatuses();
        emit dirtyChanged();
    });

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Spacing::space3);
    layout->addWidget(globalHint_);
    layout->addWidget(systemButton_, 0, Qt::AlignLeft);
    layout->addWidget(filterEdit_);
    layout->addLayout(grid);
    layout->addWidget(noMatches_);
    layout->addWidget(reset, 0, Qt::AlignLeft);

    connect(&registry_, &Hotkeys::Registry::bindingsChanged, widget, [this]() { onBindingsChanged(); });
    connect(&registry_, &Hotkeys::Registry::globalStateChanged, widget, [this]() { updateGlobalState(); });
    updateGlobalState();
    return widget;
}

void HotkeysPage::showBinding(const Row& row, const Hotkeys::Binding& binding)
{
    const QSignalBlocker keyBlocker(row.keyEdit);
    const QSignalBlocker globalBlocker(row.globalCheck);
    const QSignalBlocker notifyBlocker(row.notifyCheck);
    row.keyEdit->setKeySequence(binding.key);
    row.globalCheck->setChecked(binding.global);
    row.notifyCheck->setChecked(binding.notify);
}

QList<Hotkeys::Binding> HotkeysPage::collect() const
{
    QList<Hotkeys::Binding> result;
    for (const Row& row : rows_) {
        // Only the first chord counts: the editor lets a longer one be typed
        // before it stops taking keys.
        QKeySequence key = row.keyEdit->keySequence();
        if (key.count() > 1)
            key = QKeySequence(key[0]);
        result.append({ row.action, key, row.globalCheck->isChecked(), row.notifyCheck->isChecked() });
    }
    return result;
}

bool HotkeysPage::isDirty() const { return !rows_.isEmpty() && collect() != registry_.bindings(); }

void HotkeysPage::onBindingsChanged()
{
    const QList<Hotkeys::Binding> now = registry_.bindings();
    const QList<Hotkeys::Binding> shown = collect();
    for (int i = 0; i < rows_.size(); ++i) {
        // A row the user has edited keeps their edit.
        if (shown[i] == loaded_[i])
            showBinding(rows_[i], now[i]);
    }
    loaded_ = now;
    updateStatuses();
    emit dirtyChanged();
}

void HotkeysPage::updateGlobalState()
{
    using Support = Hotkeys::Registry::GlobalSupport;
    const Support support = registry_.globalSupport();
    switch (support) {
        case Support::None:
            globalHint_->setText(registry_.unavailableReason().isEmpty()
                    ? tr("Global shortcuts aren't available on this desktop: the shortcuts work only while the "
                         "CloudMus window is in front.")
                    : tr("Global shortcuts aren't available: %1 The shortcuts work only while the CloudMus window "
                         "is in front.")
                          .arg(registry_.unavailableReason()));
            break;
        case Support::Direct:
            globalHint_->setText(tr("A global shortcut works even when the CloudMus window isn't in front."));
            break;
        case Support::SystemAssigned:
            globalHint_->setText(tr("This desktop decides the keys of global shortcuts: the key here is what CloudMus "
                                    "asks for the first time, and it is what works while the window is in front. To "
                                    "change a global one afterwards, use the desktop's own settings."));
            break;
    }
    systemButton_->setVisible(registry_.canConfigureInSystem());
    for (const Row& row : std::as_const(rows_))
        row.globalCheck->setEnabled(support != Support::None);
    updateStatuses();
}

void HotkeysPage::updateResetButtons()
{
    const QList<Hotkeys::Binding> bindings = collect();
    const QList<Hotkeys::Binding> defaults = Hotkeys::Registry::defaultBindings();
    for (int i = 0; i < rows_.size(); ++i)
        rows_[i].resetButton->setEnabled(bindings[i] != defaults[i]);
}

void HotkeysPage::applyFilter()
{
    bool any = false;
    for (const Row& row : std::as_const(rows_)) {
        const bool visible = matchesFilter(row);
        any = any || visible;
        row.description->setVisible(visible);
        row.keyEdit->setVisible(visible);
        row.globalCheck->setVisible(visible);
        row.notifyCheck->setVisible(visible && Hotkeys::info(row.action).hasNotice);
        row.resetButton->setVisible(visible);
    }
    noMatches_->setVisible(!any);
    updateStatuses();
}

bool HotkeysPage::matchesFilter(const Row& row) const
{
    // Typed with or without spaces, in any case: "ctrl alt l", "Ctrl+Alt+L".
    const auto squeeze = [](const QString& text) {
        QString result = text.toLower();
        result.remove(QLatin1Char(' ')).remove(QLatin1Char('+'));
        return result;
    };
    const QString wanted = squeeze(filterEdit_ != nullptr ? filterEdit_->text() : QString());
    if (wanted.isEmpty())
        return true;
    const Hotkeys::ActionInfo& info = Hotkeys::info(row.action);
    return squeeze(info.description).contains(wanted)
        || squeeze(row.keyEdit->keySequence().toString(QKeySequence::NativeText)).contains(wanted)
        || squeeze(row.keyEdit->keySequence().toString(QKeySequence::PortableText)).contains(wanted);
}

void HotkeysPage::updateStatuses()
{
    updateResetButtons();
    using Support = Hotkeys::Registry::GlobalSupport;
    const QList<Hotkeys::Binding> bindings = collect();
    QHash<int, QString> conflictWith;
    for (const auto& [first, second] : Hotkeys::Registry::conflicts(bindings)) {
        conflictWith.insert(int(first), Hotkeys::info(second).description);
        conflictWith.insert(int(second), Hotkeys::info(first).description);
    }
    const QHash<QString, QString> triggers = registry_.systemTriggers();
    const QSet<QString> failed = registry_.failedIds();

    for (int i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        const Hotkeys::ActionInfo& info = Hotkeys::info(row.action);
        QString text;
        bool error = false;
        if (conflictWith.contains(int(row.action))) {
            text = tr("The same shortcut as “%1”.").arg(conflictWith.value(int(row.action)));
            error = true;
        } else if (bindings[i].global && registry_.globalSupport() == Support::Direct && failed.contains(info.id)) {
            text = tr("Another program already has this shortcut, so it works only while the window is in front.");
            error = true;
        } else if (bindings[i].global && registry_.globalSupport() == Support::SystemAssigned) {
            const QString trigger = triggers.value(info.id);
            text = trigger.isEmpty() ? tr("The desktop hasn't set a global shortcut for this yet.")
                                     : tr("Global shortcut set by the desktop: %1").arg(trigger);
        }
        row.status->setText(text);
        // The row's own visibility: the filter is applied when it changes, not on every edit.
        row.status->setVisible(!text.isEmpty() && !row.description->isHidden());
        setError(row.status, error);
    }
}

Rpc::Task<bool> HotkeysPage::apply()
{
    if (rows_.isEmpty())
        co_return true;
    const QList<Hotkeys::Binding> bindings = collect();
    // Refused: updateStatuses() has marked the rows that clash.
    if (!Hotkeys::Registry::conflicts(bindings).isEmpty())
        co_return false;
    registry_.setBindings(bindings);
    loaded_ = registry_.bindings();
    co_return true;
}

} // namespace Ui::Settings
