#pragma once
#include "WizThreads.h"
#include <functional>
#include <utility>

// Declare after QApplication and before any account windows. Early startup
// returns must stop workers before the application dispatcher is destroyed,
// just as the main window's ordinary cleanup already does.
class WizQueuedThreadsSession final {
public:
    explicit WizQueuedThreadsSession(std::function<void()> beforeShutdown)
        : m_beforeShutdown(std::move(beforeShutdown)) { WizQueuedThreadsInit(); }
    ~WizQueuedThreadsSession() {
        if (m_beforeShutdown) m_beforeShutdown();
        WizQueuedThreadsShutdown();
    }
    WizQueuedThreadsSession(const WizQueuedThreadsSession&) = delete;
    WizQueuedThreadsSession& operator=(const WizQueuedThreadsSession&) = delete;
private:
    std::function<void()> m_beforeShutdown;
};
