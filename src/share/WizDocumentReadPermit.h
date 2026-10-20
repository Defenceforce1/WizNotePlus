#ifndef WIZ_DOCUMENT_READ_PERMIT_H
#define WIZ_DOCUMENT_READ_PERMIT_H
#include "WizDocumentAsync.h"
#include <functional>

// GUI-only permission for a transient plaintext read/output, never a save
// receipt. Revocation must not invalidate the independent save/print barriers.
class WizDocumentReadPermit final {
public:
    WizDocumentReadPermit() = default;
    WizDocumentReadPermit(QObject* owner, QObject* page, WizDocumentCallbackEpoch::Ticket document,
                          std::function<bool()> allowed)
        :m_owner(owner),m_page(page),m_document(std::move(document)),m_allowed(std::move(allowed)){}
    bool current() const {
        return m_owner && m_page && WizDocumentCallbackEpoch::current(m_document)
            && m_allowed && m_allowed();
    }
private:
    QPointer<QObject> m_owner,m_page;
    WizDocumentCallbackEpoch::Ticket m_document;
    std::function<bool()> m_allowed;
};
#endif
