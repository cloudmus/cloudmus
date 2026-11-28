#include "DockIcon.h"

#include <QBrush>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QTransform>

#import <AppKit/AppKit.h>

namespace Integration::DockIcon {
namespace {
    std::function<void()> reopenHandler;
}
} // namespace Integration::DockIcon

// Receives the reopen Apple Event for setReopenHandler().
@interface CloudMusReopenTarget : NSObject
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation CloudMusReopenTarget
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply
{
    if (Integration::DockIcon::reopenHandler)
        Integration::DockIcon::reopenHandler();
}
@end

namespace Integration::DockIcon {

namespace {
    CloudMusReopenTarget* reopenTarget = nil;

    // Qt hands the Dock its 1x rasters, which it scales up blurry. The icon
    // is drawn at 1024px instead, on Apple's grid (100px of the 1024 free
    // around the artwork), and handed over at every size an .icns has: the
    // Dock and Cmd+Tab pick one close to what they draw, rather than
    // shrinking the 1024px one jagged.
    constexpr int canvas = 1024;
    constexpr int margin = 100;
    constexpr int sizes[] = { 16, 32, 64, 128, 256, 512, 1024 };

    QIcon logoIcon;
    NSImage* logoImage = nil;
    NSImage* coverImage = nil;

    void apply()
    {
        if (NSImage* image = coverImage ? coverImage : logoImage)
            NSApp.applicationIconImage = image;
    }

    // A retained image (the caller releases it) of the canvas at each of
    // `sizes`, nil if it can't be made.
    NSImage* toDockImage(const QImage& image)
    {
        NSImage* result = [[NSImage alloc] initWithSize:NSMakeSize(128, 128)];
        for (const int pixels : sizes) {
            const QImage scaled = pixels == image.width()
                ? image
                : image.scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            CGImageRef cgImage = scaled.toCGImage();
            if (!cgImage)
                continue;
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
            CGImageRelease(cgImage);
            rep.size = NSMakeSize(pixels, pixels);
            [result addRepresentation:rep];
            [rep release];
        }
        if (result.representations.count == 0) {
            [result release];
            return nil;
        }
        return result;
    }
} // namespace

void setIcon(const QIcon& logo)
{
    logoIcon = logo;
    QImage image(canvas, canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        const int side = canvas - 2 * margin;
        painter.drawPixmap(margin, margin, logo.pixmap(QSize(side, side), 1.0));
    }
    NSImage* next = toDockImage(image);
    if (!next)
        return;
    [logoImage release];
    logoImage = next;
    apply();
}

void setCover(const QPixmap& cover)
{
    NSImage* next = nil;
    if (!cover.isNull()) {
        QImage image(canvas, canvas, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // The rounded square of the icons around it (Apple's corner is
        // ~22% of the side).
        const int side = canvas - 2 * margin;
        const QRectF tile(margin, margin, side, side);
        QPainterPath shape;
        shape.addRoundedRect(tile, side * 0.2237, side * 0.2237);
        // Filled with the cover rather than clipped to the shape: Qt's
        // clipping isn't antialiased, and the corners came out stepped.
        QBrush brush(cover.toImage().scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        brush.setTransform(QTransform::fromTranslate(margin, margin));
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
        painter.drawPath(shape);
        // The logo in the bottom right corner, a little over the edge, as
        // the Dock places badges: whose cover it is.
        const int badge = side * 3 / 8;
        const int offset = canvas - margin - badge + margin / 2;
        painter.drawPixmap(offset, offset, logoIcon.pixmap(QSize(badge, badge), 1.0));
        painter.end();
        next = toDockImage(image);
    }
    [coverImage release];
    coverImage = next;
    if (coverImage || logoImage)
        apply();
}

void setReopenHandler(std::function<void()> handler)
{
    reopenHandler = std::move(handler);
    if (reopenTarget != nil)
        return;
    reopenTarget = [[CloudMusReopenTarget alloc] init];
    // AppKit installs its own handler while it finishes launching, which
    // the event loop does: ours goes in once that has run.
    QTimer::singleShot(0, [] {
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler:reopenTarget
                                                           andSelector:@selector(handleReopen:withReplyEvent:)
                                                         forEventClass:kCoreEventClass
                                                            andEventID:kAEReopenApplication];
    });
}

} // namespace Integration::DockIcon
