#include "WizSessionAccess.h"
#include <QCoreApplication>
#include <QGuiApplication>

namespace WizSessionAccess {
Monitor::Monitor(bool native,QObject* parent):QObject(parent)
{
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
    if(state==Qt::ApplicationActive){m_retired=false;m_hardRetired=false;return;}
    systemBoundary(false);
}
void Monitor::systemBoundary(bool hard)
{
    if(m_closed||(m_retired&&(!hard||m_hardRetired)))return;
    m_retired=true;m_hardRetired=m_hardRetired||hard;
    emit accessRetired(hard); // A hard event must escalate a prior soft hide.
}
void Monitor::beginShutdown()
{
    if(m_closed)return;
    m_closed=true;
    const auto cleanup=std::move(m_unsubscribe);if(cleanup)cleanup();
    emit accessRetired(true);
}
#ifndef Q_OS_MAC
std::function<void()> subscribeWorkspace(QObject*,std::function<void()>){return {};}
#endif
}
