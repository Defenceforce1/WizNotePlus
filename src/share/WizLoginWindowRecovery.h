#ifndef WIZLOGINWINDOWRECOVERY_H
#define WIZLOGINWINDOWRECOVERY_H

#include <QApplication>
#include <QApplicationStateChangeEvent>
#include <QDialog>
#include <QEvent>
#include <QTimer>

// Installed only for the macOS login dialog. The main window has its own
// application activation handling, but does not exist during loginDialog.exec().
class WizLoginWindowRecovery final : public QObject
{
public:
    explicit WizLoginWindowRecovery(QDialog* dialog)
        : QObject(dialog), m_dialog(dialog)
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == qApp && m_dialog->isVisible() && m_dialog->isMinimized()
            && (event->type() == QEvent::ApplicationActivate
                || (event->type() == QEvent::ApplicationStateChange
                    && static_cast<QApplicationStateChangeEvent*>(event)->applicationState()
                        == Qt::ApplicationActive))) {
            // Defer until Cocoa has completed application activation. The context
            // cancels the callback when the dialog is destroyed.
            QTimer::singleShot(0, this, [this] {
                QWidget* modal = QApplication::activeModalWidget();
                if (!m_dialog->isVisible() || !m_dialog->isMinimized()
                    || (modal && modal != m_dialog)) {
                    return;
                }
                m_dialog->showNormal();
                m_dialog->raise();
                m_dialog->activateWindow();
            });
        }
        // Do not consume activation: Qt and any nested dialogs still need it.
        return QObject::eventFilter(watched, event);
    }

private:
    QDialog* const m_dialog;
};

#endif // WIZLOGINWINDOWRECOVERY_H
