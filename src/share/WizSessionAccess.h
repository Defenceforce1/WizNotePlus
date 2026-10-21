#pragma once
#include <QObject>
#include <QPointer>
#include <Qt>
#include <functional>

namespace WizSessionAccess {
// Public workspace sleep/session-resign plus Qt application visibility changes.
// Activity resumption only rearms notification coalescing; it never grants access.
class Monitor final : public QObject {
    Q_OBJECT
public:
    explicit Monitor(bool subscribeToNative=true,QObject* parent=nullptr);
    ~Monitor() override;
    static Monitor* applicationMonitor();
    void observeApplicationState(Qt::ApplicationState state);
    void systemBoundary(bool hard=true);
    void beginShutdown();
Q_SIGNALS:
    void accessRetired(bool hard);
private:
    bool m_closed=false,m_retired=false,m_hardRetired=false;
    std::function<void()> m_unsubscribe;
};
// Native registration is separate so isolated tests never invoke OS sessions.
std::function<void()> subscribeWorkspace(QObject* context,std::function<void()> retired);
}
