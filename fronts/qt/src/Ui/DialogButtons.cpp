#include "DialogButtons.h"

#include <QAbstractButton>
#include <QIcon>
#include <QStyle>

#include "Typography.h"

namespace Ui {

void styleDialogButton(QAbstractButton* button, const char* variant)
{
    button->setProperty("variant", variant);
    button->setFont(Theme::font(Theme::TextStyle::Button));
    // Some platform themes (KDE's in particular) inject a standard
    // checkmark/cross icon onto Ok/Cancel regardless of the active
    // QStyle — clashes with the flat, icon-less button look everywhere
    // else in the app.
    button->setIcon(QIcon());
    // QDialogButtonBox's own construction appears to polish its standard
    // buttons before we get a chance to set "variant" above, so the
    // [variant="..."] QSS rule never gets (re-)evaluated against it — Qt's
    // documented fix for "a QSS-relevant dynamic property changed after the
    // widget was polished."
    button->style()->unpolish(button);
    button->style()->polish(button);
}

} // namespace Ui
