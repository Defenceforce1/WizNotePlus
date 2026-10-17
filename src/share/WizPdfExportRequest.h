#ifndef WIZ_PDF_EXPORT_REQUEST_H
#define WIZ_PDF_EXPORT_REQUEST_H

#include "WizDocumentAsync.h"
#include "WizPdfExport.h"
#include <QWebEnginePage>

// GUI-thread-only weak reservation, including the native save dialog. A print
// callback never retains the editor, page, request or original note context.
class WizPdfExportRequest final
{
    struct State {
        QPointer<QObject> owner;
        QPointer<QWebEnginePage> page;
        WizDocumentCallbackEpoch::Ticket document;
        bool finished = false;
    };
    std::shared_ptr<State> m_state;
    static bool context(const std::shared_ptr<State>& state) {
        return state && state->owner && state->page
            && WizDocumentCallbackEpoch::current(state->document);
    }
public:
    using Ticket = std::weak_ptr<State>;
    enum class Result { Canceled, Saved, Failed };
    WizPdfExportRequest() = default;
    WizPdfExportRequest(const WizPdfExportRequest&) = delete;
    WizPdfExportRequest& operator=(const WizPdfExportRequest&) = delete;
    Ticket begin(QObject* owner,QWebEnginePage* page,const WizDocumentCallbackEpoch::Ticket& document) {
        if(context(m_state)&&!m_state->finished)return {};
        m_state.reset();
        if(!owner||!page||!WizDocumentCallbackEpoch::current(document))return {};
        m_state=std::make_shared<State>(State{owner,page,document});
        return m_state;
    }
    void cancel() { m_state.reset(); }
    static bool ownsContext(const Ticket& ticket) { return context(ticket.lock()); }
    static bool current(const Ticket& ticket) {
        const auto state=ticket.lock();return context(state)&&!state->finished;
    }
    static void cancel(const Ticket& ticket) {
        if(const auto state=ticket.lock())state->finished=true;
    }
    static Result complete(const Ticket& ticket,const QString& destination,const QByteArray& bytes,QString& error) {
        error.clear();
        const auto state=ticket.lock();
        if(!context(state)||state->finished)return Result::Canceled;
        state->finished=true; // One completion, including a failed atomic write.
        return WizPdfExport::writeFile(destination,bytes,error)?Result::Saved:Result::Failed;
    }
};
#endif
