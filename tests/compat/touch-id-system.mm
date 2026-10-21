#include "mac/WizBiometricKeychain.h"
#include "widgets/WizBiometricUnlockWidget.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#import <AppKit/AppKit.h>

// Interactive, opt-in, synthetic-only. Never registered with unattended CTest.
// Uses the main client's actual native backend AND default async unlock widget,
// not a supplied authentication provider. No normal account/DB/bootstrap exists.
int wizTouchIdSystemProbe(int argc, char** argv)
{
    const bool prerequisite = argc == 2 && QByteArray(argv[1]) == "--touch-id-prerequisite-probe";
    const bool interactive = argc == 2 && QByteArray(argv[1]) == "--touch-id-system-probe"
        && qgetenv("WIZNOTE_SYNTHETIC_TOUCH_ID_CONSENT") == "create-read-delete";
    if (!prerequisite && !interactive) {
        qCritical() << "Isolated Touch ID fixture: explicit test option/consent required; normal startup refused.";
        return 2;
    }
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    qputenv("QT_QPA_PLATFORM", "cocoa");
    QApplication app(argc, argv);
    using namespace WizBiometricKeychain;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    if (!check(hasSigningPrerequisites(), "running main executable has valid development signing and keychain entitlements")) return 1;
    if (prerequisite) {
        qInfo() << "Prerequisite only: no SecItem or authentication calls; no account opened.";
        return 0;
    }
    const QString password = "Synthetic-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto generated = WizCertificateCreation::generate(password, password, QStringLiteral("仅本次合成测试"));
    if (!check(bool(generated), "random synthetic certificate generated in memory without an account")) return 1;
    const Scope scope{QStringLiteral("https://touch-id-test.invalid/"),
        QUuid::createUuid().toString(QUuid::WithoutBraces), {}, generated.certificate};
    const auto enrolled = enroll(scope, password, true);
    qInfo() << "Synthetic enrollment result" << int(enrolled);
    // Never remove a pre-existing/colliding item. Ownership starts ONLY on add success.
    if (!check(enrolled == Error::None, "create-only enrollment of one unique synthetic data-protection item")) return 1;
    qInfo().noquote() << "Owned synthetic item identity (no password):" << itemIdentity(scope);
    check(enroll(scope, password, true) == Error::AlreadyExists, "repeat enrollment refuses overwrite of the owned synthetic item");
    QWidget window;
    window.setWindowTitle(QStringLiteral("WizNotePlus — Touch ID 合成测试"));
    auto* layout = new QVBoxLayout(&window);
    auto* notice = new QLabel(QStringLiteral("仅验证随机合成凭据，不打开账号或真实笔记。\n"
        "即将请求一次系统 Touch ID。取消或关闭也会清理本次合成记忆。"), &window);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* unlock = new WizBiometricUnlockWidget(&window); // Actual native default, no test service.
    layout->addWidget(unlock);
    WizBiometricUnlockWidget::Context context{scope, QStringLiteral("synthetic-document"), 1};
    unlock->setContext(context, true);
    bool started = false, finished = false, verified = false;
    QObject::connect(unlock, &WizBiometricUnlockWidget::stageChanged, &app, [&] {
        if (unlock->stage() == WizBiometricUnlockWidget::Stage::Working) started = true;
        else if (started && !finished && unlock->stage() == WizBiometricUnlockWidget::Stage::Ready) {
            finished = true;
            check(false, "native authentication completed without a credential (cancel/unavailable/denied)");
            app.quit();
        }
    });
    QObject::connect(unlock, &WizBiometricUnlockWidget::credentialReady, &app, [&] {
        finished = true;
        auto result = unlock->takeCredential(context);
        verified = result && result.password == password;
        check(verified, "actual Touch ID returns the enrolled secret through production async widget exactly once");
        result.password.clear();
        check(unlock->authorizedFor(context), "consumed credential authorizes only the original synthetic context");
        check(unlock->takeCredential(context).error == Error::Canceled, "credential cannot be consumed twice");
        app.quit();
    });
    QObject::connect(unlock, &WizBiometricUnlockWidget::manualPasswordRequested, &app, &QCoreApplication::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        finished = true; check(false, "native authentication timed out");
        unlock->beginShutdown(); app.quit();
    });
    deadline.start(120000);
    window.resize(630, 200);
    window.show(); window.raise(); window.activateWindow();
    [NSApp activateIgnoringOtherApps:YES];
    QTimer::singleShot(1200, &window, [&] {
        qInfo() << "READY_FOR_TOUCH_ID: synthetic item only; use fingerprint or cancel.";
        if (auto* button = unlock->findChild<QPushButton*>("biometricUnlockStart")) button->click();
    });
    app.exec();
    unlock->beginShutdown();
    // Join the owned read before deleting its exact item. No normal save tasks exist.
    QThreadPool::globalInstance()->waitForDone();
    if (!verified && !failures) check(false, "window closed before biometric credential verification");
    const auto removed = forget(scope, true);
    qInfo() << "Synthetic cleanup result" << int(removed);
    check(removed == Error::None, "only the successfully created synthetic item is deleted");
    if (removed == Error::None)
        check(forget(scope, true) == Error::Missing, "exact synthetic item is absent after cleanup");
    check(WizCertificateCreation::verify(scope.certificate, password) == WizCertificateCreation::Error::None,
          "in-memory synthetic certificate stays valid after local memory deletion");
    qInfo() << "Touch ID system checks" << checks << "failures" << failures
            << "real account/notes opened: false";
    return failures ? 1 : 0;
}
