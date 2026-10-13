#include "share/WizLoginWindowRecovery.h"
#include <QDebug>
#include <QPointer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    int failures = 0;
    auto check = [&](bool result, const char* label) {
        qInfo().noquote() << (result ? "PASS" : "FAIL") << label;
        failures += !result;
    };
    auto activate = [&] {
        QEvent activation(QEvent::ApplicationActivate);
        QCoreApplication::sendEvent(&app, &activation);
        QCoreApplication::processEvents();
    };

    QDialog login;
    new WizLoginWindowRecovery(&login);
    login.setWindowModality(Qt::ApplicationModal);
    login.show();
    login.showMinimized();
    check(login.isMinimized(), "minimized login test precondition");
    activate();
    check(login.isVisible() && !login.isMinimized(), "activation restores minimized login");
    login.showMinimized();
    QApplicationStateChangeEvent inactive(Qt::ApplicationInactive);
    QCoreApplication::sendEvent(&app, &inactive);
    QCoreApplication::processEvents();
    check(login.isMinimized(), "inactive application state does not restore login");
    QApplicationStateChangeEvent active(Qt::ApplicationActive);
    QCoreApplication::sendEvent(&app, &active);
    QCoreApplication::processEvents();
    check(!login.isMinimized(), "modern application state event restores login");
    const QSize size = login.size();
    activate();
    check(login.isVisible() && login.size() == size && !login.isMaximized(),
          "normal login is not resized or maximized");

    login.showMinimized();
    QDialog child(&login);
    child.setWindowModality(Qt::ApplicationModal);
    child.show();
    check(QApplication::activeModalWidget() == &child, "nested modal test precondition");
    activate();
    check(login.isMinimized(), "nested modal dialog is not interrupted");
    child.reject();
    activate();
    check(!login.isMinimized(), "login restores after nested modal closes");

    login.showMinimized();
    QEvent activation(QEvent::ApplicationActivate);
    QCoreApplication::sendEvent(&app, &activation);
    login.reject();
    QCoreApplication::processEvents();
    check(!login.isVisible(), "queued activation does not reopen rejected login");
    activate();
    check(!login.isVisible(), "closed login stays closed");

    auto* disposable = new QDialog;
    QPointer<QDialog> destroyed(disposable);
    new WizLoginWindowRecovery(disposable);
    disposable->showMinimized();
    QCoreApplication::sendEvent(&app, &activation);
    delete disposable;
    QCoreApplication::processEvents();
    check(destroyed.isNull(), "destroyed dialog cancels deferred restoration without crashing");

    QTimer::singleShot(0, &login, [&] {
        login.showMinimized();
        activate();
        check(!login.isMinimized(), "restoration works inside QDialog::exec event loop");
        login.accept();
    });
    check(login.exec() == QDialog::Accepted, "modal login can finish normally");
    return failures ? 1 : 0;
}
