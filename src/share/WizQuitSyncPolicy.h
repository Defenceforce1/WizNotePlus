#pragma once
#include <QtGlobal>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPointer>

namespace WizQuitSyncPolicy {
// Installed only for an already-started quit preparation. The first Quit is
// handled normally. Repeating it skips cloud waiting, without killing a saver
// or recursively entering cleanup. A held key is not a second explicit Quit.
class CancelOnRepeatQuit final : public QObject {
public:
    explicit CancelOnRepeatQuit(QCoreApplication* app) : m_app(app) {
        if (m_app) m_app->installEventFilter(this);
    }
    ~CancelOnRepeatQuit() override { if (m_app) m_app->removeEventFilter(this); }
    bool requested() const { return m_requested; }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            // WizActions binds Exit explicitly to Ctrl+Q (Command+Q under
            // Qt's normal macOS mapping). Some themes have no StandardKey
            // binding for Quit, so match the actual application binding too.
            if (key->matches(QKeySequence::Quit)
                || (key->key() == Qt::Key_Q && key->modifiers() == Qt::ControlModifier)) {
                // Frozen editor widgets can suppress the subsequent KeyPress.
                // ShortcutOverride still arrives before shortcut dispatch.
                if (!key->isAutoRepeat()) m_requested = true;
                event->accept();
                return true;
            }
        }
        if (watched == m_app && (event->type() == QEvent::Quit || event->type() == QEvent::Close)) {
            m_requested = true;
            event->ignore();
            return true;
        }
        return false;
    }
private:
    QPointer<QCoreApplication> m_app;
    bool m_requested = false;
};

constexpr int startTimeoutMs = 3000;
constexpr int noProgressTimeoutMs = 3000;
enum class Decision { Wait, Finished, StartTimedOut, Stalled };

// Pure monotonic-time policy, also exercised with a synthetic clock. A sync
// that starts promptly is NOT canceled just because total time exceeds 3 s.
class Wait final {
public:
    Decision observe(qint64 elapsedMs, bool started, quint64 activity, bool finished) {
        // A timed-out/finished attempt is terminal. New activity cannot revive
        // it or implicitly schedule another quit-time network attempt.
        if (m_decision != Decision::Wait) return m_decision;
        if (finished) return m_decision = Decision::Finished;
        if (!m_started) {
            if (started) {
                m_started = true; m_lastProgressMs = elapsedMs; m_activity = activity;
            } else if (elapsedMs >= startTimeoutMs) return m_decision = Decision::StartTimedOut;
        }
        if (m_started) {
            if (activity != m_activity) { m_activity = activity; m_lastProgressMs = elapsedMs; }
            if (elapsedMs - m_lastProgressMs >= noProgressTimeoutMs) return m_decision = Decision::Stalled;
        }
        return Decision::Wait;
    }
private:
    bool m_started = false;
    Decision m_decision = Decision::Wait;
    quint64 m_activity = 0;
    qint64 m_lastProgressMs = 0;
};
}
