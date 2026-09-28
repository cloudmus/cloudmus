# High-refresh-rate animations (research, on hold)

Status: researched, not implemented. Picked up again later.

## Problem

On high-refresh monitors (120–165 Hz) the Qt front's animations visibly
run at 60 Hz: transitions, smooth scrolling, tooltip/toast fades, the
overlay scrollbar, the theme crossfade, cover fades in track rows.

## Findings

- Every `QVariantAnimation`/`QPropertyAnimation` in `src/Ui/`
  (`Transition.h`, `SmoothScroller`, `ThemedToolTip`, `ToastNotifier`,
  `OverlayScrollBar`, `ThemeCrossfade`) is ticked by Qt's single internal
  `QUnifiedTimer`. Its default driver uses a fixed 16 ms interval
  (`DEFAULT_TIMER_INTERVAL`, `QtCore/private/qabstractanimation_p.h`),
  whatever the monitor.
- `TrackRowDelegate` runs its own 16 ms `QTimer` for cover fades
  (`src/Ui/TrackRowDelegate.cpp`, `fadeTimer_`).
- `QAnimationDriver` is **public** API (`qabstractanimation.h`): a subclass
  with `install()` replaces what ticks all animations in the process.
  `QUnifiedTimer::setTimingInterval()` would also work, but it is private
  API.
- `QGuiApplication::primaryScreen()` isn't necessarily the fast one. On
  the dev machine the primary screen is eDP-1 at 60 Hz and the external
  DP-3 is at 164.8 Hz. The rate must come from the screen the window is
  on.

### Measurement

Ticks of one 2 s `QVariantAnimation` (Qt 6.11.2, Linux; program below):

| Driver | Ticks/s |
|---|---|
| Qt default | 62 |
| Custom driver, `Qt::PreciseTimer`, 6 ms interval | 166 |

This counts animation ticks, not frames on screen. Whether widgets
actually repaint that often is still open (see below).

<details>
<summary>Measurement program</summary>

```cpp
// g++ -std=c++20 -fPIC main.cpp -o anim $(pkg-config --cflags --libs Qt6Gui)
// ./anim          -> default driver
// ./anim custom   -> custom driver
#include <QAnimationDriver>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>
#include <cstdio>

class RefreshRateDriver : public QAnimationDriver {
public:
    explicit RefreshRateDriver(qreal hz)
    {
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(qMax(1, int(1000.0 / hz)));
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        clock_.start();
    }
    qint64 elapsed() const override { return clock_.elapsed(); }

protected:
    void start() override { QAnimationDriver::start(); timer_.start(); }
    void stop() override { timer_.stop(); QAnimationDriver::stop(); }

private:
    QTimer timer_;
    QElapsedTimer clock_;
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const bool custom = argc > 1;
    for (QScreen* s : QGuiApplication::screens())
        std::printf("%s %.1f Hz\n", qPrintable(s->name()), s->refreshRate());
    if (custom)
        (new RefreshRateDriver(144))->install();
    int ticks = 0;
    QVariantAnimation anim;
    anim.setStartValue(0.0);
    anim.setEndValue(1.0);
    anim.setDuration(2000);
    QObject::connect(&anim, &QVariantAnimation::valueChanged, [&] { ++ticks; });
    QObject::connect(&anim, &QVariantAnimation::finished, [&] {
        std::printf("%s: %.0f ticks/s\n", custom ? "custom" : "default", ticks / 2.0);
        app.quit();
    });
    anim.start();
    return app.exec();
}
```

</details>

## Options for syncing to the monitor

1. **Refresh-rate timer driver (recommended first step).**
   - A `QAnimationDriver` ticking with a `Qt::PreciseTimer` at
     `1000 / refreshRate` ms of the window's current screen.
   - The interval is recomputed on `QWindow::screenChanged` and
     `QScreen::refreshRateChanged`.
   - `elapsed()` returns wall-clock time, so values stay correct even
     when a tick jitters.
   - Works on every platform. The drawback: it isn't phase-locked to
     vsync, so an occasional frame is repeated or skipped.
2. **Vsync-driven ticks** (what Qt Quick's render loop does with its own
   driver).
   - **Wayland:** `QWindow::requestUpdate()` is delivered on the
     compositor's frame callback. The driver requests an update and calls
     `advance()` on `QEvent::UpdateRequest`.
   - **Windows:** a thread looping on `DwmFlush()` (or
     `IDXGIOutput::WaitForVBlank`) signals the GUI thread every vblank.
   - **X11:** no reliable source; use option 1 there.
   - Complications:
     - several top-level windows (main window, tooltips, toasts);
     - no frame callbacks while minimized or hidden, so a fallback timer
       is still needed.

Plan: implement option 1 behind a small clock interface (for example
`Ui::FrameClock` plus the driver). Move `TrackRowDelegate`'s fade timer
onto it. Option 2's per-platform sources can be added behind the same
interface later.

## Open questions

- **Real repaint rate.** Does Qt Widgets actually paint at 120–165 Hz?
  `update()` calls are coalesced, and on Wayland repaints may already be
  throttled by frame callbacks. Measure with a `paintEvent` counter in the
  app, not just animation ticks.
- **CPU cost.** Painting is raster (software), with the glass effect and
  large lists. Measure CPU during scrolling and transitions at 165 Hz;
  maybe cap the rate (for example at 144 Hz) or make it a setting.
- **Windows specifics.** Whether `QWindow::requestUpdate()` is
  vsync-driven there in Qt 6.9 still needs checking.
