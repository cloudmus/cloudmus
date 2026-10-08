#include "CrashShortcut.h"

#include <QKeyEvent>

#include "CrashReporter.h"

namespace Diagnostics {

namespace {
constexpr int kPresses = 3;
constexpr quint64 kWindowMs = 3000;
constexpr Qt::KeyboardModifiers kModifiers = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
} // namespace

CrashShortcut::CrashShortcut(std::function<void()> action, QObject* parent)
    : QObject(parent)
    , action_(action ? std::move(action) : std::function<void()>(&CrashReporter::crashOnPurpose))
{
}

bool CrashShortcut::eventFilter(QObject*, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->isAutoRepeat() || key->key() != Qt::Key_F12 || key->modifiers() != kModifiers)
        return false;

    // The event's own time, not the clock's: the presses are what is timed.
    const quint64 now = key->timestamp();
    if (presses_ == 0 || now - firstPress_ > kWindowMs) {
        presses_ = 0;
        firstPress_ = now;
    }
    if (++presses_ >= kPresses) {
        presses_ = 0;
        action_();
    }
    // Not consumed: the key means nothing else, and swallowing it would
    // show that something is listening.
    return false;
}

} // namespace Diagnostics
