#include "mac/WizSessionAccess_p.h"
#include "share/WizSessionAccess.h"
#include "share/WizBiometricNoteAccess.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <thread>
#import <AppKit/AppKit.h>

// A distributed-center-shaped in-process fixture: never connects to or posts
// through the system-wide distributed center. Exercises production selectors
// and explicit suspension behavior instead of directly calling systemBoundary.
@interface WizLocalDistributedSessionCenter : NSDistributedNotificationCenter
@property(strong) NSNotificationCenter* local;
@property(strong) NSMutableArray<NSDictionary*>* registrations;
@end
@implementation WizLocalDistributedSessionCenter
- (instancetype)init {
    if((self=[super init])){_local=[NSNotificationCenter new];_registrations=[NSMutableArray new];}return self;
}
- (void)addObserver:(id)observer selector:(SEL)selector name:(NSString*)name object:(NSString*)object
    suspensionBehavior:(NSNotificationSuspensionBehavior)behavior {
    [self.registrations addObject:@{@"name":name,@"behavior":@(behavior)}];
    [self.local addObserver:observer selector:selector name:name object:object];
}
- (void)removeObserver:(id)observer { [self.local removeObserver:observer]; }
@end

void wizSessionNotificationsSelfTest(const WizBiometricNoteAccess::Context& context,
    const std::function<void(bool,const char*)>& check)
{
    using namespace WizBiometricNoteAccess;
    using namespace WizSessionAccess;
    @autoreleasepool {
        NSNotificationCenter* workspace=[NSNotificationCenter new];
        WizLocalDistributedSessionCenter* distributed=[WizLocalDistributedSessionCenter new];
        Monitor monitor(false,nullptr,300000);Session session(&monitor);QObject owner;
        Gate a,b;a.setSession(&session);b.setSession(&session);auto other=context;other.documentGuid+="-other";
        int callbacks=0,hardEvents=0,wrongThread=0;
        QObject::connect(&monitor,&Monitor::accessRetired,&owner,[&](bool hard){if(hard)++hardEvents;});
        const auto stop=subscribeNotifications(&owner,[&]{
            ++callbacks;if(QThread::currentThread()!=owner.thread())++wrongThread;monitor.systemBoundary();
        },workspace,distributed);
        check(bool(stop)&&distributed.registrations.count==2,"native bridge registers both screen-lock and unlock fallback selectors");
        bool immediate=true;for(NSDictionary* registration in distributed.registrations)
            immediate=immediate&&[registration[@"behavior"] integerValue]==NSNotificationSuspensionBehaviorDeliverImmediately;
        check(immediate,"screen notifications bypass AppKit inactive-delivery suspension without changing system settings");
        const auto authorize=[&]{monitor.observeApplicationState(Qt::ApplicationActive);a.begin(context,true);b.begin(other,true);
            return a.grant(context,a.ticket())&&b.grant(other,b.ticket());};
        const auto locked=[&]{return a.requiresAuthorization(true)&&b.requiresAuthorization(true);};
        check(authorize(),"native-bridge fixture independently verifies two synthetic notes");
        monitor.observeApplicationState(Qt::ApplicationInactive);
        [workspace postNotificationName:NSWorkspaceDidActivateApplicationNotification object:nil];
        monitor.observeApplicationState(Qt::ApplicationActive);
        check(callbacks==0&&!locked(),"ordinary application switching neither invokes native hard lock nor skips the five-minute grace");
        for(NSString* name in @[@"com.apple.screenIsLocked",@"com.apple.screenIsUnlocked"]){
            check(authorize(),"fresh synthetic per-note proofs exist before the screen boundary");
            const auto stale=a.ticket();const int before=hardEvents;
            [distributed.local postNotificationName:name object:nil];
            check(locked()&&hardEvents==before+1&&!a.grant(context,stale),"actual subscribed screen selector immediately retires both proofs and rejects pre-lock completion");
            [distributed.local postNotificationName:name object:nil];
            check(hardEvents==before+1,"duplicate screen boundary coalesces before application reactivation");
            monitor.observeApplicationState(Qt::ApplicationActive);a.begin(context,true);b.begin(other,true);
            check(locked(),"returning from screen lock or unlock notification never restores cached authorization");
            check(a.grant(context,a.ticket())&&!a.requiresAuthorization(true)&&b.requiresAuthorization(true),"fresh A verification after screen boundary cannot revive B's old proof");
        }
        for(NSString* name in @[NSWorkspaceWillSleepNotification,NSWorkspaceDidWakeNotification,
            NSWorkspaceSessionDidResignActiveNotification,NSWorkspaceSessionDidBecomeActiveNotification]){
            check(authorize(),"fresh per-note proofs exist before workspace departure or fallback");
            [workspace postNotificationName:name object:nil];
            check(locked(),"production workspace selectors retire access on sleep, wake, session resign and resume");
        }
        check(authorize(),"fixture can establish new proof before cross-thread native delivery");
        const int count=callbacks;
        std::thread worker([&]{@autoreleasepool{[workspace postNotificationName:NSWorkspaceWillSleepNotification object:nil];}});worker.join();
        check(callbacks==count&&!locked(),"off-main notification does not access Qt gates before GUI dispatch");
        // offscreen Qt does not necessarily pump the Cocoa main dispatch
        // queue. Service the native run loop as well; no OS state is changed.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false);
        QCoreApplication::processEvents(QEventLoop::AllEvents,50);
        check(callbacks==count+1&&wrongThread==0&&locked(),"queued native callback reaches Monitor and Session on the GUI thread");
        check(authorize(),"fixture restores only fresh proof before subscription teardown");
        std::thread late([&]{@autoreleasepool{[workspace postNotificationName:NSWorkspaceWillSleepNotification object:nil];}});late.join();
        const int beforeStop=callbacks;stop();stop();
        CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.02,false);
        QCoreApplication::processEvents(QEventLoop::AllEvents,50);
        [distributed.local postNotificationName:@"com.apple.screenIsLocked" object:nil];
        check(callbacks==beforeStop&&!locked(),"idempotent teardown removes selectors and rejects already queued native callbacks");
        auto* disappearing=new QObject;int deadCallbacks=0;
        const auto stopDead=subscribeNotifications(disappearing,[&]{++deadCallbacks;},workspace,distributed);
        delete disappearing;[distributed.local postNotificationName:@"com.apple.screenIsUnlocked" object:nil];
        check(deadCallbacks==0,"native callback cannot call a destroyed Qt owner");stopDead();
        check(!subscribeNotifications(nullptr,[]{},workspace,distributed)
            &&!subscribeNotifications(&owner,{},workspace,distributed),"invalid native subscription does not register observers");
    }
}
