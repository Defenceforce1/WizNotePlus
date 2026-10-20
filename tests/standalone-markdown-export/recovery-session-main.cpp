#include "widgets/WizMarkdownExportRecoverySession.h"
#include "widgets/WizMarkdownExportRecoveryApp.h"
#include "widgets/WizMarkdownExportRecoveryPrompt.h"
#include "share/WizMarkdownExportRecoveryAudit.h"
#include "share/WizQueuedThreadsSession.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPushButton>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <atomic>

using namespace WizMarkdownExportRecovery;
using Session = WizMarkdownExportRecoverySession;
using Work = WizMarkdownExportRecoveryRequest::Work;
using Stage = WizMarkdownExportRecoveryRequest::Stage;
namespace Audit = WizMarkdownExportRecoveryAudit;
static int checks = 0, failures = 0, caseNumber = 0;
static void check(bool value, const char* label)
{
    ++checks;
    if (!value) {
        ++failures;
        QTextStream(stderr) << "FAIL: " << label << Qt::endl;
    }
}
static bool write(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.flush();
}
static QByteArray read(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static bool pump(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 4000) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}
static QByteArray snapshot(const QString& root)
{
    QDir directory(root);
    QStringList paths;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (it.hasNext())
        paths << directory.relativeFilePath(it.next());
    paths.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& path : paths) {
        const QString full = directory.filePath(path);
        const QFileInfo info(full);
        const QByteArray bytes = info.isDir() ? QByteArray("DIRECTORY") : read(full);
        hash.addData(path.toUtf8() + '\0' + QByteArray::number(info.permissions()) + '\0'
            + QByteArray::number(bytes.size()) + ':' + bytes);
    }
    return hash.result().toHex();
}
struct Fixture {
    QString root, target, resources, recovery, auditBase;
    Inspection inspection;
    QByteArray before;
    explicit Fixture(const QString& sandbox)
    {
        const QString name = QStringLiteral("case-%1").arg(++caseNumber);
        root = QDir(sandbox).filePath(name);
        auditBase = QDir(sandbox).filePath(name + QStringLiteral("-audit"));
        check(QDir().mkpath(root), "synthetic case parent created before identity");
        target = QDir(root).filePath(QStringLiteral("note.md"));
        resources = QDir(root).filePath(QStringLiteral("note_files"));
        check(QDir().mkpath(resources) && write(target, "OLD MARKDOWN\n")
            && write(QDir(resources).filePath(QStringLiteral("old.bin")), "OLD RESOURCE"), "synthetic old pair created");
        const auto prepared = prepare(target, "NEW MARKDOWN\n", [](const QString& html) {
            const QString staged = QDir(QFileInfo(html).absolutePath()).filePath(QStringLiteral("note_files"));
            return QDir().mkpath(staged) && write(html, "<p>new</p>")
                && write(QDir(staged).filePath(QStringLiteral("new.bin")), "NEW RESOURCE");
        });
        recovery = prepared.recoveryDirectory;
        check(prepared.outcome == PreparationOutcome::Prepared, "real recovery preparation succeeds");
        check(QDir().rename(resources, QDir(recovery).filePath(QStringLiteral("previous-resources")))
            && QDir().rename(QDir(recovery).filePath(QStringLiteral("content/note_files")), resources),
            "synthetic interrupted install has old backup and live new resources");
        inspection = inspect(target);
        check(inspection.availability == Availability::AwaitingDecision
            && preflight(target).evidenceState == QStringLiteral("new-resources-installed"), "actual inspection recognizes interrupted state");
        before = snapshot(root);
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", auditBase.toUtf8());
    }
    bool oldPair() const
    {
        return read(target) == "OLD MARKDOWN\n"
            && read(QDir(resources).filePath(QStringLiteral("old.bin"))) == "OLD RESOURCE"
            && !QFileInfo::exists(QDir(resources).filePath(QStringLiteral("new.bin")));
    }
    bool retained() const { return snapshot(root) == before; }
    bool noAudit() const { return !QFileInfo::exists(auditBase); }
    bool retainAudited() const
    {
        return read(Audit::auditLogPath()) == QJsonDocument(QJsonObject{
            {QStringLiteral("destinationIdentity"), inspection.destinationIdentity},
            {QStringLiteral("decision"), QStringLiteral("retain")},
            {QStringLiteral("event"), QStringLiteral("retained")}
        }).toJson(QJsonDocument::Compact) + '\n';
    }
    bool audited(const QString& finalEvent) const
    {
        const auto lines = read(Audit::auditLogPath()).trimmed().split('\n');
        if (lines.size() != 2)
            return false;
        for (int i = 0; i < 2; ++i) {
            const auto object = QJsonDocument::fromJson(lines.at(i)).object();
            if (object.size() != 3 || object.value(QStringLiteral("destinationIdentity")) != inspection.destinationIdentity
                || object.value(QStringLiteral("decision")) != QStringLiteral("confirm")
                || object.value(QStringLiteral("event")) != (i ? finalEvent : QStringLiteral("requested")))
                return false;
        }
        return true;
    }
};
static QPushButton* confirm(Session& session)
{
    auto* prompt = session.prompt();
    auto* button = prompt ? prompt->findChild<QPushButton*>(QStringLiteral("recoveryPromptConfirm")) : nullptr;
    if (!button)
        qFatal("synthetic confirmation button missing");
    return button;
}
static void show(Session& session)
{
    session.show();
    if (session.prompt())
        session.prompt()->activateWindow();
    QApplication::processEvents();
}
static Work click(Session& session)
{
    // Exercise the actual button signal without a QTest mouse-delay event pump:
    // a result receiver may delete the session before this helper can return.
    // Real mouse/keyboard behavior is already covered by the prompt target.
    confirm(session)->click();
    return session.work();
}
static bool complete(const Work& work) { return work.stage() == Stage::Completed; }
static void queueBarrier()
{
    std::atomic_bool done {false};
    WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { done.store(true); });
    const bool passed = pump([&] { return done.load(); });
    check(passed, "actual default queue drained to finite barrier");
    if (!passed)
        qFatal("queue barrier timed out; do not destroy captured state");
    QApplication::processEvents();
}
static void pendingAndQueued(const QString& sandbox)
{
    for (int action = 0; action < 3; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        int settled = 0;
        QObject::connect(&session, &Session::settled, qApp, [&] { ++settled; });
        show(session);
        if (action == 0)
            session.beginQuit();
        else if (action == 1)
            QTest::keyClick(session.prompt(), Qt::Key_Escape);
        else {
            document.invalidate();
            click(session); // Late UI after a changed generation must not dispatch.
        }
        queueBarrier();
        check(!session.work() && f.retained() && (action == 1 ? f.retainAudited() : f.noAudit()),
            "pending quit/stale skips audit; ordinary escape only records retain without site changes");
        check(settled == 1, "retained choice notifies application cleanup once");
    }
    for (bool quit : {true, false}) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "existing queue held before recovery dispatch");
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(session);
        auto work = click(session);
        check(work && work.stage() == Stage::Queued, "confirmed session queued behind actual pool task");
        if (quit)
            session.beginQuit();
        else {
            session.invalidate();
            document.invalidate();
        }
        release.release();
        queueBarrier();
        check(work.stage() == Stage::Retained && !work.completedResult() && f.retained() && f.noAudit(),
            "queued exit/navigation skips strict backend and preserves entire site");
    }
}
static void retainAuditPrimitives(const QString& sandbox)
{
    using Audit::AppendOutcome;
    Fixture f(sandbox);
    const QString identity = f.inspection.destinationIdentity;
    const auto line = [&](const QString& decision, const QString& event) {
        return QJsonDocument(QJsonObject{{QStringLiteral("destinationIdentity"), identity},
            {QStringLiteral("decision"), decision}, {QStringLiteral("event"), event}})
            .toJson(QJsonDocument::Compact) + '\n';
    };
    const auto retained = line(QStringLiteral("retain"), QStringLiteral("retained"));
    const auto intent = line(QStringLiteral("confirm"), QStringLiteral("requested"));
    for (const QString& invalid : {QString(), f.target, QString(24, QLatin1Char('A'))})
        check(Audit::appendRetain(invalid).outcome == AppendOutcome::Refused && f.noAudit() && f.retained(),
            "invalid retain identity refuses before any audit I/O or destination access");
    check(Audit::appendRetain(identity).outcome == AppendOutcome::Recorded && f.retainAudited()
        && f.retained() && !QFileInfo::exists(Audit::auditLockPath()), "retain primitive writes exactly three canonical fields and releases audit lock");
    const auto publicBits = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    check(!(QFileInfo(Audit::auditDirectory()).permissions() & publicBits)
        && !(QFileInfo(Audit::auditLogPath()).permissions() & (publicBits | QFileDevice::ExeOwner))
        && !read(Audit::auditLogPath()).contains(f.root.toUtf8()), "retain audit remains private and contains no note paths");
    Result refused;
    refused.recovery = RecoveryOutcome::Refused;
    check(Audit::appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
        && Audit::appendConfirmResult(identity, refused).outcome == AppendOutcome::Recorded
        && read(Audit::auditLogPath()) == retained + intent + line(QStringLiteral("confirm"), QStringLiteral("refused"))
        && refused.audit == AuditOutcome::NotRequested && f.retained(), "confirm readers preserve retain history without changing independent result or site");
    QByteArray full;
    for (int i = 0; i < 256; ++i)
        full += i % 2 ? retained : intent;
    check(write(Audit::auditLogPath(), full) && Audit::appendRetain(identity).outcome == AppendOutcome::Recorded
        && read(Audit::auditArchivePath(1)) == full && f.retainAudited()
        && Audit::appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
        && read(Audit::auditLogPath()) == retained + intent && f.retained(), "mixed confirm/retain history rotates intact and accepts subsequent confirmation");
    const auto invalidLines = QList<QByteArray>{line(QStringLiteral("confirm"), QStringLiteral("retained")),
        line(QStringLiteral("retain"), QStringLiteral("requested")), line(QStringLiteral("retain"), QStringLiteral("rolled-back")),
        line(QStringLiteral("cancel"), QStringLiteral("retained")),
        QJsonDocument(QJsonObject{{QStringLiteral("destinationIdentity"), identity},
            {QStringLiteral("decision"), QStringLiteral("retain")}, {QStringLiteral("event"), 99}}).toJson(QJsonDocument::Compact) + '\n'};
    for (const auto& invalid : invalidLines)
        check(write(Audit::auditLogPath(), invalid) && Audit::appendRetain(identity).outcome == AppendOutcome::Refused
            && read(Audit::auditLogPath()) == invalid && read(Audit::auditArchivePath(1)) == full && f.retained(),
            "unsupported decision/event combinations refuse while preserving history and recovery evidence");
    for (const char* point : {"before-live-commit", "live-committed", "live-file-synced", "live-directory-synced"}) {
        check(write(Audit::auditLogPath(), intent), "retain failure fixture restored");
        qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", point);
        const auto result = Audit::appendRetain(identity);
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT");
        check(result.outcome == AppendOutcome::Failed && read(Audit::auditLogPath())
            == (QByteArray(point) == "before-live-commit" ? intent : intent + retained)
            && !QFileInfo::exists(Audit::auditLockPath()) && f.retained(), "retain failure reports uncertain logging without retrying or changing site");
    }
    qputenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE", "1");
    const auto releaseFailure = Audit::appendRetain(identity);
    qunsetenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE");
    check(releaseFailure.outcome == AppendOutcome::Failed && read(Audit::auditLogPath()) == intent + retained + retained
        && f.retained(), "retain unlock uncertainty does not invent recovery or undo committed audit bytes");
    const auto before = read(Audit::auditLogPath());
    QLockFile lock(Audit::auditLockPath());
    lock.setStaleLockTime(0);
    check(lock.tryLock(0) && Audit::appendRetain(identity).outcome == AppendOutcome::Busy
        && read(Audit::auditLogPath()) == before && f.retained(), "retain contention returns busy without retry or destination mutation");
    lock.unlock();
    check(write(Audit::auditLockPath(), "malformed retain fixture")
        && Audit::appendRetain(identity).outcome == AppendOutcome::Refused
        && read(Audit::auditLockPath()) == "malformed retain fixture" && read(Audit::auditLogPath()) == before
        && f.retained(), "retain never takes over an untrusted audit lock");
}
static void bestEffortRetain(const QString& sandbox)
{
    // Plain UI retention settles before optional logging; neither success nor
    // failure is a recovery result or a condition of application cleanup.
    for (int action = 0; action < 4; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        int settled = 0, delivered = 0;
        QObject::connect(&session, &Session::settled, qApp, [&] { ++settled; });
        QObject::connect(&session, &Session::resultReady, qApp, [&](const Result&) { ++delivered; });
        show(session);
        if (action == 1)
            qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", "before-live-commit");
        else if (action == 2)
            qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", "live-committed");
        else if (action == 3) {
            check(QDir().mkpath(Audit::auditDirectory()) && write(Audit::auditLogPath(), "invalid historical audit"),
                "retained corrupt audit fixture created");
        }
        if (action == 0)
            session.prompt()->findChild<QPushButton*>(QStringLiteral("recoveryPromptRetain"))->click();
        else
            session.prompt()->reject();
        check(settled == 1 && !session.work() && f.retained(), "ordinary retain settles synchronously without waiting for optional audit");
        queueBarrier();
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT");
        check(delivered == 0 && settled == 1 && f.retained()
            && (action == 0 || action == 2 ? f.retainAudited()
                : read(Audit::auditLogPath()) == (action == 3 ? QByteArray("invalid historical audit") : QByteArray())),
            "retention audit succeeds/fails once without a recovery result, retry, popup or site change");
        const auto auditBeforeQuit = read(Audit::auditLogPath());
        session.beginQuit();
        queueBarrier();
        check(settled == 1 && delivered == 0 && f.retained() && read(Audit::auditLogPath()) == auditBeforeQuit,
            "post-retain quit never emits another decision or restarts audit");
    }
    for (int action = 0; action < 5; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "optional retain audit queued behind existing work");
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(*session);
        if (action == 0) {
            session->beginQuit(); // Must never start an audit merely to log exit.
        } else {
            session->prompt()->reject();
            if (action == 1)
                session->beginQuit();
            else if (action == 2)
                session->invalidate();
            else if (action == 3)
                session.reset();
            else {
                session->beginQuit();
                session->show(); // No new prompt or audit after exit.
            }
        }
        bool mainRan = false;
        WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { mainRan = true; });
        check(pump([&] { return mainRan; }) && f.noAudit() && f.retained(), "quit/retain never waits for queued logger or blocks main-thread continuation");
        release.release();
        queueBarrier();
        check(f.noAudit() && f.retained(), "cancelled or destroyed optional audit is dropped before I/O");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "retain cleanup receiver queue gate reached");
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        QObject::connect(session.get(), &Session::settled, qApp, [&] { session.reset(); });
        show(*session);
        session->prompt()->reject();
        check(!session && f.noAudit() && f.retained(), "synchronous settled receiver may destroy session without waiting for logging");
        release.release();
        queueBarrier();
        check(f.noAudit() && f.retained(), "cleanup receiver destruction drops queued retain audit safely");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        const QString ready = QDir(f.root).filePath(QStringLiteral("audit-ready"));
        const QString resume = QDir(f.root).filePath(QStringLiteral("audit-resume"));
        qputenv("WIZ_RECOVERY_TEST_AUDIT_CHECKPOINT", "before-live-commit");
        qputenv("WIZ_RECOVERY_TEST_AUDIT_READY", ready.toUtf8());
        qputenv("WIZ_RECOVERY_TEST_AUDIT_RESUME", resume.toUtf8());
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(*session);
        session->prompt()->reject();
        check(pump([&] { return QFileInfo::exists(ready); }), "optional logger actually running before quit");
        session->beginQuit();
        session.reset();
        bool mainRan = false;
        WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { mainRan = true; });
        check(pump([&] { return mainRan; }) && !QFileInfo::exists(Audit::auditLogPath()),
            "quit/session teardown returns while started audit is deliberately held without a GUI receipt");
        check(write(resume, "RESUME"), "running optional audit released by isolated fixture");
        queueBarrier();
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_CHECKPOINT");
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_READY");
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_RESUME");
        check(QFile::remove(ready) && QFile::remove(resume) && f.retainAudited() && f.retained(),
            "started optional audit drains once without owner, callback or recovery mutation");
    }
}
static void ordinaryAndRefused(const QString& sandbox)
{
    for (bool externalEdit : {false, true}) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        std::optional<Result> received;
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        QObject::connect(session.get(), &Session::resultReady, qApp, [&](const Result& result) {
            received = result;
            session.reset(); // Actual result receiver may synchronously delete the session.
        });
        show(*session);
        if (externalEdit) {
            check(write(f.target, "EXTERNAL MARKDOWN\n"), "synthetic external edit after inspection");
            f.before = snapshot(f.root);
        }
        const auto work = click(*session);
        check(pump([&] { return received.has_value(); }), "actual coordinator returns GUI result");
        check(!session && complete(work) && received && work.completedResult(), "result signal may destroy session without losing work result");
        const auto expected = externalEdit ? RecoveryOutcome::Refused : RecoveryOutcome::RolledBack;
        check(received && received->recovery == expected && received->audit == AuditOutcome::Recorded,
            "fresh backend accepts rollback or conservatively refuses changed evidence");
        check(externalEdit ? f.retained() : (f.oldPair() && !QFileInfo::exists(f.recovery)), "actual old pair restored or external state preserved");
        check(f.audited(externalEdit ? QStringLiteral("refused") : QStringLiteral("rolled-back")), "actual audit contains exact private intent/result pair");
        queueBarrier();
    }
}
struct Hold {
    QString ready, resume;
    Hold(const QString& root, const char* point)
    {
        ready = QDir(root).filePath(QStringLiteral("ready"));
        resume = QDir(root).filePath(QStringLiteral("resume"));
        qputenv("WIZ_RECOVERY_TEST_CONFIRM_CHECKPOINT", point);
        qputenv("WIZ_RECOVERY_TEST_CONFIRM_READY", ready.toUtf8());
        qputenv("WIZ_RECOVERY_TEST_CONFIRM_RESUME", resume.toUtf8());
    }
    void release() const { check(write(resume, "RESUME"), "synthetic backend hold released"); }
    ~Hold()
    {
        qunsetenv("WIZ_RECOVERY_TEST_CONFIRM_CHECKPOINT");
        qunsetenv("WIZ_RECOVERY_TEST_CONFIRM_READY");
        qunsetenv("WIZ_RECOVERY_TEST_CONFIRM_RESUME");
    }
};
static void runningLifetimes(const QString& sandbox)
{
    for (int action = 0; action < 3; ++action) {
        Fixture f(sandbox);
        // Keep control files outside the export snapshot.
        Hold hold(sandbox, "intent-recorded");
        QFile::remove(hold.ready);
        QFile::remove(hold.resume);
        auto owner = std::make_unique<QWidget>();
        WizDocumentCallbackEpoch document;
        auto session = std::make_unique<Session>(owner.get(), document.ticket(), f.target, f.inspection, LockState::Available);
        int delivered = 0, settled = 0;
        QObject::connect(session.get(), &Session::resultReady, qApp, [&](const Result&) { ++delivered; });
        QObject::connect(session.get(), &Session::settled, qApp, [&] { ++settled; });
        show(*session);
        const auto work = click(*session);
        check(pump([&] { return QFileInfo::exists(hold.ready); }) && work.stage() == Stage::Running, "actual coordinator held after durable intent");
        if (action == 0)
            session->beginQuit();
        else if (action == 1) {
            session->invalidate();
            document.invalidate();
            owner.reset();
        } else
            session.reset();
        bool mainQueueRan = false;
        WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { mainQueueRan = true; });
        check(pump([&] { return mainQueueRan; }) && work.stage() == Stage::Running,
            "invalidation returns and existing main queue runs while backend held");
        hold.release();
        check(pump([&] { return complete(work); }), "started actual coordinator finishes after UI context closes");
        queueBarrier();
        const auto result = work.completedResult();
        check(result && result->recovery == RecoveryOutcome::RolledBack && result->audit == AuditOutcome::Recorded
            && result->lockRelease == LockReleaseOutcome::Released && delivered == 0,
            "real independent result survives quit/navigation/session deletion with no stale GUI signal");
        check(f.oldPair() && !QFileInfo::exists(f.recovery) && f.audited(QStringLiteral("rolled-back")),
            "started rollback completes exact files and actual durable audit");
        check(settled == (action == 2 ? 0 : 1), "application cleanup notified despite stale UI while session lives");
    }
}
static void finalAuditBusy(const QString& sandbox)
{
    Fixture f(sandbox);
    Hold hold(sandbox, "recovery-complete");
    QFile::remove(hold.ready);
    QFile::remove(hold.resume);
    QWidget owner;
    WizDocumentCallbackEpoch document;
    Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
    std::optional<Result> received;
    QObject::connect(&session, &Session::resultReady, qApp, [&](const Result& result) { received = result; });
    show(session);
    const auto work = click(session);
    check(pump([&] { return QFileInfo::exists(hold.ready); }) && f.oldPair(), "actual files restored before final audit contention");
    QLockFile auditWriter(Audit::auditLockPath());
    auditWriter.setStaleLockTime(0);
    check(auditWriter.tryLock(0), "synthetic competing audit writer owns final-log lock");
    hold.release();
    check(pump([&] { return received.has_value(); }), "audit failure reaches current GUI once");
    check(received && received->recovery == RecoveryOutcome::RolledBack && received->audit == AuditOutcome::Failed
        && received->lockRelease == LockReleaseOutcome::Released && complete(work), "actual audit busy never relabels completed rollback");
    check(f.oldPair() && !QFileInfo::exists(f.recovery) && read(Audit::auditLogPath()).trimmed().split('\n').size() == 1,
        "no rollback retry and no false final event when audit busy");
    auditWriter.unlock();
    queueBarrier();
}
struct DiscoverySeen {
    int discovered = 0, settled = 0;
    Availability availability = Availability::Refused;
    LockState lock = LockState::Refused;
};
static void trackDiscovery(Session& session, DiscoverySeen& seen)
{
    QObject::connect(&session, &Session::discovered, qApp, [&seen](const Inspection& inspection, const LockInspection& lock) {
        ++seen.discovered;
        seen.availability = inspection.availability;
        seen.lock = lock.state;
    });
    QObject::connect(&session, &Session::settled, qApp, [&seen] { ++seen.settled; });
}
static void asynchronousDiscovery(const QString& sandbox)
{
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        Session session(&owner, document.ticket(), f.target);
        trackDiscovery(session, seen);
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "read-only discovery queued behind actual pool task");
        session.show();
        session.show();
        bool mainRan = false;
        WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { mainRan = true; });
        check(pump([&] { return mainRan; }) && !session.prompt() && seen.discovered == 0,
            "show returns without GUI filesystem discovery and main queue remains responsive");
        release.release();
        check(pump([&] { return session.prompt() != nullptr; }) && seen.discovered == 1
            && seen.availability == Availability::AwaitingDecision && seen.lock == LockState::Available,
            "one actual asynchronous snapshot creates one pending prompt");
        session.show();
        queueBarrier();
        check(seen.discovered == 1 && seen.settled == 0 && !session.work() && f.retained() && f.noAudit(),
            "repeat show neither scans again nor authorizes recovery or audit");
        session.beginQuit();
        check(seen.settled == 1 && !session.work() && f.retained() && f.noAudit(), "discovered prompt retains without confirmation");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        // A different valid recovery site already exists in this same parent.
        const QString other = QDir(f.root).filePath(QStringLiteral("other.md"));
        Session session(&owner, document.ticket(), other);
        trackDiscovery(session, seen);
        session.show();
        check(pump([&] { return seen.settled == 1; }) && seen.discovered == 1
            && seen.availability == Availability::None && seen.lock == LockState::Available,
            "same-target discovery ignores another destination recovery site");
        check(!session.prompt() && !session.work() && f.retained() && f.noAudit(),
            "no-site advisory creates no prompt, export, lock or audit");
    }
    for (bool activeLock : {false, true}) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        std::unique_ptr<QLockFile> lock;
        if (activeLock) {
            lock = std::make_unique<QLockFile>(destinationLockPath(f.target));
            lock->setStaleLockTime(0);
            check(lock->tryLock(0), "actual active destination lock for read-only discovery");
            qint64 pid = 0;
            QString host, writer;
            check(lock->getLockInfo(&pid, &host, &writer) && writer == QCoreApplication::applicationName(),
                "synthetic Qt lock writer identity matches product protocol identity");
        } else {
            check(write(QDir(f.recovery).filePath(QStringLiteral("transaction.json")), "incomplete synthetic manifest"),
                "ordinary incomplete recovery metadata fixture created");
        }
        f.before = snapshot(f.root);
        Session session(&owner, document.ticket(), f.target);
        trackDiscovery(session, seen);
        session.show();
        const bool shown = pump([&] { return session.prompt() != nullptr; });
        if (seen.availability != (activeLock ? Availability::Busy : Availability::Refused)) {
            qint64 pid = 0;
            QString host, writer;
            if (lock)
                lock->getLockInfo(&pid, &host, &writer);
            QTextStream(stderr) << "DISCOVERY_LOCK_CONTROL active=" << activeLock
                << " availability=" << int(seen.availability) << " lock=" << int(seen.lock)
                << " writer=" << writer << " expected=" << QCoreApplication::applicationName() << Qt::endl;
        }
        check(shown && seen.discovered == 1
            && seen.availability == (activeLock ? Availability::Busy : Availability::Refused),
            "actual busy/refused discovery produces conservative information prompt");
        check(!confirm(session)->isEnabled() && f.retained() && f.noAudit(),
            "read-only discovery does not acquire/remove lock or repair incomplete metadata");
        session.beginQuit();
        check(seen.settled == 1 && f.retained() && f.noAudit(), "busy/refused view closes retaining exact fixture");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        Session session(&owner, document.ticket(), f.target);
        trackDiscovery(session, seen);
        session.beginQuit();
        session.show();
        queueBarrier();
        check(seen.settled == 1 && seen.discovered == 0 && !session.prompt() && f.retained() && f.noAudit(),
            "quit before first show prevents discovery and stale popup");
    }
    for (int action = 0; action != 3; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target);
        trackDiscovery(*session, seen);
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "discovery cancellation queue gate reached");
        session->show();
        if (action == 0)
            session->beginQuit();
        else if (action == 1) {
            session->invalidate();
            document.invalidate();
        } else
            session.reset();
        release.release();
        queueBarrier();
        check(seen.discovered == 0 && seen.settled == (action == 2 ? 0 : 1)
            && owner.findChildren<WizMarkdownExportRecoveryPrompt*>().isEmpty() && f.retained() && f.noAudit(),
            "queued discovery invalidation/destruction cannot create a late prompt");
    }
    for (int action = 0; action != 3; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target);
        trackDiscovery(*session, seen);
        session->show(); // Do not pump the GUI receipt yet.
        QSemaphore readFinished;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { readFinished.release(); });
        check(readFinished.tryAcquire(1, 4000) && seen.discovered == 0, "actual read completes before deliberately deferred GUI receipt");
        if (action == 0)
            session->beginQuit();
        else if (action == 1)
            document.invalidate(); // Also protect against a missed early host notification.
        else
            session.reset();
        queueBarrier();
        check(seen.discovered == 0 && seen.settled == (action == 2 ? 0 : 1)
            && owner.findChildren<WizMarkdownExportRecoveryPrompt*>().isEmpty() && f.retained() && f.noAudit(),
            "completed read with stale GUI context is never presented");
    }
    for (int action = 0; action != 3; ++action) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        auto session = std::make_unique<Session>(&owner, document.ticket(), f.target);
        trackDiscovery(*session, seen);
        QObject::connect(session.get(), &Session::discovered, qApp, [&](const Inspection&, const LockInspection&) {
            if (action == 0)
                session->beginQuit();
            else if (action == 1)
                document.invalidate();
            else
                session.reset();
        });
        session->show();
        check(pump([&] { return seen.discovered == 1; }), "actual discovered receiver invoked");
        queueBarrier();
        check(seen.settled == (action == 2 ? 0 : 1)
            && owner.findChildren<WizMarkdownExportRecoveryPrompt*>().isEmpty() && f.retained() && f.noAudit(),
            "receiver exit/navigation/session destruction prevents prompt creation after signal");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        DiscoverySeen seen;
        Session session(&owner, document.ticket(), f.target);
        trackDiscovery(session, seen);
        std::optional<Result> result;
        QObject::connect(&session, &Session::resultReady, qApp, [&](const Result& value) { result = value; });
        session.show();
        check(pump([&] { return session.prompt() != nullptr; }) && !session.work() && f.retained() && f.noAudit(),
            "asynchronous discovery alone cannot authorize or start recovery");
        const auto work = click(session);
        check(pump([&] { return result.has_value(); }) && result->recovery == RecoveryOutcome::RolledBack
            && result->audit == AuditOutcome::Recorded && complete(work), "explicit confirmation after discovery uses actual strict backend");
        check(f.oldPair() && !QFileInfo::exists(f.recovery) && f.audited(QStringLiteral("rolled-back"))
            && seen.discovered == 1 && seen.settled == 1, "discovery-to-confirmation round trip restores exact old pair and audit");
        queueBarrier();
    }
}
static void discoveryClosedQueue(const QString& sandbox)
{
    Fixture f(sandbox);
    QWidget owner;
    WizDocumentCallbackEpoch document;
    DiscoverySeen seen;
    Session session(&owner, document.ticket(), f.target);
    trackDiscovery(session, seen);
    session.show();
    check(seen.settled == 1 && seen.discovered == 0 && !session.prompt()
        && f.retained() && f.noAudit(), "closed queue discards discovery without dangling state or popup");
}
static void freshDestinationGate(const QString& sandbox)
{
    Fixture f(sandbox);
    int calls = 0;
    QString error;
    check(!withFreshDestination(f.target, [&] { ++calls; return true; }, error)
        && calls == 0 && !error.isEmpty() && f.retained() && f.noAudit(),
        "fresh export reservation cannot overwrite any existing recovery site");
    const QString target = QDir(f.root).filePath(QStringLiteral("fresh.md"));
    check(withFreshDestination(target, [&] {
        ++calls;
        check(inspectDestinationLock(target).state == LockState::Active,
            "actual ordinary writer runs with conservative target lock held");
        QString competingError;
        check(!withFreshDestination(target, [&] { calls += 100; return true; }, competingError),
            "competing ordinary export cannot enter the held reservation");
        return write(target, "FRESH CONTENT");
    }, error) && calls == 1 && error.isEmpty() && read(target) == "FRESH CONTENT"
        && !QFileInfo::exists(destinationLockPath(target)), "fresh ordinary export runs once and releases reservation");
    check(!withFreshDestination(target, {}, error) && read(target) == "FRESH CONTENT"
        && !QFileInfo::exists(destinationLockPath(target)), "missing ordinary writer refuses without lock or destination changes");
    check(!withFreshDestination(target, [&] { error = QStringLiteral("ordinary write failed"); return false; }, error)
        && error == QStringLiteral("ordinary write failed") && read(target) == "FRESH CONTENT"
        && !QFileInfo::exists(destinationLockPath(target)), "ordinary writer failure is preserved and releases target lock");
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const bool released = withFreshDestination(target, [&] { return write(target, "COMPLETED WRITE"); }, error);
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    check(!released && !error.isEmpty() && read(target) == "COMPLETED WRITE",
        "uncertain ordinary-export unlock never claims completed content was rolled back");
    check(inspect(target).availability == Availability::None, "no-site advisory obtained before deliberately new recovery evidence");
    check(QDir().mkpath(recoveryDirectory(target))
        && write(QDir(recoveryDirectory(target)).filePath(QStringLiteral("unknown")), "PRESERVE"),
        "synthetic recovery site appears after advisory but before write");
    const auto before = snapshot(f.root);
    check(!withFreshDestination(target, [&] { calls += 100; return true; }, error)
        && calls == 1 && snapshot(f.root) == before && f.noAudit(),
        "fresh locked validation refuses raced recovery evidence without changing any files");
}
static void applicationHooks(const QString& sandbox)
{
    namespace App = WizMarkdownExportRecoveryApp;
    {
        Fixture f(sandbox);
        auto owner = std::make_unique<QWidget>();
        WizDocumentCallbackEpoch document;
        int continued = 0, results = 0;
        QPointer<Session> session = App::open(owner.get(), document.ticket(),
            QDir(f.root).filePath(QStringLiteral("fresh.md")),
            [&] { ++continued; }, [&](const Result&) { ++results; });
        session->show();
        owner.reset();
        queueBarrier();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(!session && continued == 0 && results == 0 && f.retained() && f.noAudit(),
            "App continuations never run after original owner destruction");
    }
    for (bool stale : {false, true}) {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        int continued = 0, results = 0;
        const QString target = QDir(f.root).filePath(QStringLiteral("fresh.md"));
        QPointer<Session> session = App::open(&owner, document.ticket(), target,
            [&] { ++continued; }, [&](const Result&) { ++results; });
        check(session && !session->prompt() && !App::isQuitting(), "experimental App factory accepts only explicit current target without constructor I/O");
        if (stale)
            document.invalidate();
        session->show();
        queueBarrier();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(!session && continued == (stale ? 0 : 1) && results == 0 && f.retained() && f.noAudit(),
            "only current no-site advisory continues; cleanup itself never authorizes export");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        int continued = 0, results = 0;
        QPointer<Session> session = App::open(&owner, document.ticket(), f.target,
            [&] { ++continued; }, [&](const Result& result) {
                check(result.recovery == RecoveryOutcome::RolledBack, "App result carries actual completed recovery");
                ++results;
            });
        session->show();
        check(pump([&] { return session && session->prompt(); }), "actual App facade shows same-target recovery prompt");
        click(*session);
        check(pump([&] { return results == 1; }) && continued == 0 && f.oldPair() && f.audited(QStringLiteral("rolled-back")),
            "App confirmation recovers once but never automatically retries ordinary export");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(!session, "App owns settled-session cleanup independent of original view");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        int continued = 0;
        QPointer<Session> session = App::open(&owner, document.ticket(), f.target, [&] { ++continued; }, {});
        session->show();
        check(pump([&] { return session && session->prompt(); }), "pending App prompt ready before quit scope");
        {
            App::QuitScope quit;
            check(App::isQuitting() && !session->work()
                && !App::open(&owner, document.ticket(), f.target, {}, {}), "quit scope immediately retains prompts and rejects new App requests");
            {
                App::QuitScope nested;
                check(App::isQuitting(), "nested quit scope remains blocked");
            }
            check(App::isQuitting(), "inner cancelled quit cannot reopen exports during outer save preparation");
            bool savedLocally = false;
            QTimer::singleShot(0, qApp, [&] { savedLocally = true; });
            check(pump([&] { return savedLocally; }) && continued == 0 && f.retained() && f.noAudit(),
                "synthetic local-save receipt runs during quit without recovery audit or export");
        }
        check(!App::isQuitting() && continued == 0 && f.retained() && f.noAudit(),
            "failed or cancelled quit permits future explicit work but never revives old prompt");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(!session && App::open(nullptr, document.ticket(), f.target, {}, {}) == nullptr,
            "settled App session is deleted; invalid factory owner remains refused");
        int freshContinuation = 0;
        QPointer<Session> fresh = App::open(&owner, document.ticket(),
            QDir(f.root).filePath(QStringLiteral("fresh-after-cancel.md")), [&] { ++freshContinuation; }, {});
        fresh->show();
        queueBarrier();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(!fresh && freshContinuation == 1 && continued == 0 && f.retained() && f.noAudit(),
            "cancelled quit permits a new explicit request without reviving the old export");
    }
}
static void applicationQuitCommit(const QString& sandbox)
{
    namespace App = WizMarkdownExportRecoveryApp;
    Fixture f(sandbox);
    QWidget owner;
    WizDocumentCallbackEpoch document;
    Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
    show(session);
    {
        App::QuitScope quit;
        quit.commit();
        check(App::isQuitting() && !session.work() && f.retained() && f.noAudit(),
            "committed quit retains every App-owned pending session without I/O");
    }
    check(App::isQuitting() && !App::open(&owner, document.ticket(), f.target, {}, {})
        && f.retained() && f.noAudit(), "committed quit remains closed after scope destruction");
}
static void queueShutdown(const QString& sandbox)
{
    {
        Fixture f(sandbox);
        Hold hold(sandbox, "intent-recorded");
        QFile::remove(hold.ready);
        QFile::remove(hold.resume);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        int delivered = 0;
        QObject::connect(&session, &Session::resultReady, qApp, [&](const Result&) { ++delivered; });
        show(session);
        const auto work = click(session);
        check(pump([&] { return QFileInfo::exists(hold.ready); }), "actual started coordinator ready before queue cleanup");
        session.beginQuit();
        hold.release();
        WizQueuedThreadsShutdown();
        QApplication::processEvents();
        const auto result = work.completedResult();
        check(result && result->recovery == RecoveryOutcome::RolledBack && result->audit == AuditOutcome::Recorded
            && delivered == 0, "existing queue cleanup joins actual backend without needing GUI receipt");
        check(f.oldPair() && !QFileInfo::exists(f.recovery) && f.audited(QStringLiteral("rolled-back")),
            "queue-cleanup result and files survive discarded main-queue callback");
        WizQueuedThreadsInit(); // Existing supported isolated reinitialization.
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "retain audit shutdown blocker started");
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(session);
        session.prompt()->reject();
        std::unique_ptr<QThread> releaser(QThread::create([&] { QThread::msleep(30); release.release(); }));
        releaser->start();
        WizQueuedThreadsShutdown();
        check(releaser->wait(4000) && f.noAudit() && f.retained(),
            "actual queue shutdown discards pending optional logger without I/O or recovery changes");
        WizQueuedThreadsInit();
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        QSemaphore held, release;
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [&] { held.release(); release.tryAcquire(1, 8000); });
        check(held.tryAcquire(1, 4000), "queue discard blocker started");
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(session);
        const auto work = click(session);
        std::unique_ptr<QThread> releaser(QThread::create([&] { QThread::msleep(30); release.release(); }));
        releaser->start();
        // Existing application cleanup joins its pools; beginQuit itself never does.
        WizQueuedThreadsShutdown();
        check(releaser->wait(4000), "synthetic queue releaser joined normally");
        check(work.stage() == Stage::Retained && f.retained() && f.noAudit(), "actual queue shutdown destroys queued action without backend");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        show(session);
        const auto work = click(session);
        check(work && work.stage() == Stage::Retained && f.retained() && f.noAudit(), "closed application queue rejects action and releases claim");
    }
    {
        Fixture f(sandbox);
        QWidget owner;
        WizDocumentCallbackEpoch document;
        Session session(&owner, document.ticket(), f.target, f.inspection, LockState::Available);
        int settled = 0;
        QObject::connect(&session, &Session::settled, qApp, [&] { ++settled; });
        show(session);
        session.prompt()->reject();
        check(settled == 1 && !session.work() && f.noAudit() && f.retained(),
            "closed queue drops ordinary retain audit while application cleanup still settles immediately");
    }
}
#ifdef WIZ_VERIFY_QUIT
#include "recovery-quit-cases.h"
#endif

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("WizRecoverySessionSynthetic"));
    QCoreApplication::setApplicationName(QStringLiteral("WizRecoverySessionOffscreen"));
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    // Qt's stock lock records the executable name on this platform. Keep this
    // synthetic process identity consistent; never weaken product lock checks.
    QCoreApplication::setApplicationName(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    application.setQuitOnLastWindowClosed(false);
    check(QGuiApplication::platformName() == QStringLiteral("offscreen"), "offscreen platform enforced");
    QTemporaryDir sandbox;
    check(sandbox.isValid(), "isolated synthetic sandbox valid");
#ifdef WIZ_VERIFY_QUIT
    return QuitCases::entry(application, sandbox.path());
#else
    {
        WizQueuedThreadsSession queues([] {});
        pendingAndQueued(sandbox.path());
        ordinaryAndRefused(sandbox.path());
        runningLifetimes(sandbox.path());
        finalAuditBusy(sandbox.path());
        asynchronousDiscovery(sandbox.path());
        retainAuditPrimitives(sandbox.path());
        bestEffortRetain(sandbox.path());
        freshDestinationGate(sandbox.path());
        applicationHooks(sandbox.path());
        queueShutdown(sandbox.path());
        discoveryClosedQueue(sandbox.path());
        applicationQuitCommit(sandbox.path());
    }
    qunsetenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTextStream(stdout) << "RECOVERY_SESSION_CHECKS=" << checks << '\n'
        << "RECOVERY_SESSION_FIXTURES=" << caseNumber << '\n'
        << "RECOVERY_SESSION_FAILURES=" << failures << Qt::endl;
    return failures ? 1 : 0;
#endif
}
