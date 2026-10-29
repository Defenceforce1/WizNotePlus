#ifndef CWIZLOCALPROGRESSWEBVIEW_H
#define CWIZLOCALPROGRESSWEBVIEW_H

#include <QWidget>

#include "share/WizWebEngineView.h"

class WizWebEngineView;
class QMovie;
class QLabel;

class WizLocalProgressWebView : public QWidget
{
    Q_OBJECT

public:
    enum class WebInitialization { Immediate, OnDemand };
    explicit WizLocalProgressWebView(QWidget *parent = 0);
    WizLocalProgressWebView(QWidget *parent, WebInitialization initialization);
    explicit WizLocalProgressWebView(const WizWebEngineInjectObjectCollection &objects, QWidget *parent = 0);
    ~WizLocalProgressWebView();

    WizWebEngineView* web();

private:
    void init(const WizWebEngineInjectObjectCollection &objects, bool deferred = false);
    void createWeb(const WizWebEngineInjectObjectCollection &objects);

signals:
    void webCreated(WizWebEngineView* web);
    void widgetStatusChanged();
    void willShow();

public slots:

protected:
    void hideEvent(QHideEvent* ev);
    void showEvent(QShowEvent* ev);

private:
    WizWebEngineView* m_web = nullptr;
};

#endif // CWIZLOCALPROGRESSWEBVIEW_H
