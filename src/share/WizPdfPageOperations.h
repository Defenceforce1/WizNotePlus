#ifndef WIZ_PDF_PAGE_OPERATIONS_H
#define WIZ_PDF_PAGE_OPERATIONS_H

#include "WizPdfSvgLinks.h"
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <deque>
#include <functional>
#include <memory>

// GUI-thread-only FIFO for the two operations that must not overlap: note
// serialization and native PDF printing. Canceling an output reservation does
// NOT release this gate while the browser still owns temporary print DOM.
class WizPdfPageOperations final : public QObject
{
public:
    using Valid = std::function<bool()>;
    using Done = std::function<void()>;
    ~WizPdfPageOperations() override {
        m_closing=true;
        if (m_cancelActive) m_cancelActive();
        auto pending=std::move(m_pending);
        for (const auto& job:pending) job.cancel();
    }

    void snapshot(QWebEnginePage* page, const Valid& valid, const QString& source,
                  const std::function<void(const QVariant&)>& callback) {
        const auto reply=once<QVariant>(callback);
        const QPointer<QWebEnginePage> guard(page);
        enqueue({guard,[guard,valid]{return guard && !guard->property(blockedKey()).toBool() && valid();},
            [guard,source,reply](const Done& done){
                guard->runJavaScript(source,[reply,done](const QVariant& value){reply(value);done();});
            },[reply]{reply({});}});
    }

    void print(QWebEnginePage* page, const Valid& valid, const QPageLayout& layout,
               const std::function<void(const QByteArray&)>& callback, const Valid& sameDocument=[]{return true;}) {
        const auto reply=once<QByteArray>(callback);
        const QPointer<QWebEnginePage> guard(page);
        enqueue({guard,[guard,valid]{return guard && !guard->property(blockedKey()).toBool() && valid();},[guard,valid,sameDocument,layout,reply](const Done& done){
            // Only a serialized native print arms the event helper. Ordinary
            // page print events must never mutate DOM outside this FIFO.
            // A canceled navigation can miss a registered DocumentReady user
            // script in Qt. Ensure this document's helper at the native entry
            // point too; never assume a previous document installed it.
            guard->runJavaScript(WizPdfSvgLinks::source()+";globalThis.__wizPdfSvgLinks?.arm()===true",QWebEngineScript::ApplicationWorld,
                [guard,valid,sameDocument,layout,reply,done](const QVariant& armed){
                if (!guard) {reply({});done();return;}
                const auto restore=[guard,sameDocument,reply,done](const QByteArray& bytes){
                    if (!guard) {reply({});done();return;}
                    guard->runJavaScript("globalThis.__wizPdfSvgLinks?.finish()===true",QWebEngineScript::ApplicationWorld,
                        [guard,sameDocument,reply,done,bytes](const QVariant& clean){
                        if (guard && sameDocument() && !clean.toBool()) {
                            // Fail closed: do not let a subsequent save persist
                            // unverified print DOM. A fresh load resets this.
                            guard->setProperty(blockedKey(),true);
                            if (!guard->property("wizPdfSnapshotResetInstalled").toBool()) {
                                guard->setProperty("wizPdfSnapshotResetInstalled",true);
                                QObject::connect(guard.data(),&QWebEnginePage::loadFinished,guard.data(),
                                    [guard](bool ok){if(guard && ok)guard->setProperty(blockedKey(),false);});
                            }
                        }
                        reply(clean.toBool()?bytes:QByteArray{});done();
                    });
                };
                if (!armed.toBool() || !valid()) {restore({});return;}
                guard->printToPdf(restore,layout);
            });
        },[reply]{reply({});}});
    }

private:
    struct Job {QPointer<QWebEnginePage> page;Valid valid;std::function<void(const Done&)> start;Done cancel;};
    std::deque<Job> m_pending;
    Done m_cancelActive;
    QMetaObject::Connection m_destroyed;
    bool m_active=false,m_closing=false;
    static const char* blockedKey() {return "wizPdfUnverifiedPrintDom";}

    template<typename T> static std::function<void(const T&)> once(const std::function<void(const T&)>& callback) {
        auto delivered=std::make_shared<bool>(false);
        return [delivered,callback](const T& value){if(!*delivered){*delivered=true;callback(value);}};
    }
    void enqueue(Job job) {
        if (m_closing) {job.cancel();return;}
        m_pending.push_back(std::move(job));drain();
    }
    void drain() {
        if (m_active || m_closing) return;
        const QPointer<WizPdfPageOperations> self(this);
        while (!m_pending.empty()) {
            auto job=std::move(m_pending.front());m_pending.pop_front();
            if (!job.page || !job.valid()) {
                job.cancel();
                // A failure callback may enqueue (and start) another operation.
                // Do not let this outer drain start a concurrent job afterward.
                if(!self || m_active || m_closing)return;
                continue;
            }
            m_active=true;m_cancelActive=job.cancel;
            const auto done=once<int>([self](const int&){
                if (!self || self->m_closing) return;
                QObject::disconnect(self->m_destroyed);
                self->m_cancelActive={};self->m_active=false;
                QTimer::singleShot(0,self.data(),[self]{if(self)self->drain();});
            });
            const Done finish=[done]{done(0);};
            m_destroyed=QObject::connect(job.page.data(),&QObject::destroyed,this,[cancel=job.cancel,finish]{cancel();finish();});
            job.start(finish);return;
        }
    }
};
#endif
