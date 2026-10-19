#include "WizStandaloneStorage.h"
#include "WizBuildIdentity.h"
#include "WizNotePresentation.h"
#include "WizUpgrade.h"
#include "WizWebSettingsDialog.h"
#include "gui/documentviewer/WizSvgEditorDialog.h"
#include "share/WizWebEngineView.h"
#include "share/WizGlobal.h"
#include "utils/WizPathResolve.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QPointer>
#include <memory>
#include <functional>
#include <QWebEngineUrlRequestInterceptor>

class StartupNoRequests final : public QWebEngineUrlRequestInterceptor {
    void interceptRequest(QWebEngineUrlRequestInfo& info) override { info.block(true); }
};
class StartupInlineOnlyRequests final : public QWebEngineUrlRequestInterceptor {
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme=info.requestUrl().scheme();
        if(scheme!="data"&&scheme!="about")info.block(true);
    }
};

// Use the actual dialog constructor and delayed virtual dispatch. Override
// only loading, so no settings website, token acquisition or account is used.
class StartupWebDialogProbe final : public WizWebSettingsDialog {
public:
    explicit StartupWebDialogProbe(int& calls)
        : WizWebSettingsDialog(QStringLiteral("about:blank"),QSize(200,120)),m_calls(calls) {}
protected:
    void load() override { ++m_calls; }
private:
    int& m_calls;
};

// Actual SVG dialog/slots, with ONLY remote loading suppressed. Its real local
// page and C++ save continuation can be exercised without a handwriting site.
class StartupSvgDialogProbe final : public WizSvgEditorDialog {
public:
    StartupSvgDialogProbe(const QString& htmlFile,QWidget* parent,
        std::function<void(bool,std::function<void(bool)>)> save)
        : WizSvgEditorDialog(QStringLiteral("about:blank"),{},std::move(save),htmlFile,parent) {}
    using WizSvgEditorDialog::reject;
    using WizSvgEditorDialog::onLoaded;
protected:
    void load() override {}
};

int wizStandaloneLockProbe(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 4) return 3;
    WizStandaloneStorage::Session session; QString error;
    return session.prepare(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), error) ? 0 : 2;
}
int wizStandaloneStartupSelfTest(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Standalone Regression Test");
    app.setOrganizationName(WizBuildIdentity::organizationName());
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
    StartupNoRequests requests;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString base = QFileInfo(temporary.path()).canonicalFilePath();
    const QString home = QDir(base).filePath("test-home");
    QDir().mkpath(home);
    qputenv("XDG_CACHE_HOME", (base + "/cache").toUtf8());
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << name; failures += !ok; return ok;
    };
    using namespace WizStandaloneStorage;
    const QString root = defaultRoot(home);
    QString normalized, error;
    check(root == home + "/Library/Application Support/WizNotePlus-AppleSilicon", "default root is standalone and independent of bundle location");
    check(validateRoot(home, root + "/", normalized, error) && normalized == root, "normalizes a valid dedicated absolute root");
    check(!validateRoot(home, QString(), normalized, error), "empty root rejected without fallback");
    check(!validateRoot(home, "relative-store", normalized, error), "relative root rejected");
    check(!validateRoot(home, home, normalized, error), "whole home rejected");
    check(!validateRoot(home, "/", normalized, error), "filesystem root rejected");
    check(!validateRoot(home, base + "/outside", normalized, error), "outside-home override rejected");
    check(!validateRoot(home, home + "/.wiznote", normalized, error), "old WizNote default rejected");
    check(!validateRoot(home, home + "/WizNote/account", normalized, error), "historical WizNote directory rejected");
    check(!validateRoot(home, home + "/.WIZNOTE/account", normalized, error), "case-insensitive old-data descendants rejected");
    check(!validateRoot(home, home + "/Library/Containers/cn.wiz.wiznoteformac/Data/Documents", normalized, error), "old sandbox container rejected");
    check(!validateRoot(home, home + "/Documents", normalized, error), "old sandbox Documents root rejected");
    check(!validateRoot(home, home + "/safe/../.wiznote", normalized, error), "dot-dot alias of old store rejected");
    const QString linked = home + "/linked";
    QFile::link(home, linked);
    check(!validateRoot(home, linked + "/store", normalized, error), "symlink ancestor rejected");
    QFile::remove(linked); // Only the synthetic link created above.
    Session first;
    check(first.prepare(home, QString(), error) && first.root() == root
        && qEnvironmentVariable("WIZNOTE_DATA_STORE") == root, "prepare installs safe default before existing path consumers");
    check(!QFileInfo::exists(home + "/.wiznote"), "default prepare never creates or reads old store");
    Session second;
    check(!second.prepare(home, root, error), "same-process duplicate startup blocked");
    Session emptyOverride;
    check(!emptyOverride.prepare(home, QStringLiteral(""), error), "explicit empty environment override cannot fall back");
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--standalone-lock-probe", home, root});
    check(child.waitForFinished(15000) && child.exitCode() == 2, "second process cannot open locked store");
    QDir().mkpath(root + "/fixture-account");
    QSettings css(root + "/fixture-account/wiznote.ini", QSettings::IniFormat);
    const QString cssSource = "/* WizNotePlus scientific math environments v1 */\nbody {color:#263950;}";
    css.setValue("NotePresentation/screenCss", cssSource); css.setValue("NotePresentation/enabled", true); css.sync();
    WizGlobal::setSettings(new QSettings(css.fileName(), QSettings::IniFormat));
    WizGlobal::setGlobalSettings(new QSettings(root + "/wiznote.ini", QSettings::IniFormat));
    check(!first.hasConsent(), "first launch has no implicit account consent");
    bool canceledDialog = false;
    QTimer::singleShot(100, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box) { canceledDialog = box->objectName() == "wiz-standalone-first-launch"
            && box->defaultButton() == box->button(QMessageBox::Cancel); box->done(QMessageBox::Cancel); }
    });
    check(!confirmFirstLaunch(first) && canceledDialog && !first.hasConsent(), "cancel is default and does not authorize account startup");
    QTimer::singleShot(100, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->done(QMessageBox::Ok);
    });
    check(confirmFirstLaunch(first) && first.hasConsent(), "explicit first-launch confirmation is saved locally");
    check(confirmFirstLaunch(first) && !QApplication::activeModalWidget(), "next launch does not repeat confirmation");
    check(WizNotePresentation::loadStyle(&css).screen == cssSource, "existing per-user CSS remains unchanged under new organization identity");
    check(Utils::WizPathResolve::dataStorePath() == root + '/', "actual path resolver uses bootstrap root");
    check(Utils::WizPathResolve::cachePath() == root + "/cache/", "inherited XDG cache cannot redirect standalone cache to old data");
    check(Utils::WizPathResolve::tempPath().startsWith(root + "/temp/session-"), "temporary cleanup is per-store and per-process");
    auto* primary = WizWebEngineProfile();
    primary->setUrlRequestInterceptor(&requests);
    check(primary->persistentStoragePath().startsWith(root + "/webengine/")
        && primary->cachePath().startsWith(root + "/webengine/"), "primary browser cookies and cache stay in the store");
    QObject object;
    WizWebEngineInjectObjectCollection objects; objects.insert("testObject", &object);
    auto* secondary = createWebEngineProfile(objects, &app);
    if (secondary) secondary->setUrlRequestInterceptor(&requests);
    check(secondary && secondary->persistentStoragePath() == profileStorage(root, "WizNoteWebEngineProfile")
        && secondary->cachePath() == profileCache(root, "WizNoteWebEngineProfile"), "secondary service browser profile is isolated too");
    bool updateFinished = false;
    WizUpgradeChecker updates;
    QObject::connect(&updates, &WizUpgradeChecker::checkFinished, [&](QJsonObject stable, QJsonObject test) {
        updateFinished = stable.isEmpty() && test.isEmpty();
    });
    updates.checkUpgrade();
    check(!WizBuildIdentity::appleSiliconTrial || updateFinished, "trial legacy update check ends synchronously without legacy releases");
    {
        const auto pump=[](int milliseconds) {
            QEventLoop loop;
            QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);
            loop.exec();
        };
        int legacyLate=0,closedLoads=0,liveLoads=0,repeatedLoads=0;
        auto legacyOwner=std::make_unique<QObject>();
        const QPointer<QObject> legacy(legacyOwner.get());
        QTimer::singleShot(30,[legacy,&legacyLate] {if(!legacy)++legacyLate;});
        legacyOwner.reset();
        auto closing=std::make_unique<StartupWebDialogProbe>(closedLoads);
        const QPointer<StartupWebDialogProbe> closed(closing.get());
        closing.reset();
        auto live=std::make_unique<StartupWebDialogProbe>(liveLoads);
        check(liveLoads==0,"web settings retains its delayed rather than inline initialization");
        for(int index=0;index<4;++index){
            auto transient=std::make_unique<StartupWebDialogProbe>(repeatedLoads);
        }
        pump(250);
        check(legacyLate==1,"safe control confirms an ownerless timer still delivers after its source object closes");
        check(closed.isNull()&&closedLoads==0,"actual closed web settings dialog cancels its pending initialization");
        check(liveLoads==1,"actual live web settings dialog still initializes exactly once");
        check(repeatedLoads==0,"rapidly closing actual web dialogs cannot initialize replacement instances");
        live.reset();pump(150);
        check(liveLoads==1&&closedLoads==0&&repeatedLoads==0,
            "dialog destruction leaves no later repeated initialization callback");
    }
    {
        QWidget parent;
        const auto pump=[](int milliseconds) {
            QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();
        };
        const auto answer=[&](WizWebEngineView* view,QMessageBox::StandardButton response,
                              const std::function<void()>& after) {
            bool answered=false,timedOut=false;
            QTimer poll,deadline;deadline.setSingleShot(true);
            QObject::connect(&poll,&QTimer::timeout,&parent,[&] {
                if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                    if(auto* button=box->button(response)){
                        poll.stop();answered=true;button->click();
                        if(after)after();
                    }
                }
            });
            QObject::connect(&deadline,&QTimer::timeout,&parent,[&] {
                timedOut=true;poll.stop();
                for(auto* widget:QApplication::topLevelWidgets())
                    if(auto* box=qobject_cast<QMessageBox*>(widget))box->reject();
            });
            poll.start(5);deadline.start(2000);
            // Simulate the notification, never terminate a renderer process.
            view->renderProcessTerminated(QWebEnginePage::CrashedTerminationStatus,42);
            poll.stop();deadline.stop();
            return answered&&!timedOut;
        };
        auto living=std::make_unique<WizWebEngineView>(WizWebEngineInjectObjectCollection{},&parent);
        check(answer(living.get(),QMessageBox::No,[]{})&&living,
            "actual renderer-error question keeps a live view when reload is declined");
        auto closing=std::make_unique<WizWebEngineView>(WizWebEngineInjectObjectCollection{},&parent);
        const QPointer<WizWebEngineView> closed(closing.get());
        const bool answeredBeforeClose=answer(closing.get(),QMessageBox::Yes,[&]{closing.reset();});
        pump(50);
        check(answeredBeforeClose&&closed.isNull(),
            "closing actual web view inside reload question cannot schedule access after destruction");
        auto pending=std::make_unique<WizWebEngineView>(WizWebEngineInjectObjectCollection{},&parent);
        const QPointer<WizWebEngineView> pendingClosed(pending.get());
        const bool queuedReload=answer(pending.get(),QMessageBox::Yes,[]{});
        pending.reset();pump(50);
        check(queuedReload&&pendingClosed.isNull(),
            "closing actual view after accepting reload cancels the queued zero-delay action");
        int starts=0;bool initialFinished=false;
        QObject::connect(living.get(),&QWebEngineView::loadStarted,&parent,[&]{++starts;});
        QObject::connect(living.get(),&QWebEngineView::loadFinished,&parent,[&](bool){initialFinished=true;});
        living->setHtml("<!doctype html><p>Synthetic reload fixture</p>");
        QElapsedTimer initialWait;initialWait.start();
        while(!initialFinished&&initialWait.elapsed()<5000)pump(5);
        check(initialFinished&&starts>0,"reload fixture finishes its initial navigation before counting a later reload");
        const int beforeReload=starts;
        const bool acceptedReload=answer(living.get(),QMessageBox::Yes,[]{});
        pump(150);
        check(initialFinished&&acceptedReload&&starts>beforeReload,
            "live actual view still starts its original reload after accepting the error question");
    }
    {
        StartupInlineOnlyRequests inlineOnly;
        primary->setUrlRequestInterceptor(&inlineOnly);
        QWidget parent;parent.resize(320,240);
        const auto pump=[](int milliseconds) {
            QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();
        };
        std::function<void(bool)> pending;
        int submitted=0,delivered=0,rejected=0;bool changed=true;
        const auto save=[&](bool value,std::function<void(bool)> completion) {
            ++submitted;changed=value;pending=std::move(completion);
        };
        auto living=std::make_unique<StartupSvgDialogProbe>(base+"/svg-fixture/index.html",&parent,save);
        QObject::connect(living.get(),&QDialog::rejected,&parent,[&]{++rejected;});
        living->saveData("{\"closeDialog\":false}",{},{});
        const bool submittedUnchanged=submitted==1&&!changed&&bool(pending);
        if(pending){++delivered;pending(true);}
        check(submittedUnchanged&&delivered==1&&rejected==0,
            "actual SVG unchanged save keeps a live dialog open when close was not requested");
        living->saveData("{\"closeDialog\":true}",{},{});
        if(pending){++delivered;pending(true);}
        check(submitted==2&&delivered==2&&rejected==1,
            "actual SVG successful save still closes its live originating dialog");

        living->saveData("{\"closeDialog\":true}",{},{});
        bool warningSeen=false;
        QTimer warningPoll,warningDeadline;warningDeadline.setSingleShot(true);
        QObject::connect(&warningPoll,&QTimer::timeout,&parent,[&] {
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                warningSeen=box->text()==QStringLiteral("Failed to save note.")&&box->parentWidget()==living.get();
                if(auto* ok=box->button(QMessageBox::Ok))ok->click();else box->reject();
            }
        });
        QObject::connect(&warningDeadline,&QTimer::timeout,&parent,[&] {
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* box=qobject_cast<QMessageBox*>(widget))box->reject();
        });
        warningPoll.start(5);warningDeadline.start(2000);
        if(pending){++delivered;pending(false);}
        warningPoll.stop();warningDeadline.stop();
        check(warningSeen&&living&&rejected==1,
            "actual SVG save failure preserves its original warning and does not close a live dialog");

        auto closing=std::make_unique<StartupSvgDialogProbe>(base+"/svg-fixture/index.html",&parent,save);
        const QPointer<StartupSvgDialogProbe> closed(closing.get());
        closing->saveData("{\"closeDialog\":true}",{},{});
        auto completedAfterClose=std::move(pending);
        closing.reset();
        if(completedAfterClose){++delivered;completedAfterClose(true);}
        check(closed.isNull()&&delivered==4,
            "actual SVG save completion is still delivered after dialog destruction without stale close UI");
        if(completedAfterClose){++delivered;completedAfterClose(false);}
        pump(50);
        bool staleWarning=false;
        for(auto* widget:QApplication::topLevelWidgets())
            if(qobject_cast<QMessageBox*>(widget)&&widget->isVisible())staleWarning=true;
        check(delivered==5&&!staleWarning,
            "actual SVG late failed completion cannot open a warning on its destroyed owner");

        const auto localPage=[&](StartupSvgDialogProbe* dialog,const QString& reply) {
            struct Ready {bool loaded=false,ok=false,queried=false,painter=false;};
            const auto ready=std::make_shared<Ready>();
            const auto connection=QObject::connect(dialog->web(),&QWebEngineView::loadFinished,&parent,[ready](bool ok){ready->loaded=true;ready->ok=ok;});
            dialog->web()->setHtml("<!doctype html><script>window.wizSvgPainter={save:function(o){window.closeRequested=o.closeDialog;return "+reply+";}};</script><p>Synthetic SVG UI</p>");
            QElapsedTimer deadline;deadline.start();
            while(!ready->loaded&&deadline.elapsed()<5000)pump(5);
            QObject::disconnect(connection);
            dialog->web()->page()->runJavaScript("!!window.wizSvgPainter&&typeof wizSvgPainter.save==='function'",[ready](const QVariant& value){ready->queried=true;ready->painter=value.toBool();});
            while(!ready->queried&&deadline.elapsed()<5000)pump(5);
            return ready->ok&&ready->queried&&ready->painter;
        };
        auto scriptClose=std::make_unique<StartupSvgDialogProbe>(base+"/svg-fixture/index.html",&parent,save);
        int scriptRejected=0;
        QObject::connect(scriptClose.get(),&QDialog::rejected,&parent,[&]{++scriptRejected;});
        const bool invalidLoaded=localPage(scriptClose.get(),"undefined");
        scriptClose->reject();pump(150);
        check(invalidLoaded&&scriptRejected==1,
            "actual SVG invalid painter reply retains its original live-dialog close fallback");
        auto scriptPending=std::make_unique<StartupSvgDialogProbe>(base+"/svg-fixture/index.html",&parent,save);
        const bool validLoaded=localPage(scriptPending.get(),"true");
        int prematureClose=0;
        QObject::connect(scriptPending.get(),&QDialog::rejected,&parent,[&]{++prematureClose;});
        scriptPending->reject();pump(150);
        check(validLoaded&&prematureClose==0,
            "actual SVG valid painter reply continues waiting for the existing save protocol");
        const QPointer<StartupSvgDialogProbe> queryClosed(scriptPending.get());
        scriptPending->reject();scriptPending.reset();pump(150);
        check(queryClosed.isNull()&&!QApplication::activeModalWidget(),
            "destroying actual SVG dialog during its pending JavaScript close query is safe");
        auto error=std::make_unique<StartupSvgDialogProbe>(base+"/svg-fixture/index.html",&parent,save);
        int errorClosed=0;
        QObject::connect(error.get(),&QDialog::rejected,&parent,[&]{++errorClosed;});
        error->onLoaded(false);error->reject();
        check(errorClosed==1,"actual SVG existing load-error path still rejects synchronously");
        const QString svg=QString::fromUtf8("<svg xmlns=\"http://www.w3.org/2000/svg\"><text>合成数学 x²</text></svg>");
        const QString html=QString::fromUtf8("<!doctype html><html><head></head><body>合成手写笔记</body></html>");
        living->saveData("{\"fileName\":\"shape.svg\",\"isFullHtml\":true,\"closeDialog\":false}",svg,html);
        const bool changedSaveSubmitted=changed&&bool(pending);
        if(pending)pending(true);
        QFile savedSvg(base+"/svg-fixture/index_files/shape.svg"),savedHtml(base+"/svg-fixture/index.html");
        const bool filesOpened=savedSvg.open(QIODevice::ReadOnly)&&savedHtml.open(QIODevice::ReadOnly);
        check(changedSaveSubmitted&&filesOpened&&savedSvg.readAll()==svg.toUtf8()
            &&savedHtml.readAll()==QByteArray::fromHex("efbbbf")+html.toUtf8(),
            "actual SVG save retains original UTF-8 SVG and BOM-prefixed HTML bytes");
        primary->setUrlRequestInterceptor(&requests);
    }
    delete secondary; delete primary;
    first.release();
    check(second.prepare(home, root, error) && second.hasConsent(), "normal close releases lock and preserves first-launch confirmation");
    second.release();
    child.start(QCoreApplication::applicationFilePath(), {"--standalone-lock-probe", home, root});
    check(child.waitForFinished(15000) && child.exitCode() == 0, "new process can reopen only after previous lock release");
    QDir().mkpath(root + "/.standalone-copy-in-progress");
    check(!validateRoot(home, root, normalized, error), "incomplete data copy blocks account startup");
    QDir().rmdir(root + "/.standalone-copy-in-progress");
    QFile::link(home, root + "/unsafe-account");
    check(!validateRoot(home, root, normalized, error), "link inside existing store rejected before account access");
    QFile::remove(root + "/unsafe-account");
    check(validateRoot(home, root, normalized, error), "store can reopen after synthetic unsafe link removal");
    check(WizNotePresentation::loadStyle(&css).screen == cssSource, "all startup checks retain exact existing CSS");
    QProcess invalidTest;
    auto testEnvironment = QProcessEnvironment::systemEnvironment();
    const QString unusedStore = base + "/unknown-test-unused-store";
    testEnvironment.insert("WIZNOTE_DATA_STORE", unusedStore);
    invalidTest.setProcessEnvironment(testEnvironment);
    invalidTest.start(QCoreApplication::applicationFilePath(), {"--mac-main-window-controls-self-test"});
    const bool refused = invalidTest.waitForFinished(5000);
    if (!refused) { invalidTest.kill(); invalidTest.waitForFinished(3000); }
    check(refused && invalidTest.exitStatus() == QProcess::NormalExit && invalidTest.exitCode() == 2
          && invalidTest.readAllStandardError().contains("normal startup refused") && !QFileInfo::exists(unusedStore),
          "misspelled self-test refuses normal startup before opening or creating an account store");
    qInfo() << "Standalone startup failures:" << failures;
    return failures ? 1 : 0;
}
