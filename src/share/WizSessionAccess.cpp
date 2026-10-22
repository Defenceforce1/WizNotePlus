#include "WizSessionAccess.h"
#include <QCoreApplication>
#include <QGuiApplication>

namespace WizSessionAccess {
Monitor::Monitor(bool native,QObject* parent,int inactiveGraceMs)
    :QObject(parent),m_inactiveGraceMs(qMax(0,inactiveGraceMs))
{
    m_inactiveTimer.setSingleShot(true);
    connect(&m_inactiveTimer,&QTimer::timeout,this,[this]{systemBoundary(false);});
    if(native){
        if(auto* app=qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
            connect(app,&QGuiApplication::applicationStateChanged,this,&Monitor::observeApplicationState);
        m_unsubscribe=subscribeWorkspace(this,[this]{systemBoundary();});
        connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,&Monitor::beginShutdown);
    }
}
Monitor::~Monitor(){if(m_unsubscribe)m_unsubscribe();}
Monitor* Monitor::applicationMonitor()
{
    static QPointer<Monitor> value;
    if(!value&&QCoreApplication::instance())value=new Monitor(true,QCoreApplication::instance());
    return value;
}
void Monitor::observeApplicationState(Qt::ApplicationState state)
{
    if(m_closed)return;
    if(state==Qt::ApplicationActive){
        // If event delivery was delayed while away, do not miss an elapsed
        // grace period just because activation arrived before the timer.
        const QPointer<Monitor> self(this);
        if(m_inactiveElapsed.isValid()&&m_inactiveElapsed.elapsed()>=m_inactiveGraceMs)
            systemBoundary(false);
        if(!self)return;
        m_inactiveTimer.stop();m_inactiveElapsed.invalidate();
        m_retired=false;m_hardRetired=false;return;
    }
    // Brief app switching is not a session lock. Repeated inactive/hidden
    // notifications must not extend the original five-minute deadline.
    if(!m_retired&&!m_inactiveElapsed.isValid()){
        m_inactiveElapsed.start();m_inactiveTimer.start(m_inactiveGraceMs);
    }
}
void Monitor::systemBoundary(bool hard)
{
    if(m_closed||(m_retired&&(!hard||m_hardRetired)))return;
    m_inactiveTimer.stop();m_inactiveElapsed.invalidate();
    m_retired=true;m_hardRetired=m_hardRetired||hard;
    emit accessRetired(hard); // A hard event must escalate a prior soft hide.
}
void Monitor::beginShutdown()
{
    if(m_closed)return;
    m_closed=true;
    m_inactiveTimer.stop();m_inactiveElapsed.invalidate();
    const auto cleanup=std::move(m_unsubscribe);if(cleanup)cleanup();
    emit accessRetired(true);
}
#ifndef Q_OS_MAC
std::function<void()> subscribeWorkspace(QObject*,std::function<void()>){return {};}
#endif
}
