#pragma once

#include "WindowGlass.h"

#include <QRect>
#include <QRegion>

class QWindow;

// WindowGlass.cpp's platform backends, shared with the ones that need a
// source file of their own (Objective-C++ on macOS).
namespace Integration::WindowGlass::Detail {

// A way to ask the compositor for blur: apply() sets `region` (window coordinates;
// the whole window if empty) behind `window`, whose native window exists.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void apply(QWindow* window, const QRegion& region) = 0;
    // See enableBlurBehindPanel().
    virtual void applyPanel(QWindow* window, const QRect& panel, qreal radius)
    {
        apply(window, roundedRegion(panel, radius));
    }
    // See fallbackBlur(), nativePopups(), setUpNativePopup().
    virtual bool isFallback() const { return false; }
    virtual bool hasNativePopups() const { return false; }
    // Whether apply()/applyPanel() work on a native window that isn't on
    // screen yet — then there's no need to wait for it to be mapped, and the
    // first frame is already blurred.
    virtual bool appliesBeforeShow() const { return false; }
    virtual void setUpNativePopup(QWindow*, int, bool) { }
};

#if defined(Q_OS_MACOS)
// WindowGlassMac.mm.
Backend* createMacBackend();
#endif

} // namespace Integration::WindowGlass::Detail
