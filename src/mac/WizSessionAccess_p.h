#pragma once
#include <QObject>
#include <functional>
#ifdef __OBJC__
@class NSNotificationCenter;
@class NSDistributedNotificationCenter;
namespace WizSessionAccess {
// Private injection seam: tests use in-process centers, never post system-wide
// lock notifications or change the real user's session.
std::function<void()> subscribeNotifications(QObject* owner,std::function<void()> retired,
    NSNotificationCenter* workspace,NSDistributedNotificationCenter* distributed);
}
#endif
