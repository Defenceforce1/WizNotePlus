#include "widgets/WizBiometricUnlockWidget.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QRunnable>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <stdexcept>

namespace {
using Widget = WizBiometricUnlockWidget;
using namespace WizBiometricKeychain;
bool until(const std::function<bool()>& condition, int ms = 3000)
{
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < ms) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
void drain() { until([] { return false; }, 50); }
QPushButton* button(Widget& widget, const char* name) { return widget.findChild<QPushButton*>(name); }
void click(Widget& widget) { button(widget, "biometricUnlockStart")->click(); }
struct Probe {
    std::atomic<int> made{0}, reads{0}, cancels{0};
    std::atomic<bool> entered{false}, offGui{false}, timedOut{false};
    QSemaphore resume;
    bool hold = false, throws = false;
    Result result;
};
class FakeRead final : public Widget::Operation {
    std::shared_ptr<Probe> probe;
    QThread* gui;
public:
    explicit FakeRead(std::shared_ptr<Probe> probe) : probe(std::move(probe)), gui(QThread::currentThread()) {}
    Result read(const Scope&) override {
        probe->reads.fetch_add(1);
        probe->offGui.store(QThread::currentThread() != gui);
        probe->entered.store(true);
        // Deliberately ignore cancel to exercise a late successful completion.
        if (probe->hold && !probe->resume.tryAcquire(1, 3000)) {
            probe->timedOut.store(true);
            return {Error::Failed, {}};
        }
        if (probe->throws) throw std::runtime_error("synthetic provider failure");
        return probe->result;
    }
    void cancel() noexcept override { probe->cancels.fetch_add(1); }
};
Widget::Services services(const std::shared_ptr<Probe>& probe, QThreadPool& pool)
{
    return {[probe] { probe->made.fetch_add(1); return std::make_shared<FakeRead>(probe); }, &pool};
}
void show(Widget& widget, const Widget::Context& context, bool enabled = true)
{
    widget.resize(560, 170); widget.setContext(context, enabled); widget.show(); drain();
}
}

int wizBiometricUnlockUiSelfTest(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    const QString password = "Synthetic-unlock-UI-only-2026";
    const auto generated = WizCertificateCreation::generate(password, password, {});
    if (!check(bool(generated), "temporary synthetic certificate generates without an account or credential access")) return 1;
    Widget::Context context{{"https://synthetic.invalid/", "synthetic-account", {}, generated.certificate}, "synthetic-note", 1};
    const auto fresh = [&] { auto p = std::make_shared<Probe>(); p->result = {Error::None, password}; return p; };
    {
        QThreadPool pool;const auto p=fresh();bool available=false;
        auto supplied=services(p,pool);supplied.biometricAvailable=[&]{return available;};
        Widget widget(supplied);widget.setAutomaticStart(true);widget.setContext(context,true);widget.show();QApplication::setActiveWindow(&widget);drain();
        check(p->made==0&&widget.stage()==Widget::Stage::Unavailable,"unavailable Touch ID stays on password without constructing a read operation");
        click(widget);drain();check(p->made==0,"unavailable Touch ID button cannot start authentication");
        available=true;widget.setContext(context,true);available=false;drain();
        check(p->made==0&&widget.stage()==Widget::Stage::Unavailable,"availability lost before the queued attempt falls directly back to password");
        available=true;QEvent activate(QEvent::WindowActivate);QApplication::sendEvent(&widget,&activate);
        check(until([&]{return p->reads==1&&widget.stage()==Widget::Stage::Verified;}),"current available remembered context still performs only one automatic verification");
    }
    {
        QThreadPool pool;const auto p=fresh();bool available=false;
        auto supplied=services(p,pool);supplied.biometricAvailable=[&]{return available;};
        QWidget host;Widget widget(supplied,&host);widget.setAutomaticStart(true);
        QObject::connect(&widget,&Widget::stageChanged,&host,[&]{widget.setPresentationVisible(widget.stage()!=Widget::Stage::Unavailable);});
        host.show();QApplication::setActiveWindow(&host);widget.setContext(context,true);drain();
        check(!widget.isVisible()&&p->made==0,"unavailable hidden choice preserves password-only presentation without a credential read");
        available=true;QEvent activate(QEvent::WindowActivate);QApplication::sendEvent(&widget,&activate);
        check(until([&]{return widget.isVisible()&&p->reads==1&&widget.stage()==Widget::Stage::Verified;}),"restored capability on activation reveals the hidden choice and starts its first verification once");
        widget.cancel();QApplication::sendEvent(&widget,&activate);drain();
        check(p->reads==1&&button(widget,"biometricUnlockStart")->isEnabled(),"cancel leaves a manual Touch ID retry without another automatic prompt");
        available=false;widget.setContext(context,true);widget.cancelAutomaticStart();available=true;QApplication::sendEvent(&widget,&activate);drain();
        check(widget.isVisible()&&p->reads==1,"manual password input prevents automatic verification after capability restoration");
        available=false;widget.setContext(context,true);widget.cancel();available=true;QApplication::sendEvent(&widget,&activate);drain();
        check(p->reads==1,"retired hidden context cannot automatically restart after capability restoration");
    }
    {
        QThreadPool pool;const auto p=fresh();p->result={Error::Canceled,{}};
        Widget widget(services(p,pool));widget.setAutomaticStart(true);widget.setContext(context,true);widget.show();
        // The offscreen platform has no window manager to reactivate a reused
        // top-level window. Drive Qt's active-window state, never the desktop.
        QApplication::setActiveWindow(&widget);
        check(until([&]{return p->reads==1&&widget.stage()==Widget::Stage::Ready;}),"opted-in active locked window starts verification without a second button click");
        drain();check(p->reads==1,"canceled automatic verification does not repeatedly prompt");
        click(widget);check(until([&]{return p->reads==2&&widget.stage()==Widget::Stage::Ready;}),"explicit retry remains available after canceling the automatic prompt");
        widget.setContext(context,true);widget.cancel();drain();check(p->reads==2,"manual password or cancel retires an already queued automatic request");
        widget.setContext(context,false);drain();check(p->reads==2,"non-opted-in context never automatically authenticates");
        widget.hide();widget.setContext(context,true);drain();check(p->reads==2,"hidden locked window defers automatic verification");
        widget.show();QApplication::setActiveWindow(&widget);
        check(until([&]{return p->reads==3&&widget.stage()==Widget::Stage::Ready;}),"a fresh visible locked presentation can request one automatic verification");
        widget.beginShutdown();drain();check(p->reads==3,"shutdown cannot restart an automatic verification");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool));
        widget.show(); drain();
        check(widget.stage() == Widget::Stage::Unavailable && !button(widget,"biometricUnlockStart")->isEnabled(),
              "missing context offers only manual password without automatic authentication");
        show(widget, context, false); click(widget); drain();
        check(p->made == 0 && !widget.authorizedFor(context), "remembered access defaults off even with a valid certificate");
        for (int type = 0; type < 5; ++type) {
            auto bad = context;
            if (type == 0) bad.openSequence = 0;
            if (type == 1) bad.documentGuid.clear();
            if (type == 2) bad.documentGuid += QChar::Null;
            if (type == 3) bad.documentGuid = QString(257, 'x');
            if (type == 4) bad.scope.certificate = {};
            widget.setContext(bad, true); click(widget);
            check(p->made == 0 && widget.stage() == Widget::Stage::Unavailable,
                  "invalid note or certificate context refuses before constructing a native operation");
        }
    }
    {
        QThreadPool pool; const auto p = fresh(); p->hold = true;
        Widget widget(services(p, pool)); int ready = 0;
        QObject::connect(&widget, &Widget::credentialReady, &widget, [&] { ++ready; });
        show(widget, context);
        check(p->made == 0, "showing a valid enabled prompt never starts authentication by itself");
        click(widget); click(widget);
        check(widget.stage() == Widget::Stage::Working && p->made == 1,
              "explicit click starts exactly one operation and duplicate clicks cannot overlap");
        check(until([&] { return p->entered.load(); }) && p->offGui,
              "read and local certificate verification execute away from the GUI thread");
        bool heartbeat = false; QTimer::singleShot(5, &widget, [&] { heartbeat = true; });
        check(until([&] { return heartbeat; }) && widget.stage() == Widget::Stage::Working,
              "GUI keeps processing events while the simulated authentication is pending");
        p->resume.release();
        check(until([&] { return ready == 1; }) && widget.stage() == Widget::Stage::Verified,
              "locally verified exact credential produces one ready notification");
        check(!widget.authorizedFor(context), "verified but unconsumed result is not an open-note authorization");
        auto wrong = context; ++wrong.openSequence;
        check(!widget.takeCredential(wrong) && !widget.authorizedFor(wrong),
              "a different open request cannot consume the credential");
        const auto credential = widget.takeCredential(context);
        check(credential && credential.password == password && widget.authorizedFor(context),
              "only the current note opening consumes the exact synthetic password once");
        check(!widget.takeCredential(context) && widget.authorizedFor(context),
              "same already-open note retains only authorization and cannot retrieve the password twice");
        auto other = context; other.documentGuid += "-other";
        check(!widget.authorizedFor(other), "old authorization cannot authorize another note despite a shared certificate cache");
        widget.setContext(context, true);
        check(!widget.authorizedFor(context) && widget.stage() == Widget::Stage::Ready,
              "even an identical context reset is a new authorization boundary rather than reuse of old cached access");
        widget.hide(); widget.show();
        check(!widget.authorizedFor(context) && widget.stage() == Widget::Stage::Ready,
              "hiding clears authorization and showing again requires another explicit verification");
        check(!p->timedOut, "success fixture releases its worker without timeout");
    }
    for (const auto error : {Error::SigningRequired, Error::BiometryUnavailable, Error::Missing,
            Error::AccessDenied, Error::Canceled, Error::Failed, Error::CredentialMismatch}) {
        QThreadPool pool; const auto p = fresh(); p->result.error = error;
        Widget widget(services(p, pool)); int ready = 0;
        QObject::connect(&widget, &Widget::credentialReady, &widget, [&] { ++ready; });
        show(widget, context); click(widget);
        const auto expected=error==Error::BiometryUnavailable?Widget::Stage::Unavailable:Widget::Stage::Ready;
        check(until([&] { return widget.stage() == expected; }) && ready == 0,
              "failed canceled missing or unavailable read never produces a ready credential");
        check(!widget.takeCredential(context) && !widget.authorizedFor(context)
              && button(widget,"biometricUnlockManual")->isEnabled()
              && !widget.findChild<QLabel*>("biometricUnlockStatus")->text().contains(password),
              "failure leaves manual password usable and exposes no password through UI or cached authorization");
    }
    for (int type = 0; type < 3; ++type) {
        QThreadPool pool; const auto p = fresh();
        if (type == 0) p->result.password = "wrong-synthetic-password";
        if (type == 1) p->result.password.clear();
        if (type == 2) p->throws = true;
        Widget widget(services(p, pool)); int ready = 0;
        QObject::connect(&widget, &Widget::credentialReady, &widget, [&] { ++ready; });
        show(widget, context); click(widget);
        check(until([&] { return widget.stage() == Widget::Stage::Ready; })
              && ready == 0 && !widget.takeCredential(context),
              "malformed success wrong password or provider exception fails local verification closed");
    }
    for (int field = 0; field < 7; ++field) {
        QThreadPool pool; const auto p = fresh(); p->hold = true;
        Widget widget(services(p, pool)); int ready = 0;
        QObject::connect(&widget, &Widget::credentialReady, &widget, [&] { ++ready; });
        show(widget, context); click(widget);
        check(until([&] { return p->entered.load(); }), "old-context synthetic worker reaches its bounded pending state");
        auto changed = context;
        if (field == 0) changed.scope.serverIdentity += "other/";
        if (field == 1) changed.scope.accountGuid += "-other";
        if (field == 2) changed.scope.kbGuid = "other-kb";
        if (field == 3) changed.documentGuid += "-other";
        if (field == 4) ++changed.openSequence;
        if (field == 5) changed.scope.certificate.encryptedD += "A";
        if (field == 6) changed.scope.certificate.hint = "another hint";
        widget.setContext(changed, true);
        // Also exercise leave-and-return ABA: identical GUID is not a ticket.
        if (field == 3) widget.setContext(context, true);
        p->resume.release(); check(pool.waitForDone(3000), "canceled old-context worker finishes independently of the widget");
        drain();
        check(p->cancels == 1 && ready == 0 && !widget.authorizedFor(context)
              && !widget.takeCredential(changed),
              "server account kb note opening key or context reset drops late successful credentials including ABA");
    }
    for (int action = 0; action < 5; ++action) {
        QThreadPool pool; const auto p = fresh(); p->hold = true;
        auto* widget = new Widget(services(p, pool)); int ready = 0, manual = 0;
        QObject::connect(widget, &Widget::credentialReady, &app, [&] { ++ready; });
        QObject::connect(widget, &Widget::manualPasswordRequested, &app, [&] { ++manual; });
        show(*widget, context); click(*widget);
        check(until([&] { return p->entered.load(); }), "cancel-lifecycle fixture begins a real background synthetic read");
        if (action == 0) widget->cancel();
        if (action == 1) button(*widget,"biometricUnlockManual")->click();
        if (action == 2) widget->hide();
        if (action == 3) widget->systemLocked();
        if (action == 4) widget->beginShutdown();
        check(p->cancels == 1 && !widget->authorizedFor(context),
              "cancel manual hide lock and quit immediately invalidate pending access without a save call");
        p->resume.release(); check(pool.waitForDone(3000), "late canceled worker returns without touching GUI objects");
        drain();
        check(ready == 0 && !widget->takeCredential(context) && manual == (action == 1 ? 1 : 0),
              "late result never overrides manual fallback cancellation hiding lock or quit");
        if (action == 4) {
            widget->setContext(context, true); click(*widget);
            check(widget->stage() == Widget::Stage::Closed && p->made == 1,
                  "quit is terminal and later context or click cannot resurrect authentication");
        }
        delete widget;
    }
    {
        QThreadPool pool; const auto p = fresh(); p->hold = true;
        auto* parent = new QWidget; auto* widget = new Widget(services(p, pool), parent);
        parent->show(); show(*widget, context); int ready = 0;
        QObject::connect(widget, &Widget::credentialReady, &app, [&] { ++ready; });
        click(*widget); check(until([&] { return p->entered.load(); }), "parent-destruction fixture reaches background read");
        QPointer<Widget> guard(widget); delete parent;
        p->resume.release(); check(pool.waitForDone(3000), "worker retains its operation after the parent and prompt are destroyed");
        drain(); check(!guard && p->cancels == 1 && ready == 0,
                       "destroyed owner receives no callback or credential from late authentication");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool)); int ready = 0;
        QObject::connect(&widget, &Widget::credentialReady, &widget, [&] { ++ready; });
        show(widget, context); click(widget);
        check(pool.waitForDone(3000) && widget.stage() == Widget::Stage::Working,
              "worker can finish before the GUI has consumed its result");
        auto next = context; ++next.openSequence; widget.setContext(next, true); drain();
        check(ready == 0 && !widget.takeCredential(next), "context change between worker completion and GUI polling rejects the old result");
    }
    {
        QThreadPool pool; pool.setMaxThreadCount(1); QSemaphore entered, release;
        pool.start(QRunnable::create([&] { entered.release(); release.tryAcquire(1,3000); }));
        check(entered.tryAcquire(1,3000), "queue fixture occupies only its private synthetic thread pool");
        const auto p = fresh(); Widget widget(services(p, pool)); show(widget, context); click(widget);
        pool.clear();
        check(until([&] { return widget.stage() == Widget::Stage::Ready; }) && p->reads == 0,
              "discarded queued operation returns to manual-ready state without reading a credential");
        release.release(); check(pool.waitForDone(3000), "queue fixture releases and joins all of its own work");
    }
    {
        QThreadPool pool; Widget widget({[] { return std::shared_ptr<Widget::Operation>{}; }, &pool});
        show(widget, context); click(widget);
        check(widget.stage() == Widget::Stage::Ready && !widget.takeCredential(context), "missing provider cannot leave a success or a stuck busy prompt");
    }
    {
        QThreadPool pool; Widget widget({[]() -> std::shared_ptr<Widget::Operation> { throw std::runtime_error("synthetic"); }, &pool});
        show(widget, context); click(widget);
        check(widget.stage() == Widget::Stage::Ready && !widget.takeCredential(context), "factory exception preserves manual fallback");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool));
        show(widget, context);
        QObject::connect(&widget, &Widget::stageChanged, &widget, [&] {
            if (widget.stage() == Widget::Stage::Working) widget.cancel();
        });
        click(widget);
        check(p->made == 0 && widget.stage() == Widget::Stage::Ready,
              "reentrant cancellation before operation creation starts no native work");
    }
    {
        QThreadPool pool; const auto p = fresh(); auto* widget = new Widget(services(p, pool)); int ready = 0;
        QObject::connect(widget, &Widget::credentialReady, &app, [&] { ++ready; });
        QObject::connect(widget, &Widget::stageChanged, &app, [widget] {
            if (widget->stage() == Widget::Stage::Verified) delete widget;
        });
        QPointer<Widget> guard(widget); show(*widget, context); click(*widget);
        check(until([&] { return !guard; }) && ready == 0,
              "deleting the prompt at verified-state delivery suppresses the later credential signal");
        check(pool.waitForDone(3000), "verified-state destruction leaves no worker to access the deleted prompt");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool));
        show(widget, context); click(widget);
        check(until([&] { return widget.stage() == Widget::Stage::Verified; }), "one-shot reentrancy fixture receives a locally verified result");
        QObject::connect(&widget, &Widget::stageChanged, &widget, [&] {
            if (widget.stage() == Widget::Stage::Consumed) {
                auto next = context; ++next.openSequence; widget.setContext(next, true);
            }
        });
        const auto result = widget.takeCredential(context);
        check(!result && result.password.isEmpty() && !widget.authorizedFor(context),
              "note change during credential consumption prevents even the synchronous return of the stale password");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool));
        bool delivered = false, leaked = false;
        QObject::connect(&widget, &Widget::stageChanged, &widget, [&] {
            if (widget.stage() == Widget::Stage::Verified) {
                // Queue cancellation first, then the notification below. A
                // queued notification contains no secret or authorization.
                QTimer::singleShot(0, &widget, [&] { widget.setContext(context, false); });
            }
        });
        QObject::connect(&widget, &Widget::credentialReady, &app, [&] {
            delivered = true; const auto value = widget.takeCredential(context);
            leaked = bool(value) || !value.password.isEmpty();
        }, Qt::QueuedConnection);
        show(widget, context); click(widget);
        check(until([&] { return delivered; }) && !leaked && !widget.authorizedFor(context),
              "late queued ready notification contains no password and cannot consume after remembered access is disabled");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget* owner = nullptr;
        Widget widget({[&] {
            p->made.fetch_add(1); click(*owner);
            return std::make_shared<FakeRead>(p);
        }, &pool}); owner = &widget;
        show(widget, context); click(widget);
        check(until([&] { return widget.stage() == Widget::Stage::Verified; }) && p->made == 1 && p->reads == 1,
              "reentrant click from operation creation cannot construct or schedule a second read");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget* owner = nullptr;
        Widget widget({[&] {
            owner->setContext(context, false);
            return std::make_shared<FakeRead>(p);
        }, &pool}); owner = &widget;
        show(widget, context); click(widget); drain();
        check(p->reads == 0 && p->cancels == 1 && widget.stage() == Widget::Stage::Unavailable,
              "context invalidation during factory construction cancels its late operation before any read");
    }
    {
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool)); show(widget, context);
        check(QMetaObject::invokeMethod(&app, "aboutToQuit", Qt::DirectConnection)
              && widget.stage() == Widget::Stage::Closed && p->made == 0,
              "actual application about-to-quit connection closes the idle prompt without starting native work");
    }
    const QString evidence = qEnvironmentVariable("WIZNOTE_BIOMETRIC_UI_EVIDENCE_DIR");
    if (!evidence.isEmpty()) {
        QDir().mkpath(evidence);
        QThreadPool pool; const auto p = fresh(); Widget widget(services(p, pool));
        show(widget, context);
        check(widget.grab().save(QDir(evidence).filePath("biometric-ready.png")), "synthetic ready-state screenshot saves without real account content");
        widget.beginShutdown();
        check(widget.grab().save(QDir(evidence).filePath("biometric-closed.png")), "synthetic quit-state screenshot saves without a system authentication prompt");
    }
    qInfo().noquote() << "biometric-unlock-ui checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
