#pragma once

#include <QObject>

#include <functional>

namespace Diagnostics {

// A hidden way to crash the app on purpose — to check, on a release build,
// that a crash reaches the crash service with a readable stack. Ctrl+Shift+
// Alt+F12 pressed three times within three seconds, in any window of the
// app (install it as an event filter on the QApplication). Three presses,
// so a stray key never does it.
class CrashShortcut : public QObject {
    Q_OBJECT

public:
    // What the third press does; crashing on purpose unless a test says
    // otherwise.
    explicit CrashShortcut(std::function<void()> action = { }, QObject* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::function<void()> action_;
    int presses_ = 0;
    // Event time (ms) of the first press of the current run.
    quint64 firstPress_ = 0;
};

} // namespace Diagnostics
