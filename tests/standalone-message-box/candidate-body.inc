// UNACCEPTED diagnostic implementation, compiled only in the independent probe.
// Main App still uses its original WizMessageBox.cpp. Caller lifetime review
// is required before any production integration of parent-close support.
#include <QPointer>
QMessageBox::StandardButton WizMessageBox::messageBox(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, QMessageBox::Icon icon)
{
    const QPointer<WizMessageBox> msg = new WizMessageBox(parent);
    const QPointer<QWidget> owner(parent);
    msg->setIcon(icon);
    msg->setWindowTitle(title);
    msg->setText(text);
    QList<QMessageBox::StandardButton> buttonList;
    buttonsFromStandardButtons(buttons, buttonList);
    for (QMessageBox::StandardButton button : buttonList) {
        QPushButton* item = msg->addButton(button);
        item->setText(buttonTextFromStandardButton(button));
    }
    msg->setDefaultButton(defaultButton);
#ifdef Q_OS_MAC
    QObject::connect(msg.data(), &WizMessageBox::resized, msg.data(), [msg, owner] {
        if (msg && owner) {
            QPoint leftTop = owner->geometry().topLeft();
            leftTop.setX(leftTop.x() + (owner->width() - msg->width()) / 2);
            leftTop.setY(leftTop.y() + (owner->height() - msg->height()) / 2);
            msg->move(leftTop);
        }
    });
#endif
    msg->exec();
    const auto result = msg ? msg->standardButton(msg->clickedButton()) : QMessageBox::NoButton;
    delete msg.data();
    return result;
}
