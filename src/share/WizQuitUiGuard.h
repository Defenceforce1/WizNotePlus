#pragma once
#include <QAction>
#include <QList>
#include <QPointer>
#include <QWidget>

// Block ordinary input across local-save/final-sync preparation, without
// stopping JS snapshots, queued save receipts or the original quit progress.
// A canceled/failed preparation restores explicit states, including widgets
// whose disabled state was only inherited from their parent.
class WizQuitUiGuard final {
public:
    WizQuitUiGuard(QWidget* window, bool& committed) : m_committed(committed) {
        if (!window) return;
        for (auto* action : window->findChildren<QAction*>()) {
            m_actions.append(qMakePair(QPointer<QAction>(action), action->isEnabled()));
            action->setEnabled(false);
        }
    }
    WizQuitUiGuard(const WizQuitUiGuard&) = delete;
    WizQuitUiGuard& operator=(const WizQuitUiGuard&) = delete;
    void freeze(QWidget* widget) {
        if (!widget) return;
        for (const auto& saved : m_widgets) if (saved.first == widget) return;
        m_widgets.append(qMakePair(QPointer<QWidget>(widget), !widget->testAttribute(Qt::WA_ForceDisabled)));
        widget->setEnabled(false);
    }
    ~WizQuitUiGuard() {
        if (m_committed) return;
        for (auto it=m_widgets.crbegin();it!=m_widgets.crend();++it)
            if (it->first) it->first->setEnabled(it->second);
        for (const auto& saved : m_actions) if (saved.first) saved.first->setEnabled(saved.second);
    }
private:
    bool& m_committed;
    QList<QPair<QPointer<QAction>,bool>> m_actions;
    QList<QPair<QPointer<QWidget>,bool>> m_widgets;
};
