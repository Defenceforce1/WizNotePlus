#pragma once
#include <QtGlobal>

namespace WizQuitSyncPolicy {
constexpr int startTimeoutMs = 3000;
constexpr int noProgressTimeoutMs = 10000;
enum class Decision { Wait, Finished, StartTimedOut, Stalled };

// Pure monotonic-time policy, also exercised with a synthetic clock. A sync
// that starts promptly is NOT canceled just because total time exceeds 3 s.
class Wait final {
public:
    Decision observe(qint64 elapsedMs, bool started, quint64 activity, bool finished) {
        if (finished) return Decision::Finished;
        if (!m_started) {
            if (started) {
                m_started = true; m_lastProgressMs = elapsedMs; m_activity = activity;
            } else if (elapsedMs >= startTimeoutMs) return Decision::StartTimedOut;
        }
        if (m_started) {
            if (activity != m_activity) { m_activity = activity; m_lastProgressMs = elapsedMs; }
            if (elapsedMs - m_lastProgressMs >= noProgressTimeoutMs) return Decision::Stalled;
        }
        return Decision::Wait;
    }
private:
    bool m_started = false;
    quint64 m_activity = 0;
    qint64 m_lastProgressMs = 0;
};
}
