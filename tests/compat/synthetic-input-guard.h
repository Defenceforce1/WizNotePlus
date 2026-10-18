#pragma once
#include <QCoreApplication>
#include <QEvent>
#include <QObject>

// Only instantiate inside an isolated test process that edits through its
// scripted fixture. No key text, input-method content or clipboard is logged.
class ScopedSyntheticInputGuard final : public QObject {
public:
    explicit ScopedSyntheticInputGuard(QCoreApplication& app) : m_app(app) { m_app.installEventFilter(this); }
    ~ScopedSyntheticInputGuard() override { m_app.removeEventFilter(this); }
    int blockedEvents() const { return m_blocked; }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        bool block = false;
        switch (event->type()) {
        case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::Shortcut:
        case QEvent::ShortcutOverride: case QEvent::InputMethod:
        case QEvent::Drop: case QEvent::DragEnter: case QEvent::DragMove:
            block = true; break;
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: case QEvent::Wheel:
            block = event->spontaneous(); break;
        default: break;
        }
        if (block) { ++m_blocked; event->accept(); }
        return block;
    }
private:
    QCoreApplication& m_app;
    int m_blocked = 0;
};
