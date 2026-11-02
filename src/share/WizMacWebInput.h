#pragma once

#include <QtCore/QEvent>
#include <QtGui/QKeyEvent>

// Limit the workaround to nontext Caps notifications delivered to the
// focused web control. Native input-source switching remains with macOS.
inline bool wizFilterMacWebCapsNotification(const QEvent* event, bool focusedWebReceiver)
{
#ifdef Q_OS_MAC
    if (!focusedWebReceiver || (event->type() != QEvent::KeyPress
            && event->type() != QEvent::KeyRelease))
        return false;
    const auto* key = static_cast<const QKeyEvent*>(event);
    return key->key() == Qt::Key_CapsLock && key->text().isEmpty()
        && !(key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier));
#else
    Q_UNUSED(event);
    Q_UNUSED(focusedWebReceiver);
    return false;
#endif
}
