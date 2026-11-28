#pragma once

#include <QAbstractAnimation>
#include <QAnimationDriver>
#include <QChronoTimer>

#include <chrono>

namespace Ui {

// One frame at the fastest connected display's refresh rate (60 Hz when
// nothing reports one). The fastest, not the main window's screen: the
// animation clock is shared by every window, and a window can straddle
// two screens — a tick faster than a slower screen needs costs a little
// CPU, and only while something is animating.
std::chrono::nanoseconds displayFrameInterval();

// Ticks every QAbstractAnimation in the app once per display frame. Qt's
// default driver uses a fixed 16 ms timer, which caps every animation at
// 60 FPS even on a 144/165 Hz monitor. Widgets have no vsync signal to
// lock onto (raster windows aren't frame-paced by Qt), so this is a
// precise timer at the display's rate; the compositor shows whichever
// frame is newest at each refresh. Install once, on the GUI thread, after
// the QApplication exists; it uninstalls itself when destroyed.
class DisplayAnimationDriver : public QAnimationDriver {
public:
    explicit DisplayAnimationDriver(QObject* parent = nullptr);

protected:
    void start() override;
    void stop() override;

private:
    QChronoTimer timer_;
};

// An endless animation that emits frame() on every animation tick — for
// hand-painted effects that compute their own progress (see
// TrackRowDelegate's cover fades) and just need repainting in step with
// every other animation.
class FrameTicker : public QAbstractAnimation {
    Q_OBJECT

public:
    using QAbstractAnimation::QAbstractAnimation;
    int duration() const override { return -1; }

signals:
    void frame();

protected:
    void updateCurrentTime(int) override { emit frame(); }
};

} // namespace Ui
