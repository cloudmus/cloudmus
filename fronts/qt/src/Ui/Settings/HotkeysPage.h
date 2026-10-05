#pragma once

#include <QList>

#include "Registry.h"
#include "Settings/Page.h"

class QCheckBox;
class QKeySequenceEdit;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;

namespace Ui::Settings {

// The hotkeys: for each action its key, whether it works with the window out
// of focus (global) or only inside the app, and whether it says what it did
// in a notification.
class HotkeysPage : public Page {
    Q_OBJECT

public:
    explicit HotkeysPage(Hotkeys::Registry& registry, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("hotkeys"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("keyboard"); }
    int estimatedHeight() const override { return 560; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // A wrapped label's height depends on its width, which the grid only
    // learns after laying out: without this, a description that needs
    // two or three lines is cut off to one.
    void fitDescription(QLabel* label);

    struct Row {
        Hotkeys::Action action = Hotkeys::Action::PlayPause;
        QLabel* description = nullptr;
        QKeySequenceEdit* keyEdit = nullptr;
        QCheckBox* globalCheck = nullptr;
        QCheckBox* notifyCheck = nullptr;
        QCheckBox* soundCheck = nullptr;
        QLabel* status = nullptr;
        QPushButton* resetButton = nullptr;
    };

    QList<Hotkeys::Binding> collect() const;
    void showBinding(const Row& row, const Hotkeys::Binding& binding);
    // A change made outside the page (a key set in the desktop's settings):
    // rows the user hasn't touched follow it.
    void onBindingsChanged();
    void updateGlobalState();
    void updateStatuses();
    // The row's own Reset is for when it differs from the action's defaults.
    void updateResetButtons();
    // Shows only the rows whose action name or shortcut contains what the
    // filter says (all of them when it is empty).
    void applyFilter();
    bool matchesFilter(const Row& row) const;

    Hotkeys::Registry& registry_;
    QList<Row> rows_;
    // What the rows were last set from the registry.
    QList<Hotkeys::Binding> loaded_;
    QLabel* globalHint_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* noMatches_ = nullptr;
    // Filters once typing has paused, not on every key.
    QTimer* filterTimer_ = nullptr;
    QPushButton* systemButton_ = nullptr;
};

} // namespace Ui::Settings
