#pragma once

#include "WizDocumentAsync.h"
#include <QWebEnginePage>
#include <functional>

namespace WizDocumentUiQuery {
// Trusted internal expression only, never user code. A missing mail sender is
// a valid empty result; a destroyed page or a JS exception is not that result.
inline QString stringScript(const QString& expression, bool allowMissing = false)
{
    return QStringLiteral(R"JS((function(){try{
        var value=%1
        ;
        if(value==null&&%2)value='';
        if(typeof value!=='string')return {ok:false};
        return {ok:true,value:value};
    }catch(error){return {ok:false};}})())JS")
        .arg(expression.trimmed().isEmpty() ? QStringLiteral("undefined") : expression,
             allowMissing ? QStringLiteral("true") : QStringLiteral("false"));
}

// Transient UI reads only. No persistence acknowledgment is dropped here.
inline void string(QObject* owner, QWebEnginePage* page,
                   WizDocumentCallbackEpoch::Ticket document, const QString& expression,
                   std::function<void(const QString&)> completed, bool allowMissing = false)
{
    if (!owner || !page || !completed || !WizDocumentCallbackEpoch::current(document)) return;
    const QPointer<QObject> context(owner);
    const QPointer<QWebEnginePage> target(page);
    page->runJavaScript(stringScript(expression, allowMissing),
        [context, target, document, completed](const QVariant& result) {
        if (!context || !target || !WizDocumentCallbackEpoch::current(document)
            || result.userType() != QMetaType::QVariantMap) return;
        const auto state = result.toMap();
        if (state.value("ok").userType() != QMetaType::Bool || !state.value("ok").toBool()
            || state.value("value").userType() != QMetaType::QString) return;
        completed(state.value("value").toString());
        // The receiver may close itself; no context access follows.
    });
}
}
