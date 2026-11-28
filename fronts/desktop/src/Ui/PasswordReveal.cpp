#include "PasswordReveal.h"

#include <QAction>
#include <QLineEdit>

#include "Icons.h"
#include "Tokens.h"

namespace Ui {

namespace {
constexpr int kIconSide = 16;
} // namespace

void addPasswordReveal(QLineEdit* edit)
{
    edit->setEchoMode(QLineEdit::Password);
    auto* action = edit->addAction(QIcon(), QLineEdit::TrailingPosition);
    // Tinted at use, so it follows theme changes like the app's other icons.
    const auto refresh = [edit, action]() {
        const bool shown = edit->echoMode() == QLineEdit::Normal;
        action->setIcon(Theme::icon(shown ? QStringLiteral("visibility_off") : QStringLiteral("visibility"),
            Theme::IconColor::InkSecondary, kIconSide));
        action->setToolTip(shown ? QLineEdit::tr("Hide") : QLineEdit::tr("Show"));
    };
    QObject::connect(action, &QAction::triggered, edit, [edit, refresh]() {
        edit->setEchoMode(edit->echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
        refresh();
    });
    QObject::connect(&Theme::notifier(), &Theme::Notifier::changed, action, refresh);
    refresh();
}

} // namespace Ui
