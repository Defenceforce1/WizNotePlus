#include "share/WizSessionAccess.h"
#include <QThread>
#import <AppKit/AppKit.h>

namespace WizSessionAccess {
std::function<void()> subscribeWorkspace(QObject* owner,std::function<void()> retired)
{
    if(!owner||!retired||QThread::currentThread()!=owner->thread())return {};
    // Main-queue notifications keep all QPointer access on the GUI thread.
    const QPointer<QObject> context(owner);
    NSNotificationCenter* center=NSWorkspace.sharedWorkspace.notificationCenter;
    void (^callback)(NSNotification*)=^(NSNotification*){if(context)retired();};
    id sleep=[center addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:callback];
    id session=[center addObserverForName:NSWorkspaceSessionDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:callback];
    return [center,sleep,session]{[center removeObserver:sleep];[center removeObserver:session];};
}
}
