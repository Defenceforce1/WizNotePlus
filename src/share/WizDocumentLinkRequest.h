#ifndef WIZ_DOCUMENT_LINK_REQUEST_H
#define WIZ_DOCUMENT_LINK_REQUEST_H

#include "WizDocumentAsync.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QWebEnginePage>
#include <functional>

// GUI-thread-only request. No callback retains an owner, page, or this helper.
// Superseding/canceling a query expires its weak state even before JS returns.
class WizDocumentLinkRequest final
{
    struct State {
        QPointer<QObject> owner;
        QPointer<QWebEnginePage> page;
        WizDocumentCallbackEpoch::Ticket document;
        bool ready = false;
    };
    std::shared_ptr<State> m_state;
    static bool valid(const std::shared_ptr<State>& state) {
        return state && state->owner && state->page
            && WizDocumentCallbackEpoch::current(state->document);
    }
public:
    WizDocumentLinkRequest() = default;
    WizDocumentLinkRequest(const WizDocumentLinkRequest&) = delete;
    WizDocumentLinkRequest& operator=(const WizDocumentLinkRequest&) = delete;
    void cancel() { m_state.reset(); }
    bool current(QWebEnginePage* page) const {
        return page && valid(m_state) && m_state->ready && m_state->page == page;
    }
    void query(QObject* owner, QWebEnginePage* page,
               const WizDocumentCallbackEpoch::Ticket& document,
               const std::function<void(const QString&)>& ready) {
        cancel();
        if (!owner || !page || !WizDocumentCallbackEpoch::current(document)) return;
        m_state = std::make_shared<State>(State{owner, page, document});
        const std::weak_ptr<State> weak(m_state);
        page->runJavaScript("WizEditor.link.getCurrentLink();", [weak, ready](const QVariant& value) {
            const auto state = weak.lock();
            // An invalid page-destruction result is not an empty valid URL.
            if (!valid(state) || value.userType() != QMetaType::QString) return;
            state->ready = true;
            ready(value.toString());
        });
    }
    QString takeCommand(QWebEnginePage* page, QString url) {
        if (!current(page)) { cancel(); return {}; }
        cancel(); // A repeated accepted signal cannot submit twice.
        if (url.isEmpty() || url == "http://")
            return "WizEditor.link.removeSelectedLink();";
        const QUrl parsed(url);
        // Keep the existing dialog's scheme/default/encoding behavior.
        url = parsed.scheme().isEmpty() ? "http://" + url : parsed.toString();
        QByteArray encoded = QJsonDocument(QJsonArray{url}).toJson(QJsonDocument::Compact);
        encoded = encoded.mid(1, encoded.size() - 2);
        return "WizEditor.link.setCurrentLink(" + QString::fromUtf8(encoded) + ");";
    }
};
#endif
