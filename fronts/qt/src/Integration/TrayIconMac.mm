#include "TrayIcon.h"

#include <QCoreApplication>
#include <QCursor>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWheelEvent>

#import <AppKit/AppKit.h>

// Built with ARC (see CMakeLists.txt).

namespace Integration {

namespace {

    QSystemTrayIcon* wheelIcon = nullptr;
    id localMonitor = nil;
    id globalMonitor = nil;
    NSTimeInterval lastWheelTimestamp = -1;

    // The same units Qt uses for QWheelEvent::angleDelta (qnsview_mouse.mm):
    // one notch of a mouse wheel is ±120, and a positive value is up.
    int angleDeltaY(NSEvent* event)
    {
        if (event.hasPreciseScrollingDeltas)
            return int(event.scrollingDeltaY * 2);
        return qBound(-120, int(event.deltaY * 10000), 120);
    }

    void deliverWheel(NSEvent* event)
    {
        if (wheelIcon == nullptr || event.momentumPhase != NSEventPhaseNone)
            return;
        // A scroll can be seen by both monitors; the timestamp is the event's own.
        if (event.timestamp == lastWheelTimestamp)
            return;
        if (!wheelIcon->geometry().contains(QCursor::pos()))
            return;
        const int delta = angleDeltaY(event);
        if (delta == 0)
            return;
        lastWheelTimestamp = event.timestamp;
        const QPointer<QSystemTrayIcon> icon = wheelIcon;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [icon, delta]() {
                if (icon == nullptr)
                    return;
                QWheelEvent wheel(QPointF(), QPointF(), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(icon, &wheel);
            },
            Qt::QueuedConnection);
    }

} // namespace

void TrayIcon::installTrayWheelInput()
{
    wheelIcon = trayIcon_;
    // Local when the event is ours (the status item's window), global when
    // another app is in front and the menu bar keeps the scroll. The two
    // don't both run for one event, and the timestamp covers the overlap.
    if (localMonitor == nil) {
        localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                                             handler:^NSEvent*(NSEvent* event) {
                                                                 deliverWheel(event);
                                                                 if (event.timestamp == lastWheelTimestamp)
                                                                     return nil;
                                                                 return event;
                                                             }];
    }
    if (globalMonitor == nil) {
        globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                                               handler:^(NSEvent* event) { deliverWheel(event); }];
    }
}

void TrayIcon::removeTrayWheelInput()
{
    if (wheelIcon == trayIcon_)
        wheelIcon = nullptr;
    if (localMonitor != nil) {
        [NSEvent removeMonitor:localMonitor];
        localMonitor = nil;
    }
    if (globalMonitor != nil) {
        [NSEvent removeMonitor:globalMonitor];
        globalMonitor = nil;
    }
}

} // namespace Integration
