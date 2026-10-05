#pragma once

#include <QKeySequence>
#include <QString>

namespace Integration {

// X11 keysym for a Qt key (Qt's own key codes for printable Latin-1 are the
// same numbers); 0 for none.
unsigned int keysymForKey(Qt::Key key);

// The key's name in the XDG shortcuts spec (the keysym's name: "p", "Right",
// "Page_Up"); empty for a key without one here.
QString keysymNameForKey(Qt::Key key);

// "CTRL+ALT+p" — the shortcuts spec's format, as the portal wants a
// preferred trigger. Empty if the sequence isn't a single chord of known keys.
QString portalTrigger(const QKeySequence& sequence);

} // namespace Integration
