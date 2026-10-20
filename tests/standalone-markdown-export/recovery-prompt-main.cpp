#include "widgets/WizMarkdownExportRecoveryPrompt.h"

#include <QAccessible>
#include <QApplication>
#include <QFile>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <functional>

using namespace WizMarkdownExportRecovery;
using Prompt = WizMarkdownExportRecoveryPrompt;
static int checks = 0;
static int failures = 0;

static void check(bool condition, const QString& label)
{
    ++checks;
    if (!condition) {
        ++failures;
        QTextStream(stderr) << "FAIL: " << label << Qt::endl;
    }
}

static QPushButton* button(Prompt& prompt, const char* suffix)
{
    auto* result = prompt.findChild<QPushButton*>(QStringLiteral("recoveryPrompt") + QLatin1String(suffix));
    if (!result)
        qFatal("required prompt button missing");
    return result;
}

static QPlainTextEdit* details(Prompt& prompt)
{
    auto* result = prompt.findChild<QPlainTextEdit*>(QStringLiteral("recoveryPromptDetails"));
    if (!result)
        qFatal("required details field missing");
    return result;
}

static Inspection fixture()
{
    Inspection inspection;
    inspection.availability = Availability::AwaitingDecision;
    // Neither backend text nor diagnostic paths may appear in the initial UI.
    inspection.userVisibleSummary = QStringLiteral("/private/synthetic/never-show-summary <b>backend</b>");
    inspection.diagnosticDetail = QStringLiteral("/private/synthetic/export.md\n<reason>&literal\n只读原因");
    return inspection;
}

static void show(Prompt& prompt)
{
    prompt.show();
    prompt.activateWindow(); // This process is forced offscreen.
    QApplication::processEvents();
}

static void retained(const QString& label, const std::function<void(Prompt&)>& action)
{
    Prompt prompt(fixture(), LockState::Available);
    QSignalSpy finished(&prompt, &QDialog::finished);
    show(prompt);
    check(button(prompt, "Retain")->hasFocus(), label + " initial retain focus");
    action(prompt);
    check(prompt.hasDecision() && prompt.decision() == Decision::Retain, label + " retains");
    check(finished.count() == 1 && finished.at(0).at(0).toInt() == QDialog::Rejected,
        label + " publishes once rejected");
    button(prompt, "Confirm")->click();
    prompt.accept();
    prompt.reject();
    prompt.done(QDialog::Accepted);
    prompt.retainForExit();
    check(finished.count() == 1 && prompt.decision() == Decision::Retain,
        label + " cannot later confirm or publish twice");
}

static void defaultsAndKeys()
{
    {
        Prompt prompt(fixture(), LockState::Available);
        check(!prompt.hasDecision() && prompt.decision() == Decision::Retain, "unsettled defaults retain");
        check(details(prompt)->isHidden() && details(prompt)->toPlainText().isEmpty(), "details initially hidden and empty");
        check(button(prompt, "Retain")->isDefault(), "retain only default");
        check(!button(prompt, "Confirm")->isDefault() && !button(prompt, "ShowDetails")->isDefault(), "other buttons not default");
        check(!button(prompt, "Confirm")->autoDefault() && !button(prompt, "ShowDetails")->autoDefault(), "focus cannot change default");
        check(prompt.findChildren<QTimer*>().isEmpty(), "no countdown timer");
        QTest::qWait(20);
        check(!prompt.hasDecision(), "no elapsed-time decision");
        button(prompt, "Confirm")->click();
        check(!prompt.hasDecision(), "hidden confirmation ignored");
        show(prompt);
        check(button(prompt, "Retain")->hasFocus(), "show focuses retain");
        check(prompt.windowModality() == Qt::WindowModal, "window-modal not application-modal");
        QTest::keyClick(button(prompt, "Retain"), Qt::Key_Tab);
        check(button(prompt, "ShowDetails")->hasFocus(), "tab retain to details");
        QTest::keyClick(button(prompt, "ShowDetails"), Qt::Key_Tab);
        check(button(prompt, "Confirm")->hasFocus(), "tab details to confirm");
        QTest::keyClick(button(prompt, "Confirm"), Qt::Key_Backtab);
        check(button(prompt, "ShowDetails")->hasFocus(), "reverse tab confirm to details");
        prompt.hide();
        button(prompt, "Confirm")->click();
        check(!prompt.hasDecision(), "hidden-after-show confirmation ignored");
    }
    retained("return", [](Prompt& p) { QTest::keyClick(button(p, "Retain"), Qt::Key_Return); });
    retained("keypad enter", [](Prompt& p) { QTest::keyClick(button(p, "Retain"), Qt::Key_Enter); });
    retained("escape", [](Prompt& p) { QTest::keyClick(button(p, "Retain"), Qt::Key_Escape); });
    retained("close", [](Prompt& p) { p.close(); });
    retained("retain click", [](Prompt& p) { button(p, "Retain")->click(); });
    retained("external accept", [](Prompt& p) { p.accept(); });
    retained("external accepted result", [](Prompt& p) { p.done(QDialog::Accepted); });
    retained("external arbitrary result", [](Prompt& p) { p.done(42); });
    retained("exit notification", [](Prompt& p) { p.retainForExit(); });
    retained("return focused confirm", [](Prompt& p) {
        button(p, "Confirm")->setFocus();
        QTest::keyClick(button(p, "Confirm"), Qt::Key_Return);
    });
    retained("return focused details", [](Prompt& p) {
        button(p, "ShowDetails")->setFocus();
        QTest::keyClick(button(p, "ShowDetails"), Qt::Key_Return);
    });
    retained("return in read-only details", [](Prompt& p) {
        button(p, "ShowDetails")->click();
        details(p)->setFocus();
        QTest::keyClick(details(p), Qt::Key_Return);
    });
    for (bool keyboard : {false, true}) {
        Prompt prompt(fixture(), LockState::Available);
        QSignalSpy finished(&prompt, &QDialog::finished);
        show(prompt);
        if (keyboard) {
            button(prompt, "Confirm")->setFocus();
            QTest::keyClick(button(prompt, "Confirm"), Qt::Key_Space);
        } else {
            QTest::mouseClick(button(prompt, "Confirm"), Qt::LeftButton);
        }
        check(prompt.hasDecision() && prompt.decision() == Decision::Confirm, "explicit mouse/space confirmation");
        check(finished.count() == 1 && finished.at(0).at(0).toInt() == QDialog::Accepted, "confirmation accepted once");
        prompt.retainForExit();
        prompt.reject();
        prompt.accept();
        button(prompt, "Confirm")->click();
        check(prompt.decision() == Decision::Confirm && finished.count() == 1, "published confirmation not revoked or duplicated");
        check(!button(prompt, "Confirm")->isEnabled() && !button(prompt, "Retain")->isEnabled()
                && !button(prompt, "ShowDetails")->isEnabled(), "settled controls disabled");
    }
}

static void eligibilityAndDetails()
{
    for (Availability availability : {Availability::None, Availability::AwaitingDecision,
             Availability::Busy, Availability::Refused, static_cast<Availability>(99)}) {
        for (LockState lock : {LockState::Available, LockState::Active, LockState::ConfirmedDead,
                 LockState::Refused, static_cast<LockState>(99)}) {
            auto inspection = fixture();
            inspection.availability = availability;
            Prompt prompt(inspection, lock);
            const bool known = availability == Availability::None || availability == Availability::AwaitingDecision
                || availability == Availability::Busy;
            const bool allowed = known && (lock == LockState::ConfirmedDead
                || (availability == Availability::AwaitingDecision && lock == LockState::Available));
            const QString label = QStringLiteral("availability %1 lock %2").arg(int(availability)).arg(int(lock));
            check(button(prompt, "Confirm")->isEnabled() == allowed, label + " eligibility");
            check(button(prompt, "Retain")->isEnabled() && button(prompt, "Retain")->isDefault(), label + " retain remains available");
            show(prompt);
            button(prompt, "Confirm")->click();
            check(prompt.hasDecision() == allowed, label + " disabled confirmation ignored");
            if (!allowed)
                prompt.reject();
            check(prompt.decision() == (allowed ? Decision::Confirm : Decision::Retain), label + " decision");
        }
    }
    Prompt prompt(fixture(), LockState::Available);
    show(prompt);
    for (QLabel* label : prompt.findChildren<QLabel*>()) {
        check(label->textFormat() == Qt::PlainText, "summary/status plain text");
        check(!label->text().contains(QStringLiteral("/private/"))
            && !label->text().contains(QStringLiteral("backend")), "initial labels omit diagnostic paths/backend summary");
    }
    check(details(prompt)->isReadOnly() && details(prompt)->toPlainText().isEmpty(), "details private until explicit request");
    QTest::mouseClick(button(prompt, "ShowDetails"), Qt::LeftButton);
    check(details(prompt)->isVisible() && details(prompt)->toPlainText() == fixture().diagnosticDetail, "explicit details literal exact text");
    check(!prompt.hasDecision(), "viewing details not a decision");
    check(button(prompt, "ShowDetails")->text() == QStringLiteral("隐藏详情"), "details hide label");
    QTest::mouseClick(button(prompt, "ShowDetails"), Qt::LeftButton);
    check(details(prompt)->isHidden() && details(prompt)->toPlainText().isEmpty(), "hidden details text cleared");
    check(button(prompt, "ShowDetails")->text() == QStringLiteral("查看详情"), "details show label restored");
    check(!prompt.hasDecision(), "hiding details not a decision");
    auto empty = fixture();
    empty.diagnosticDetail.clear();
    Prompt noDetails(empty, LockState::Active);
    check(!button(noDetails, "ShowDetails")->isEnabled(), "no empty details action");
}

static void accessibility()
{
    Prompt prompt(fixture(), LockState::Available);
    show(prompt);
    auto* dialog = QAccessible::queryAccessibleInterface(&prompt);
    check(dialog && dialog->role() == QAccessible::Dialog, "accessible dialog role");
    check(dialog && dialog->text(QAccessible::Name) == QStringLiteral("Markdown 导出恢复"), "accessible dialog name");
    for (const char* suffix : {"Retain", "ShowDetails", "Confirm"}) {
        auto* control = button(prompt, suffix);
        auto* interface = QAccessible::queryAccessibleInterface(control);
        check(interface && interface->role() == QAccessible::Button, "accessible button role");
        check(interface && !interface->text(QAccessible::Name).isEmpty()
            && !interface->text(QAccessible::Description).isEmpty(), "accessible button name and description");
        check(interface && !interface->state().disabled, "accessible enabled button state");
        check(interface && !interface->text(QAccessible::Name).contains(QStringLiteral("/private/")), "accessible button has no private path");
    }
    auto* field = QAccessible::queryAccessibleInterface(details(prompt));
    check(field && field->text(QAccessible::Name) == QStringLiteral("恢复详情"), "accessible details name");
    check(field && field->state().readOnly, "accessible details read-only");
    check(field && field->text(QAccessible::Value).isEmpty(), "hidden accessible details value empty");
    button(prompt, "ShowDetails")->click();
    field = QAccessible::queryAccessibleInterface(details(prompt));
    check(field && field->text(QAccessible::Value) == fixture().diagnosticDetail, "explicit accessible details exact value");
    button(prompt, "ShowDetails")->click();
    field = QAccessible::queryAccessibleInterface(details(prompt));
    check(field && field->text(QAccessible::Value).isEmpty(), "rehidden accessible value cleared");
    auto* toggle = QAccessible::queryAccessibleInterface(button(prompt, "ShowDetails"));
    check(toggle && toggle->text(QAccessible::Name) == QStringLiteral("查看详情"), "accessible toggle name restored");
    Prompt busy(fixture(), LockState::Active);
    auto* disabled = QAccessible::queryAccessibleInterface(button(busy, "Confirm"));
    check(disabled && disabled->state().disabled, "accessible busy confirmation disabled");
}

static void lifetimeAndExit()
{
    {
        Prompt prompt(fixture(), LockState::Available);
        QSignalSpy finished(&prompt, &QDialog::finished);
        show(prompt);
        QTimer::singleShot(0, &prompt, [&prompt] { button(prompt, "Confirm")->click(); });
        prompt.retainForExit();
        QApplication::processEvents();
        check(prompt.decision() == Decision::Retain && finished.count() == 1, "exit precedes queued confirmation");
    }
    {
        auto* owner = new QWidget;
        auto* prompt = new Prompt(fixture(), LockState::Available, owner);
        QPointer<Prompt> guarded(prompt);
        int publications = 0;
        bool queuedRan = false;
        QObject::connect(prompt, &QDialog::finished, qApp, [&](int) { ++publications; });
        show(*prompt);
        QTimer::singleShot(0, prompt, [&] { queuedRan = true; });
        delete owner;
        QApplication::processEvents();
        check(!guarded && publications == 0 && !queuedRan, "owner destruction drops prompt and queued work without confirmation");
    }
    for (bool confirm : {false, true}) {
        auto* owner = new QWidget;
        auto* prompt = new Prompt(fixture(), LockState::Available, owner);
        QPointer<Prompt> guarded(prompt);
        int publications = 0;
        Decision observed = Decision::Inspect;
        QObject::connect(prompt, &QDialog::finished, qApp, [&](int result) {
            ++publications;
            observed = prompt->decision();
            check(result == (confirm ? QDialog::Accepted : QDialog::Rejected), "finished result before owner deletion");
            delete owner;
        });
        show(*prompt);
        button(*prompt, confirm ? "Confirm" : "Retain")->click();
        check(!guarded && publications == 1 && observed == (confirm ? Decision::Confirm : Decision::Retain),
            "finished can synchronously destroy owner");
    }
    {
        auto* owner = new QWidget;
        auto* prompt = new Prompt(fixture(), LockState::Available, owner);
        QPointer<Prompt> guarded(prompt);
        show(*prompt);
        int reentries = 0;
        QObject::connect(details(*prompt), &QPlainTextEdit::textChanged, qApp, [&] { ++reentries; delete owner; });
        button(*prompt, "ShowDetails")->click();
        check(guarded && reentries == 0, "private read-only update does not expose unsafe value-change reentry");
        check(guarded && details(*prompt)->toPlainText() == fixture().diagnosticDetail, "blocked value signal does not suppress visible text");
        button(*prompt, "ShowDetails")->click();
        check(guarded && reentries == 0 && details(*prompt)->toPlainText().isEmpty(), "rehide also blocks unsafe value-change reentry");
        delete owner;
        check(!guarded, "details prompt still destroyed with owner");
    }
    {
        Prompt prompt(fixture(), LockState::Available);
        QSignalSpy finished(&prompt, &QDialog::finished);
        show(prompt);
        button(prompt, "Confirm")->setFocus();
        bool reentered = false;
        const auto connection = QObject::connect(qApp, &QApplication::focusChanged, &prompt,
            [&](QWidget* old, QWidget*) {
                if (old == button(prompt, "Confirm") && prompt.hasDecision() && finished.isEmpty()) {
                    reentered = true;
                    prompt.retainForExit();
                }
            });
        button(prompt, "Confirm")->click();
        QObject::disconnect(connection);
        check(reentered, "exit reentry occurs while focused confirmation is disabled");
        check(prompt.decision() == Decision::Retain && finished.count() == 1
            && finished.at(0).at(0).toInt() == QDialog::Rejected, "unpublished confirmation downgraded on exit reentry");
    }
    {
        class HideExit final : public QObject {
        public:
            Prompt* prompt = nullptr;
            bool reentered = false;
            bool eventFilter(QObject*, QEvent* event) override
            {
                if (event->type() == QEvent::Hide) {
                    reentered = true;
                    prompt->retainForExit();
                }
                return false;
            }
        } observer;
        Prompt prompt(fixture(), LockState::Available);
        QSignalSpy finished(&prompt, &QDialog::finished);
        show(prompt);
        observer.prompt = &prompt;
        prompt.installEventFilter(&observer);
        button(prompt, "Confirm")->click();
        check(observer.reentered, "exit reentry occurs during hide before publication");
        check(prompt.decision() == Decision::Retain && finished.count() == 1
            && finished.at(0).at(0).toInt() == QDialog::Rejected, "hide reentry cannot publish confirmation after exit");
    }
}

int main(int argc, char** argv)
{
    // Never use the native Cocoa plugin or any existing App/account window.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("WizRecoveryPromptSynthetic"));
    QCoreApplication::setApplicationName(QStringLiteral("WizRecoveryPromptOffscreen"));
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    check(QGuiApplication::platformName() == QStringLiteral("offscreen"), "offscreen platform enforced");
    QTemporaryDir sandbox;
    check(sandbox.isValid(), "synthetic sandbox valid");
    const QString sentinelPath = sandbox.filePath(QStringLiteral("untouched.md"));
    const QByteArray bytes("synthetic note: \\begin{equation} x=1 \\end{equation}\n");
    QFile sentinel(sentinelPath);
    check(sentinel.open(QIODevice::WriteOnly) && sentinel.write(bytes) == bytes.size(), "synthetic sentinel created");
    sentinel.close();
    defaultsAndKeys();
    eligibilityAndDetails();
    accessibility();
    lifetimeAndExit();
    check(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == bytes, "synthetic note unchanged");
    check(!QFile::exists(sandbox.filePath(QStringLiteral("audit"))), "no synthetic audit created");
    QTextStream(stdout) << "RECOVERY_PROMPT_CHECKS=" << checks << '\n'
        << "RECOVERY_PROMPT_FAILURES=" << failures << Qt::endl;
    return failures ? 1 : 0;
}
