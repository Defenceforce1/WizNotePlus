#include "WizMessageBox.h"
#include <QApplication>
#include <QDialog>
#include <QAbstractButton>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QDebug>
#include <memory>

class EmailOwner : public QDialog {
    struct Ui { QLabel* labelInfo; };
    Ui* ui;
public:
    EmailOwner() : ui(new Ui{new QLabel(this)}) { ui->labelInfo->setText("UNCHANGED"); }
    ~EmailOwner() override { delete ui; }
    QLabel* label() const { return ui->labelInfo; }
    void on_networkError(const QString&);
};
#include "actual-email-slot.inc"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (QApplication::platformName() != "offscreen") return 2;
    app.setQuitOnLastWindowClosed(false);
    const bool destroy = app.arguments().contains("--destroy-parent");
    qInfo() << "EXTRACTED EMAIL SLOT" << EMAIL_SLOT_SHA << "helper" << HELPER_SHA
            << "prototype" << HELPER_PROTOTYPE_SHA << "mode" << EMAIL_PROBE_MODE;
    auto owner = std::make_unique<EmailOwner>();
    const QPointer<EmailOwner> original(owner.get());
    const QPointer<QLabel> label(owner->label());
    const QString message = QString::fromUtf8("Synthetic offline error / 合成网络错误");
    bool observed = false, attributes = false, timedOut = false, returned = false;
    QPointer<WizMessageBox> box;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        auto* actual = qobject_cast<WizMessageBox*>(QApplication::activeModalWidget());
        if (!actual) return;
        poll.stop(); box = actual; observed = true;
        attributes = actual->parentWidget() == original && actual->icon() == QMessageBox::Information
            && actual->standardButtons() == QMessageBox::Ok && !actual->defaultButton()
            && actual->text() == message && actual->isModal();
        if (destroy) owner.reset();
        else actual->button(QMessageBox::Ok)->click();
    });
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        timedOut = true; poll.stop();
        if (auto* actual = qobject_cast<QDialog*>(QApplication::activeModalWidget())) actual->reject();
    });
    poll.start(5); deadline.start(2000);
    owner->on_networkError(message);
    returned = true;
    poll.stop(); deadline.stop();
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* text) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << text;
    };
    check(observed && attributes && !timedOut, "actual slot retains original information text/icon/buttons/default/modality");
    check(returned && (destroy ? original.isNull() && label.isNull() : label && label->text() == message),
          "actual slot updates live label or returns safely after synthetic owner destruction");
    check(box.isNull() && !QApplication::activeModalWidget(), "actual helper leaves no modal after slot returns");
    qInfo() << "Extracted email-slot checks:" << checks << "failures:" << failures
            << "actualEmailDialogOrNetworkWorkflow" << false;
    return failures ? 1 : 0;
}
