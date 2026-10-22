#include "share/WizSessionAccess.h"
#include "WizSessionAccess_p.h"
#include <QThread>
#include <atomic>
#include <memory>
#import <AppKit/AppKit.h>

@interface WizSessionNotificationObserver : NSObject
@property(copy) void (^handler)(NSNotification*);
- (void)receive:(NSNotification*)notification;
@end
@implementation WizSessionNotificationObserver
- (void)receive:(NSNotification*)notification { if(self.handler)self.handler(notification); }
@end

namespace WizSessionAccess {
std::function<void()> subscribeNotifications(QObject* owner,std::function<void()> retired,
    NSNotificationCenter* workspace,NSDistributedNotificationCenter* distributed)
{
    if(!owner||!retired||!workspace||!distributed||![NSThread isMainThread]
        ||QThread::currentThread()!=owner->thread())return {};
    const QPointer<QObject> context(owner);
    const auto live=std::make_shared<std::atomic_bool>(true);
    WizSessionNotificationObserver* observer=[WizSessionNotificationObserver new];
    observer.handler=^(NSNotification*){
        // Workspace callbacks may originate off-main; never touch QPointer or
        // gates there. Unsubscription also cancels already queued deliveries.
        void (^deliver)(void)=^{if(live->load()&&context)retired();};
        if([NSThread isMainThread])deliver();
        else dispatch_async(dispatch_get_main_queue(),deliver);
    };
    for(NSString* name in @[NSWorkspaceWillSleepNotification,NSWorkspaceDidWakeNotification,
        NSWorkspaceSessionDidResignActiveNotification,NSWorkspaceSessionDidBecomeActiveNotification])
        [workspace addObserver:observer selector:@selector(receive:) name:name object:nil];
    // Screen locking need not resign the NSWorkspace user session. These are
    // observed macOS notification names, not SDK-guaranteed lock-state APIs.
    // Treat them only as a request to revoke access; never trust payloads or
    // grant access on unlock. Resume/wake cover a missed departure delivery.
    for(NSString* name in @[@"com.apple.screenIsLocked",@"com.apple.screenIsUnlocked"])
        [distributed addObserver:observer selector:@selector(receive:) name:name object:nil
            suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
    return [workspace,distributed,observer,live]{
        if(!live->exchange(false))return;
        [workspace removeObserver:observer];[distributed removeObserver:observer];
    };
}
std::function<void()> subscribeWorkspace(QObject* owner,std::function<void()> retired)
{
    return subscribeNotifications(owner,std::move(retired),NSWorkspace.sharedWorkspace.notificationCenter,
        NSDistributedNotificationCenter.defaultCenter);
}
}
