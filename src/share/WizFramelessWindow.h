#ifndef WIZFRAMELESSWINDOW_H
#define WIZFRAMELESSWINDOW_H

#include <QWidget>
#include <QPainter>
#include <QPaintEngine>
#include <QMouseEvent>
#include <QDebug>
#include <QBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QApplication>

#include "WizWindowTitleBar.h"
#include "WizShadowEffect.h"

#include "framelesshelper/src/core/framelesshelper.h"
#include "framelesshelper/src/core/utilities.h"

template <class Base>
class WizFramelessWindow : public Base
{
public:
    explicit WizFramelessWindow(QWidget *parent, bool canResize)
        : Base(parent)
        , m_clientWidget(NULL)
        , m_clientLayout(NULL)
        , m_canResize(canResize)
        , m_helper(new __flh_ns::FramelessHelper)
    {
        this->setAttribute(Qt::WA_DontCreateNativeAncestors);

        QLayout* windowLayout = new QBoxLayout(QBoxLayout::TopToBottom);
        this->setLayout(windowLayout);
        windowLayout->setContentsMargins(0, 0, 0, 0);
        windowLayout->setSpacing(0);

        m_titleBar = new WizWindowTitleBar(this, this, canResize);

        m_clientWidget = new QWidget(this);
        windowLayout->addWidget(m_clientWidget);
        m_clientLayout = new QBoxLayout(QBoxLayout::TopToBottom);
        m_clientWidget->setLayout(m_clientLayout);
        m_clientLayout->setSpacing(0);
        m_clientLayout->setContentsMargins(0, 0, 0, 0);
    
        m_clientLayout->addWidget(m_titleBar);
    }

public:
    QWidget* rootWidget() const { return m_clientWidget; }
    QWidget *clientWidget() const { return m_clientWidget; }
    QLayout* clientLayout() const { return m_clientLayout; }
    WizWindowTitleBar* titleBar() const { return m_titleBar; }
    void setTitleText(QString title) { m_titleBar->setText(title); }

    void setHitTestVisible(QObject *obj)
    {
        m_helper->setHitTestVisible(obj);
    }

    void setResizeBorderThickness(int thickness) {
        m_helper->setResizeBorderThickness(thickness);
    }

    void setTitleBarHeight(int height)
    {
        m_helper->setTitleBarHeight(height);
    }

    void setFrameBorderWidth(int width)
    {
        m_frameBorderWidth = width;
    }

#ifdef Q_OS_MAC
    void setCloseBtnEnabled(bool enable)
    {
        m_isCloseBtnEnabled = enable;
        refreshMacWindowButtons();
    }

    void setMinBtnEnabled(bool enable)
    {
        m_isMinBtnEnabled = enable;
        refreshMacWindowButtons();
    }

    void setZoomBtnEnabled(bool enable)
    {
        m_isZoomBtnEnabled = enable;
        refreshMacWindowButtons();
    }

private:
    void refreshMacWindowButtons()
    {
        if (!m_flhInited || !this->windowHandle()) return;
        // Do not bypass Qt/AppKit's modal blocking. Reapply policy once unblocked.
        QWidget* modal = QApplication::activeModalWidget();
        const bool enabled = this->isEnabled() && (!modal || modal == this);
        const auto win = this->windowHandle();
        __flh_ns::Utilities::setCloseBtnEnabled(win, enabled && m_isCloseBtnEnabled);
        __flh_ns::Utilities::setMinBtnEnabled(win, enabled && m_isMinBtnEnabled);
        __flh_ns::Utilities::setZoomBtnEnabled(win, enabled && m_isZoomBtnEnabled);
    }
    bool m_isCloseBtnEnabled = true;
    bool m_isMinBtnEnabled = true;
    bool m_isZoomBtnEnabled = true;
#endif // Q_OS_MAC

private:
    QWidget* m_clientWidget;
    QLayout* m_clientLayout;
    WizWindowTitleBar* m_titleBar;
    __flh_ns::FramelessHelper *m_helper;
    bool m_canResize;
    bool m_flhInited = false;
    int m_frameBorderWidth = 0;

protected:
#ifdef Q_OS_MAC
    bool event(QEvent* event) override
    {
        const bool result = Base::event(event);
        if (event->type() == QEvent::WindowUnblocked || event->type() == QEvent::WindowBlocked)
            QTimer::singleShot(0, this, [this] { refreshMacWindowButtons(); });
        return result;
    }
#endif
    void changeEvent(QEvent * event) override
    {
#ifndef Q_OS_MAC
        bool shouldUpdate = false;
        if (event->type() == QEvent::WindowStateChange) {
            if (this->isMaximized() || this->isFullScreen()) {
                this->setContentsMargins(0, 0, 0, 0);
            } else if (!this->isMinimized()) {
                this->setContentsMargins(
                    m_frameBorderWidth, m_frameBorderWidth,
                    m_frameBorderWidth, m_frameBorderWidth
                );
            }
            m_titleBar->windowStateChanged();
            shouldUpdate = true;
        } else if (event->type() == QEvent::ActivationChange) {
            shouldUpdate = true;
        }
        if (shouldUpdate) {
            this->update();
        }
#endif

#if defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (event->type() == QEvent::WindowStateChange && m_flhInited) {
            auto cev = static_cast<QWindowStateChangeEvent*>(event);
            if (cev->oldState() & Qt::WindowFullScreen && !this->isFullScreen()) {
                // Exit fullscreen
                m_titleBar->layout()->setContentsMargins(70, 0, 0, 0);
            } else if (!(cev->oldState() & Qt::WindowFullScreen) && this->isFullScreen()) {
                // Enter fullscreen
                m_titleBar->layout()->setContentsMargins(0, 0, 0, 0);
            }
        }
#endif

        Base::changeEvent(event);
#ifdef Q_OS_MAC
        if (event->type() == QEvent::EnabledChange || event->type() == QEvent::ActivationChange
            || event->type() == QEvent::WindowStateChange)
            QTimer::singleShot(0, this, [this] { refreshMacWindowButtons(); });
#endif
    }

    virtual void layoutTitleBar()
    {
        m_titleBar->layoutTitleBar();
    }

    void showEvent(QShowEvent *event) override
    {
        Base::showEvent(event);
        if (!m_flhInited) {
            const auto win = this->windowHandle();
            if (win) {
#if defined(Q_OS_MAC) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                // Keep Qt's Cocoa delegate, event dispatch and standard title bar.
                // The legacy helper replaces the native delegate and swizzles its
                // event methods; relocating title-bar controls is unsafe on Qt 6.
                // App content/toolbar remains below the system-managed title bar.
                if (m_titleBar->layout())
                    m_titleBar->layout()->setContentsMargins(0, 0, 0, 0);
#else
                m_helper->setWindow(win);
                m_helper->setTitleBarHeight(m_titleBar->height());
                m_helper->setResizable(m_canResize);
                m_helper->setResizeBorderThickness(2);
                m_helper->setHitTestVisible(m_titleBar->closeButton());
                m_helper->setHitTestVisible(m_titleBar->maxButton());
                m_helper->setHitTestVisible(m_titleBar->minButton());
                m_helper->install();
#ifndef Q_OS_MAC
                this->setContentsMargins(
                    m_frameBorderWidth, m_frameBorderWidth,
                    m_frameBorderWidth, m_frameBorderWidth
                );
#else
                __flh_ns::Utilities::setStandardWindowButtonsVisibility(win, true);
                __flh_ns::Utilities::setCloseBtnEnabled(win, m_isCloseBtnEnabled);
                __flh_ns::Utilities::setMinBtnEnabled(win, m_isMinBtnEnabled);
                __flh_ns::Utilities::setZoomBtnEnabled(win, m_isZoomBtnEnabled);
                QSize btnGroupSize = __flh_ns::Utilities::standardWindowButtonsSize(win);
                __flh_ns::Utilities::setStandardWindowButtonsPosition(win,
                    QPoint(12, (m_titleBar->height() - btnGroupSize.height())/2)
                );
                m_titleBar->layout()->setContentsMargins(70, 0, 0, 0);
#endif // Q_OS_MAC
#endif // Qt 6 macOS uses the native frame
                m_flhInited = true;
            }
        }
#ifdef Q_OS_MAC
        QTimer::singleShot(0, this, [this] { refreshMacWindowButtons(); });
#endif
    }

#ifdef Q_OS_WIN
	bool nativeEvent(const QByteArray &eventType, void *message, long *result) override
    {
        if (!m_helper)
            return Base::nativeEvent(eventType, message, result);

        if (m_helper->handleNativeEvent(this->windowHandle(), eventType, message, result))
            return true;
        else
            return Base::nativeEvent(eventType, message, result);
    }
#endif // Q_OS_WIN
};

#endif // WIZFRAMELESSWINDOW_H
