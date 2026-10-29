#include "WizLocalProgressWebView.h"
#include <QMovie>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>
#include "share/WizWebEngineView.h"

WizLocalProgressWebView::WizLocalProgressWebView(const WizWebEngineInjectObjectCollection &objects, QWidget *parent)
{
    init(objects);
}


WizLocalProgressWebView::WizLocalProgressWebView(QWidget *parent) : QWidget(parent)
{
    init({});
}


WizLocalProgressWebView::WizLocalProgressWebView(QWidget *parent, WebInitialization initialization)
    : QWidget(parent)
{
    init({}, initialization == WebInitialization::OnDemand);
}


void WizLocalProgressWebView::init(const WizWebEngineInjectObjectCollection &objects, bool deferred)
{
    setContentsMargins(0, 0, 0, 0);

    QVBoxLayout* layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setLayout(layout);

    if (!deferred) createWeb(objects);
}


void WizLocalProgressWebView::createWeb(const WizWebEngineInjectObjectCollection &objects)
{
    if (m_web) return;
    m_web = new WizWebEngineView(objects, this);
    m_web->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    layout()->addWidget(m_web);
    emit webCreated(m_web);
}


WizLocalProgressWebView::~WizLocalProgressWebView()
{

}


WizWebEngineView* WizLocalProgressWebView::web()
{
    if (!m_web) createWeb({});
    return m_web;
}


void WizLocalProgressWebView::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);

    emit widgetStatusChanged();
}

void WizLocalProgressWebView::showEvent(QShowEvent* ev)
{
    web(); // A deferred panel is initialized only when requested or shown.
    emit willShow();

    QWidget::showEvent(ev);

    emit widgetStatusChanged();
}

