#include "WizMessageBox.h"
#include "WizThreads.h"
#include "WizDocumentAsync.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <memory>

struct Stats { int rejected = 0, closed = 0; bool timerStoppedBeforeReject = false; QStringList order; };
class TimeoutOwner : public QDialog {
    Stats* m_stats;
    QTimer m_wizBoxSearchingTimer;
    QDialog* m_animationWaitingDialog;
    void closeWizBoxUdpClient() { ++m_stats->closed; m_stats->order.append("close"); }
public:
    explicit TimeoutOwner(Stats& stats) : m_stats(&stats), m_animationWaitingDialog(new QDialog(this))
    {
        m_wizBoxSearchingTimer.start(10000);
        connect(m_animationWaitingDialog, &QDialog::rejected, this, [this] {
            ++m_stats->rejected; m_stats->order.append("reject");
            m_stats->timerStoppedBeforeReject = !m_wizBoxSearchingTimer.isActive();
        });
    }
    bool timerActive() const { return m_wizBoxSearchingTimer.isActive(); }
    void onWizBoxSearchingTimeOut();
};
#include "actual-timeout-slot.inc"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (QApplication::platformName() != "offscreen") return 2;
    app.setQuitOnLastWindowClosed(false);
    const bool destroy = app.arguments().contains("--destroy-parent");
    qInfo() << "EXTRACTED TIMEOUT SLOT" << LOGIN_SLOT_SHA << "actual helper" << HELPER_SHA
            << "actualMainQueue" << true << "realLoginUdpOrWorker" << false;
    WizQueuedThreadsInit();
    Stats stats;
    auto owner = std::make_unique<TimeoutOwner>(stats);
    const QPointer<TimeoutOwner> original(owner.get());
    bool observed = false, attributes = false, barrier = false, timedOut = false;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        auto* box = qobject_cast<WizMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        poll.stop(); observed = true; stats.order.append("prompt");
        attributes = box->parentWidget() == original && box->icon() == QMessageBox::Information
            && box->standardButtons() == QMessageBox::Ok && !box->defaultButton()
            && box->text() == "There is no server address, please input it.";
        box->button(QMessageBox::Ok)->click();
    });
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        timedOut = true; poll.stop();
        if (auto* box = qobject_cast<QDialog*>(QApplication::activeModalWidget())) box->reject();
    });
    owner->onWizBoxSearchingTimeOut();
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) { ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label; };
    check(!owner->timerActive() && stats.timerStoppedBeforeReject && stats.rejected == 1 && stats.closed == 1,
          "actual slot preserves timer-stop, waiting-dialog rejection and modeled UDP-close order");
    check(!QApplication::activeModalWidget() && stats.order == QStringList{"reject", "close"},
          "original actual-main-queue notification remains deferred, not inline");
    if (destroy) owner.reset();
    WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { barrier = true; stats.order.append("barrier"); });
    poll.start(5); deadline.start(2000);
    QElapsedTimer elapsed;
    elapsed.start();
    while (!barrier && !timedOut && elapsed.elapsed() < 2500) {
        app.processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    poll.stop(); deadline.stop();
    check(barrier && !timedOut, "actual queue reaches the subsequent FIFO barrier");
    check(destroy ? original.isNull() && !observed : original && observed && attributes,
          "actual slot notifies live owner or skips already-destroyed synthetic owner");
    check(stats.order == (destroy ? QStringList{"reject", "close", "barrier"}
                                : QStringList{"reject", "close", "prompt", "barrier"}),
          "actual queue preserves action ordering without replaying stale notification");
    check(!QApplication::activeModalWidget(), "no modal remains after actual queue delivery");
    WizQueuedThreadsShutdown();
    qInfo() << "Extracted timeout-slot checks:" << checks << "failures:" << failures
            << "actualLoginConstructorUdpWorkerOrRejectReentrancy" << false;
    return failures ? 1 : 0;
}
