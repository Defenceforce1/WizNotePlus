#include "../../src/share/WizMacWebInput.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cstdio>

int main()
{
    int checks = 0;
    auto check = [&](bool value) { ++checks; if (!value) std::abort(); };
    for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        QKeyEvent caps(type, Qt::Key_CapsLock, Qt::NoModifier);
        check(wizFilterMacWebCapsNotification(&caps, true));
        check(!wizFilterMacWebCapsNotification(&caps, false));
        for (auto modifier : {Qt::ControlModifier, Qt::MetaModifier, Qt::AltModifier}) {
            QKeyEvent modified(type, Qt::Key_CapsLock, modifier);
            check(!wizFilterMacWebCapsNotification(&modified, true));
        }
        QKeyEvent shifted(type, Qt::Key_CapsLock, Qt::ShiftModifier);
        check(wizFilterMacWebCapsNotification(&shifted, true));
        QKeyEvent text(type, Qt::Key_CapsLock, Qt::NoModifier, "x");
        check(!wizFilterMacWebCapsNotification(&text, true));
        for (auto key : {Qt::Key_A, Qt::Key_Return, Qt::Key_Backspace, Qt::Key_Left}) {
            QKeyEvent ordinary(type, key, Qt::NoModifier);
            check(!wizFilterMacWebCapsNotification(&ordinary, true));
        }
        QKeyEvent command(type, Qt::Key_B, Qt::ControlModifier);
        check(!wizFilterMacWebCapsNotification(&command, true));
    }
    QEvent ime(QEvent::InputMethod), focus(QEvent::FocusOut);
    check(!wizFilterMacWebCapsNotification(&ime, true));
    check(!wizFilterMacWebCapsNotification(&focus, true));
    const auto result = QJsonDocument(QJsonObject{{"checks", checks}, {"passed", true},
        {"physicalInputAcceptance", false}}).toJson(QJsonDocument::Compact);
    std::puts(result.constData());
}
