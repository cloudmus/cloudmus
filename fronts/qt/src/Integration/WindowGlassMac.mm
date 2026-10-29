#include "WindowGlassBackend.h"

#include <QLoggingCategory>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace Integration::WindowGlass::Detail {

namespace {

    Q_LOGGING_CATEGORY(lcGlassMac, "cloudmus.integration.glass")

    NSUserInterfaceItemIdentifier const kGlassIdentifier = @"cloudmusGlass";

    // A mask for an NSVisualEffectView's rounded shape, stretched to its size:
    // only the corners keep their size.
    NSImage* roundedMask(CGFloat radius)
    {
        const CGFloat side = 2 * radius + 1;
        NSImage* image = [NSImage imageWithSize:NSMakeSize(side, side)
                                        flipped:NO
                                 drawingHandler:^BOOL(NSRect rect) {
                                     [NSColor.blackColor setFill];
                                     [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
                                     return YES;
                                 }];
        image.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
        image.resizingMode = NSImageResizingModeStretch;
        return image;
    }

    // macOS: an NSVisualEffectView blending what's behind the window, put
    // under Qt's own view — as its sibling in the window's frame view, so
    // Qt's drawing stays on top and its view isn't reparented. The main
    // window's covers all of it, in the sidebar's material — the most
    // see-through of the window ones, where the window background's was
    // all but opaque in light mode — and goes flat while the window is
    // inactive, as native windows do; a popup's covers just its panel, rounded by a
    // mask, and stays active (popups never take focus). Light or dark after
    // the app's appearance (Ui::Settings::GeneralPage::applyColorScheme()).
    class MacBackend : public Backend {
    public:
        void apply(QWindow* window, const QRegion& region) override
        {
            NSView* view = contentView(window);
            NSVisualEffectView* effect = effectViewFor(view);
            if (effect == nil)
                return;
            if (region.isEmpty()) {
                effect.frame = view.frame;
                effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                effect.material = NSVisualEffectMaterialSidebar;
                effect.state = NSVisualEffectStateFollowsWindowActiveState;
                effect.maskImage = nil;
                return;
            }
            placeOverPanel(effect, view, region.boundingRect());
            effect.maskImage = nil;
        }

        void applyPanel(QWindow* window, const QRect& panel, qreal radius) override
        {
            NSView* view = contentView(window);
            NSVisualEffectView* effect = effectViewFor(view);
            if (effect == nil)
                return;
            // A pixel inside the panel, like roundedRegion(): its own
            // antialiased edge is drawn over the blur's.
            constexpr int kInset = 1;
            placeOverPanel(effect, view, panel.adjusted(kInset, kInset, -kInset, -kInset));
            effect.maskImage = roundedMask(qMax<qreal>(0, radius - kInset));
        }

        // Not before show: as a popup first shows, Qt replaces its NSWindow
        // (with one sized for it — the first is a stale 640x480), and the
        // effect view put in the first one went with it. Once it's on
        // screen, the window is the one that stays.
        bool appliesBeforeShow() const override { return false; }

    private:
        static NSView* contentView(QWindow* window) { return (__bridge NSView*)reinterpret_cast<void*>(window->winId()); }

        // The window's effect view, made the first time; nil if `view` isn't
        // in a window yet.
        static NSVisualEffectView* effectViewFor(NSView* view)
        {
            NSView* frameView = view.superview;
            if (frameView == nil || view.window == nil) {
                qCWarning(lcGlassMac) << "no NSWindow to blur behind yet";
                return nil;
            }
            for (NSView* sibling in frameView.subviews) {
                if ([sibling.identifier isEqualToString:kGlassIdentifier])
                    return static_cast<NSVisualEffectView*>(sibling);
            }
            // Qt makes a translucent window's NSWindow non-opaque itself; made
            // sure of here, or the effect view has an opaque backing to blend.
            view.window.opaque = NO;
            view.window.backgroundColor = NSColor.clearColor;
            NSVisualEffectView* effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
            effect.identifier = kGlassIdentifier;
            effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
            // A titled window's frame view (NSThemeFrame) logs "NSWindow
            // warning: adding an unknown subview" for anything added the
            // public way; its own way for the views it knows about doesn't.
            // Private, so only where it's there.
            const SEL addKnown = NSSelectorFromString(@"_addKnownSubview:positioned:relativeTo:");
            if ([frameView respondsToSelector:addKnown]) {
                using AddKnownFn = void (*)(id, SEL, NSView*, NSWindowOrderingMode, NSView*);
                reinterpret_cast<AddKnownFn>([frameView methodForSelector:addKnown])(
                    frameView, addKnown, effect, NSWindowBelow, view);
            } else {
                [frameView addSubview:effect positioned:NSWindowBelow relativeTo:view];
            }
            return effect;
        }

        // Puts `effect` over `rect`, in Qt's coordinates of `view` — top-left
        // origin, where the frame view's is usually bottom-left.
        static void placeOverPanel(NSVisualEffectView* effect, NSView* view, const QRect& rect)
        {
            const NSRect frame = view.frame;
            const CGFloat y = view.superview.isFlipped ? frame.origin.y + rect.y()
                                                       : frame.origin.y + frame.size.height - rect.y() - rect.height();
            effect.frame = NSMakeRect(frame.origin.x + rect.x(), y, rect.width(), rect.height());
            effect.autoresizingMask = NSViewNotSizable;
            effect.material = NSVisualEffectMaterialPopover;
            effect.state = NSVisualEffectStateActive;
        }
    };

} // namespace

Backend* createMacBackend()
{
    return new MacBackend; // leaked on purpose, like the others: lives as long as the app
}

} // namespace Integration::WindowGlass::Detail
