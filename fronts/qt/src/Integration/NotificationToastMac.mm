#include "NotificationToast.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

// Built with ARC (see CMakeLists.txt).

// Shows our notifications while the app is in front too (macOS keeps them
// to Notification Center then by default), and reports a click on one.
@interface CloudMusNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@property (nonatomic, copy) void (^onActivated)(void);
@end

@implementation CloudMusNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier] && self.onActivated)
        self.onActivated();
    completionHandler();
}

@end

namespace Integration {

namespace {

    // Nil outside an app bundle (the test binaries): UserNotifications
    // throws for a process without a bundle identifier.
    UNUserNotificationCenter* notificationCenter()
    {
        if (!NSBundle.mainBundle.bundleIdentifier)
            return nil;
        return UNUserNotificationCenter.currentNotificationCenter;
    }

} // namespace

NotificationToast::NotificationToast(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
    , trayIcon_(trayIcon)
{
    UNUserNotificationCenter* center = notificationCenter();
    if (!center)
        return;
    CloudMusNotificationDelegate* delegate = [[CloudMusNotificationDelegate alloc] init];
    QPointer<NotificationToast> self(this);
    delegate.onActivated = ^{
        // On the center's own queue: over to the Qt thread.
        QMetaObject::invokeMethod(
            qApp,
            [self]() {
                if (self)
                    emit self->activated(QString());
            },
            Qt::QueuedConnection);
    };
    // The center keeps only a weak reference.
    center.delegate = delegate;
    delegate_ = std::shared_ptr<void>(const_cast<void*>(CFBridgingRetain(delegate)), [](void* object) {
        if (notificationCenter().delegate == (__bridge id)object)
            notificationCenter().delegate = nil;
        CFRelease(object);
    });
}

void NotificationToast::showTrackChange(const QString& title, const QString& artist, const QPixmap& cover)
{
    send(track_, { title, artist, cover, 5000 });
}

void NotificationToast::showNotice(const QString& title, const QString& body, const QPixmap& cover)
{
    send(notice_, { title, body, cover, 2500 });
}

void NotificationToast::updateCover(const QString& title, const QString& artist, const QPixmap& cover)
{
    // Only while the banner is still up: a late cover must not bring a
    // notification back once it's gone (the same timeout as the banner's).
    if (track_.sentAt.isValid() && !track_.sentAt.hasExpired(5000))
        showTrackChange(title, artist, cover);
}

void NotificationToast::send(Slot& slot, const Request& request)
{
    UNUserNotificationCenter* center = notificationCenter();
    if (!center)
        return;
    slot.sentAt.start();

    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = request.title.toNSString();
    content.body = request.body.toNSString();
    if (!request.cover.isNull() && coverDir_.isValid()) {
        // A file, which the system moves into its own store: a new one
        // each time.
        const QString path = coverDir_.filePath(QStringLiteral("cover-%1.png").arg(++coverSerial_));
        if (request.cover.save(path, "PNG")) {
            NSURL* url = [NSURL fileURLWithPath:path.toNSString()];
            if (UNNotificationAttachment* attachment = [UNNotificationAttachment attachmentWithIdentifier:@"cover"
                                                                                                      URL:url
                                                                                                  options:nil
                                                                                                    error:nil])
                content.attachments = @[ attachment ];
        }
    }
    // One identifier per slot: a new notification replaces the slot's
    // previous one instead of stacking (skipping through tracks).
    NSString* identifier = &slot == &track_ ? @"track" : @"notice";
    UNNotificationRequest* notification = [UNNotificationRequest requestWithIdentifier:identifier
                                                                               content:content
                                                                               trigger:nil];
    // Asks the user the first time only; denied, nothing is shown.
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                          completionHandler:^(BOOL granted, NSError*) {
                              if (granted)
                                  [center addNotificationRequest:notification withCompletionHandler:nil];
                          }];
}

// org.freedesktop.Notifications' signals: none on macOS.
void NotificationToast::onNotificationClosed(uint, uint) { }
void NotificationToast::onActionInvoked(uint, const QString&) { }
void NotificationToast::onActivationToken(uint, const QString&) { }

} // namespace Integration
