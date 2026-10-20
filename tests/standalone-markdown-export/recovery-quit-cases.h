#pragma once

#include "share/WizQuitUiGuard.h"
#include "share/WizQuitSyncPolicy.h"
#include <QFileOpenEvent>
#include <QProcess>
#include <QScopedValueRollback>

// Only collaborators are synthetic. The four method bodies below are extracted
// from the current MainWindow, not a handwritten copy of its quit algorithm.
struct QuitSaver {
    bool idle = true, error = false;
    bool isIdleForTransfer() const { return idle; }
    bool hasTransferError() const { return error; }
    void clearTransferError() { error = false; }
};
class WizDocumentView : public QWidget {
public:
    using QWidget::QWidget;
    std::function<bool(bool)> save = [](bool) { return true; };
    bool waitForSave(bool force) { return save(force); }
};
struct QuitViewer { WizDocumentView* view; WizDocumentView* docView() const { return view; } };
struct QuitViewManager {
    QMap<int, QuitViewer*> getDocumentViewerMap() const { return {}; }
};
class WizKMSyncThread : public QThread {
public:
    int attempts = 0, stops = 0;
    bool running = true;
    bool isRunning() const { return running; }
    bool isIdle() const { return true; }
    bool hasNetworkStarted() const { return true; }
    quint64 networkActivity() const { return 1; }
    void startSyncAll(bool) { ++attempts; QMetaObject::invokeMethod(this, "finished", Qt::DirectConnection); }
    void stopSync() { ++stops; }
};
namespace WizNetworkSecurity {
inline int quitNotifications = 0;
inline void beginShutdown() { ++quitNotifications; } // No security/network code linked.
}
struct QuitMessageBox {
    static inline int errors = 0;
    static void critical(QWidget*, const QString&, const QString&) { ++errors; }
};
struct QuitAnalyzer { void logAction(const char*) {} };
inline QuitAnalyzer& WizGetAnalyzer() { static QuitAnalyzer analyzer; return analyzer; }
inline bool IsWizKMURL(const QString&) { return false; }
class QuitHarness : public QWidget {
public:
    using _baseClass = QWidget;
    bool m_quitPrepared = false, m_preparingQuit = false, m_noteTransferBusy = false, m_quiting = false;
    QuitSaver saver;
    QuitSaver* m_docSaver = &saver;
    QuitViewManager manager;
    QuitViewManager* m_singleViewMgr = &manager;
    WizKMSyncThread full, quick;
    WizKMSyncThread* m_syncFull = &full;
    WizKMSyncThread* m_syncQuick = &quick;
    WizDocumentView view;
    std::function<void()> cleanup;
    bool eventFilter(QObject*, QEvent*) override;
    void on_application_aboutToQuit();
    bool prepareForQuit();
    void on_actionExit_triggered();
    void cleanOnQuit() { if (cleanup) cleanup(); }
    QWidget* centralWidget() { return &view; }
    void processAllDocumentViews(const std::function<void(WizDocumentView*)>& action) { action(&view); }
    void viewDocumentByWizKMURL(const QString&) {}
    bool processApplicationActiveEvent() { return false; }
};
#include "actual-quit-flow.inc"

namespace QuitCases {
namespace App = WizMarkdownExportRecoveryApp;

inline int child(QApplication& application, const QString& sandbox, const QString& mode)
{
    WizQueuedThreadsSession queues([] {});
    Fixture f(sandbox);
    QuitHarness window;
    WizDocumentCallbackEpoch document;
    Session session(&window, document.ticket(), f.target, f.inspection, LockState::Available);
    int uiResults = 0, settled = 0, cleanups = 0, saves = 0, receipts = 0;
    QObject::connect(&session, &Session::resultReady, qApp, [&](const Result&) { ++uiResults; });
    QObject::connect(&session, &Session::settled, qApp, [&] { ++settled; });
    show(session);
    QSemaphore held, release;
    Work work;
    const bool queued = mode == "queued-confirm" || mode == "queued-retain";
    const bool running = mode == "running-confirm";
    std::unique_ptr<Hold> hold;
    if (queued) {
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "quit fixture holds only its own default worker");
    }
    if (running) hold = std::make_unique<Hold>(sandbox, "intent-recorded");
    if (mode == "queued-retain") session.prompt()->reject();
    else if (queued || running) work = click(session);
    if (running) check(pump([&] { return QFileInfo::exists(hold->ready); }), "confirmed backend starts before direct quit");
    if (mode == "queued-confirm") check(work.stage() == Stage::Queued, "confirmation queued before direct quit");

    const bool saveFails = mode == "save-fails" || mode == "late-save-fails";
    window.view.save = [&](bool force) {
        ++saves;
        check(App::isQuitting() && !App::open(&window, document.ticket(), f.target, {}, {}),
            "actual save preparation already blocks recovery UI and new exports");
        // A real GUI receipt still runs; the recovery guard is not applied to
        // document persistence acknowledgements.
        QTimer::singleShot(0, qApp, [&] { ++receipts; window.saver.idle = true; });
        window.saver.idle = false;
        return !(mode == "save-fails" || (mode == "late-save-fails" && !force));
    };
    window.cleanup = [&] {
        ++cleanups;
        check(App::isQuitting() && !App::open(&window, document.ticket(), f.target, {}, {}),
            "actual aboutToQuit slot closes recovery gate before cleanup event pumping");
        check(!session.prompt() || !session.prompt()->isVisible(), "pending prompt is retained before cleanup");
        if (mode == "queued-confirm") check(work.stage() == Stage::Retained, "quit cancels confirmed work before backend start");
        if (running) {
            check(work.stage() == Stage::Running, "quit does not abort an already-started coordinator");
            hold->release();
        }
        if (queued) release.release();
        WizQueuedThreadsShutdown(); // Existing join policy, no new quit wait.
        QApplication::processEvents();
    };
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &window,
        [&] { window.on_application_aboutToQuit(); });
    if (mode == "quit-event" || mode == "close-event") application.installEventFilter(&window);
    QTimer rescue;
    rescue.setSingleShot(true);
    QObject::connect(&rescue, &QTimer::timeout, qApp, [&] {
        check(false, "finite synthetic quit must not require rescue");
        if (queued) release.release();
        if (hold) hold->release();
        application.exit(3);
    });
    rescue.start(8000);
    QTimer::singleShot(0, qApp, [&] {
        if (saveFails) {
            window.on_actionExit_triggered();
            QApplication::processEvents();
            check(!window.m_quitPrepared && !window.m_quiting && !App::isQuitting()
                && cleanups == 0 && QuitMessageBox::errors == 1 && receipts == saves,
                "actual failed save cancels quit, delivers save receipts and restores future request gate");
            check(window.full.attempts == (mode == "late-save-fails" ? 1 : 0)
                && f.retained() && f.noAudit() && settled == 1,
                "failed save preserves files, performs no recovery or retry, and never revives old prompt");
            // End only this isolated event loop after observing the cancelled
            // attempt. The final direct quit is covered by the fallback too.
            application.exit();
        } else if (mode == "menu") window.on_actionExit_triggered();
        else if (mode == "quit-event") QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
        else if (mode == "close-event") QCoreApplication::postEvent(qApp, new QEvent(QEvent::Close));
        else application.exit(); // Direct exit bypasses the Quit event filter.
    });
    const int exit = application.exec();
    rescue.stop();
    application.removeEventFilter(&window);
    check(exit == 0 && cleanups == 1 && App::isQuitting() && uiResults == 0,
        "actual exit route completes once without late recovery UI delivery");
    if (running) {
        const auto result = work.completedResult();
        check(result && result->recovery == RecoveryOutcome::RolledBack && result->audit == AuditOutcome::Recorded
            && f.oldPair() && f.audited(QStringLiteral("rolled-back")),
            "started recovery completes with independent files and audit during existing queue cleanup");
    } else {
        check(f.retained() && f.noAudit(), "pending/queued quit preserves exact site without starting audit");
        if (mode == "queued-confirm") check(work.stage() == Stage::Retained && !work.completedResult(), "cancelled queued confirmation has no invented result");
    }
    const bool preparedRoute = mode == "menu" || mode == "quit-event" || mode == "close-event";
    if (preparedRoute) check(window.m_quitPrepared && window.m_quiting && saves == 2 && receipts == 2
        && window.full.attempts == 1 && window.full.stops == 1 && window.quick.stops == 1,
        "actual prepared exit keeps two save barriers and exactly one synthetic final sync");
    else if (!saveFails) check(saves == 0 && window.full.attempts == 0,
        "direct fallback adds no local save or final sync attempt");
    QTextStream(stdout) << "QUIT_CHILD_MODE=" << mode << '\n' << "QUIT_CHILD_CHECKS=" << checks << '\n'
        << "QUIT_CHILD_FAILURES=" << failures << Qt::endl;
    return failures ? 1 : 0;
}

inline int entry(QApplication& application, const QString& sandbox)
{
    const auto args = application.arguments();
    if (args.size() == 3 && args.at(1) == "--quit-child") return child(application, sandbox, args.at(2));
    const QStringList modes{"direct", "menu", "quit-event", "close-event", "save-fails",
        "late-save-fails", "queued-confirm", "running-confirm", "queued-retain"};
    int childChecks = 0;
    for (const auto& mode : modes) {
        QProcess process;
        process.start(application.applicationFilePath(), {QStringLiteral("--quit-child"), mode});
        const bool finished = process.waitForStarted(3000) && process.waitForFinished(15000);
        if (!finished) { process.kill(); process.waitForFinished(3000); }
        const QByteArray out = process.readAllStandardOutput(), error = process.readAllStandardError();
        QTextStream(stdout) << out << error;
        bool countFound = false;
        for (const auto& line : out.split('\n')) if (line.startsWith("QUIT_CHILD_CHECKS=")) {
            childChecks += line.mid(18).toInt(); countFound = true;
        }
        check(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
            && countFound && out.contains("QUIT_CHILD_FAILURES=0"), "isolated actual exit-entry child passed");
    }
    QTextStream(stdout) << "QUIT_ENTRY_SOURCE_SHA256=" << QUIT_SOURCE_SHA << '\n'
        << "QUIT_ENTRY_CHECKS=" << checks + childChecks << '\n'
        << "QUIT_ENTRY_CHILDREN=" << modes.size() << '\n'
        << "QUIT_ENTRY_FAILURES=" << failures << Qt::endl;
    return failures ? 1 : 0;
}
}
