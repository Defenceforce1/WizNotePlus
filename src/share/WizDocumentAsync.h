#ifndef WIZ_DOCUMENT_ASYNC_H
#define WIZ_DOCUMENT_ASYNC_H

#include <QSaveFile>
#include <QString>
#include <QVariant>
#include <QPointer>
#include <memory>
#include <utility>

namespace WizDocumentOwnerAction {
// Create and deliver on the GUI thread. The existing main queue still owns
// scheduling/shutdown; this wrapper only prevents calls into a closed view.
// Active save operations and their completion callbacks are not canceled here.
template<class Owner, class Action>
auto guarded(Owner* owner, Action action)
{
    return [guard = QPointer<Owner>(owner), action = std::move(action)]() mutable {
        if (guard) action(*guard);
    };
}
}

// GUI-thread-only tickets. A navigation or owner destruction invalidates every
// outstanding callback without retaining the view or any note contents.
class WizDocumentCallbackEpoch final
{
public:
    using Ticket = std::weak_ptr<const char>;
    WizDocumentCallbackEpoch() { invalidate(); }
    WizDocumentCallbackEpoch(const WizDocumentCallbackEpoch&) = delete;
    WizDocumentCallbackEpoch& operator=(const WizDocumentCallbackEpoch&) = delete;
    Ticket ticket() const { return m_token; }
    void invalidate() { m_token = std::make_shared<const char>(0); }
    void cancel() { m_token.reset(); }
    static bool current(const Ticket& ticket) { return !ticket.expired(); }
private:
    std::shared_ptr<const char> m_token;
};

// GUI-owned sequence for loader results. Note IDs alone cannot distinguish
// two open views of the same note or repeated requests from one view.
class WizDocumentLoadSequence final
{
public:
    quint64 next() { return ++m_sequence; }
    bool accepts(const QObject* owner, const QPointer<QObject>& requester, quint64 sequence) const {
        return owner && requester == owner && sequence != 0 && sequence == m_sequence;
    }
private:
    quint64 m_sequence = 0;
};

namespace WizDocumentSaveSnapshot {
enum class State { Failed, Unchanged, Changed };
struct Result { State state = State::Failed; QString html; };

// Capture and update the editor baseline in one JS task. In particular, never
// reset the baseline in a later C++ callback: new typing may have arrived then.
inline QString editorScript(bool force, bool nativeDirty)
{
    return QString::fromLatin1(R"JS((function(){try{
        if(!window.WizNativeEditorInitialized || !window.WizEditor ||
            (window.WizNativeEditorSetup && WizNativeEditorSetup.state!=='ready')) return {ok:false};
        if(!document.body || !document.body.isContentEditable || document.body.classList.contains('wiz-readonly')) return {ok:false};
        if(!%1 && !%2 && !WizEditor.isModified()) return {ok:true,changed:false};
        var html=WizEditor.getContentHtml();
        if(typeof html!=='string' || !html.length) return {ok:false};
        WizEditor.setUnModified();
        return {ok:true,changed:true,html:html};
    }catch(error){return {ok:false};}})())JS")
        .arg(force ? "true" : "false", nativeDirty ? "true" : "false");
}

inline QString readerScript(bool nativeDirty = false)
{
    // The original reader returns null when unchanged. Distinguish that valid
    // result from a missing/throwing script or an invalid WebEngine callback.
    return QString::fromLatin1(R"JS((function(){try{
        if(!window.WizNativeEditorInitialized ||
            (window.WizNativeEditorSetup && WizNativeEditorSetup.state!=='ready') || !window.WizReader ||
            typeof WizReader.closeDocument!=='function') return {ok:false};
        if(!document.body || document.body.isContentEditable) return {ok:false};
        var html=WizReader.closeDocument();
        if(html==null || html==='') {
            if(!%1) return {ok:true,changed:false};
            if(!window.WizEditor) return {ok:false};
            html=WizEditor.getContentHtml();
            if(typeof html!=='string' || !html.length) return {ok:false};
        }
        if(typeof html!=='string') return {ok:false};
        return {ok:true,changed:true,html:html};
    }catch(error){return {ok:false};}})())JS").arg(nativeDirty ? "true" : "false");
}

inline Result parse(const QVariant& value)
{
    if (value.userType() != QMetaType::QVariantMap) return {};
    const auto map = value.toMap();
    const auto ok = map.value("ok"), changed = map.value("changed");
    if (ok.userType() != QMetaType::Bool || !ok.toBool()
        || changed.userType() != QMetaType::Bool) return {};
    if (!changed.toBool()) return {State::Unchanged, {}};
    const auto html = map.value("html");
    if (html.userType() != QMetaType::QString || html.toString().isEmpty()) return {};
    return {State::Changed, html.toString()};
}

inline bool writeHtmlFile(const QString& fileName, const QString& html)
{
    if (fileName.isEmpty() || html.isEmpty()) return false;
    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    // Preserve the original extracted HTML's UTF-8 BOM, not a new note format.
    const QByteArray bytes = QByteArray::fromHex("efbbbf") + html.toUtf8();
    if (file.write(bytes) != bytes.size()) { file.cancelWriting(); return false; }
    return file.commit();
}
}
#endif
