#include "DockIcon.h"

#include <QIcon>
#include <QImage>
#include <QPainter>

#import <AppKit/AppKit.h>

namespace Integration::DockIcon {

namespace {
    NSImage* dockImage = nil;

    void apply()
    {
        if (dockImage)
            NSApp.applicationIconImage = dockImage;
    }
} // namespace

void setIcon(const QIcon& logo)
{
    // Qt hands the Dock its 1x rasters, which it scales up blurry. One
    // 1024px raster at the Dock's 128pt is only ever scaled down. Apple's
    // grid keeps 100px of the 1024 free around the artwork.
    constexpr int canvas = 1024;
    constexpr int margin = 100;
    QImage image(canvas, canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        const int side = canvas - 2 * margin;
        painter.drawPixmap(margin, margin, logo.pixmap(QSize(side, side), 1.0));
    }
    CGImageRef cgImage = image.toCGImage();
    if (!cgImage)
        return;
    [dockImage release];
    dockImage = [[NSImage alloc] initWithCGImage:cgImage size:NSMakeSize(128, 128)];
    CGImageRelease(cgImage);
    apply();
}

void setVisible(bool visible)
{
    const NSApplicationActivationPolicy policy
        = visible ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
    if (NSApp.activationPolicy == policy)
        return;
    [NSApp setActivationPolicy:policy];
    if (visible) {
        // Back in the Dock, the icon is the executable's generic one again.
        apply();
        // A window shown right after the switch stays behind the others
        // unless the app takes the front itself.
        [NSApp activateIgnoringOtherApps:YES];
    }
}

} // namespace Integration::DockIcon
