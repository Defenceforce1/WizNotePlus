#pragma once

#include "WizDocumentAsync.h"
#include <QFont>
#include <QWebEnginePage>
#include <cmath>
#include <functional>
#include <optional>

namespace WizEditorFontQuery {
// Preserve the existing browser commands and their order, but read them in a
// single JS task so a selection change cannot interleave six IPC round trips.
inline QString script()
{
    return QStringLiteral(R"JS((function(){try{
        function value(name){var v=document.queryCommandValue(name);return v==null?'':String(v);}
        return {ok:true,family:value('fontFamily'),bold:document.queryCommandState('bold'),
            italic:document.queryCommandState('italic'),underline:document.queryCommandState('underline'),
            size:value('fontSize'),strike:document.queryCommandState('strikethrough')};
    }catch(error){return {ok:false};}})())JS");
}

inline bool valid(const QVariantMap& state)
{
    if (state.value("ok").userType() != QMetaType::Bool || !state.value("ok").toBool()
        || state.value("family").userType() != QMetaType::QString
        || state.value("size").userType() != QMetaType::QString) return false;
    for (const char* name : {"bold", "italic", "underline", "strike"}) {
        const QVariant flag = state.value(QLatin1String(name));
        if (flag.userType() != QMetaType::Bool && flag.userType() != QMetaType::Int
            && flag.userType() != QMetaType::Double) return false;
        bool ok = false;
        const double number = flag.toDouble(&ok);
        if (!ok || !std::isfinite(number)) return false;
    }
    return true;
}

inline std::optional<QFont> font(const QVariantMap& state, int defaultSize)
{
    if (!valid(state)) return std::nullopt;
    QFont result;
    if (!state.value("family").toString().isEmpty()) result.setFamily(state.value("family").toString());
    result.setBold(state.value("bold").toInt() == 1);
    result.setItalic(state.value("italic").toInt() == 1);
    result.setUnderline(state.value("underline").toInt() == 1);
    result.setStrikeOut(state.value("strike").toInt() == 1);
    QString size = state.value("size").toString();
    size.remove("pt");
    const int pointSize = size.toInt();
    const int fallback = defaultSize > 0 ? defaultSize : qMax(1, result.pointSize());
    result.setPointSize(pointSize > 0 ? pointSize : fallback);
    return result;
}

// GUI-owned transient query only. Never use this guard for persistence acks.
inline void query(QObject* owner, QWebEnginePage* page, WizDocumentCallbackEpoch::Ticket document,
                  std::function<bool()> current, std::function<void(const QVariantMap&)> completed)
{
    if (!owner || !page || !WizDocumentCallbackEpoch::current(document) || !current || !current()) return;
    const QPointer<QObject> context(owner);
    const QPointer<QWebEnginePage> target(page);
    page->runJavaScript(script(), [context, target, document, current, completed](const QVariant& value) {
        if (!context || !target || !WizDocumentCallbackEpoch::current(document) || !current()) return;
        if (value.userType() != QMetaType::QVariantMap) return;
        const auto state = value.toMap();
        if (!valid(state) || !context || !target) return;
        completed(state);
        // completed may close the toolbar or editor; no owner access follows.
    });
}
}
