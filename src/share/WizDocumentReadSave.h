#ifndef WIZ_DOCUMENT_READ_SAVE_H
#define WIZ_DOCUMENT_READ_SAVE_H
#include "WizDocumentReadPermit.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include <QVariant>
#include <functional>
#include <memory>

namespace WizDocumentReadSave {
// GUI-only read continuation. Original snapshots/saves are never canceled.
// The fence belongs to this saver instance, not to a globally reusable ID.
inline void start(QObject* owner, WizDocumentSaverThread* source,
                  const WIZDOCUMENTDATA& doc, const WizDocumentReadPermit& read,
                  std::function<void(std::function<void(const QVariant&)>)> snapshot,
                  std::function<void(bool)> completed)
{
    if (!owner || !source || !read.current() || !snapshot) {
        if (completed) completed(false);
        return;
    }
    struct State { quint64 fence=0; bool finished=false; };
    const auto state=std::make_shared<State>();
    const QPointer<QObject> request(new QObject(owner));
    const QPointer<WizDocumentSaverThread> saver(source);
    const auto finish=[request,state,completed](bool ok) {
        if (!request || state->finished) return;
        state->finished=true;
        request->deleteLater();
        if (completed) completed(ok);
    };
    QObject::connect(source,&WizDocumentSaverThread::saveFenceReached,request.data(),
        [saver,read,state,finish](quint64 id,bool ok) {
        if (!state->fence || id!=state->fence) return;
        finish(saver && read.current() && ok);
    },Qt::QueuedConnection);
    QObject::connect(source,&QObject::destroyed,request.data(),[finish]{finish(false);});
    snapshot([request,saver,read,doc,state,finish](const QVariant& saved) {
        if (!request || state->finished || state->fence) return;
        if (!saver || !read.current() || saved.userType()!=QMetaType::Bool || !saved.toBool()) {
            finish(false);return;
        }
        state->fence=saver->saveFence(doc);
        if (!state->fence) finish(false);
    });
}
}
#endif
