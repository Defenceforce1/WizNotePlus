#pragma once

#include "share/WizDocumentAsync.h"
#include "share/WizMarkdownExportRecovery.h"
#include <functional>

class QCoreApplication;
class QWidget;
class WizMarkdownExportRecoverySession;

// Experimental App wiring only; normal builds exclude this module. No startup
// scan, implicit confirmation, automatic retry or ownership of save/sync policy.
namespace WizMarkdownExportRecoveryApp {

bool isQuitting(); // GUI thread only; fail closed off the application thread.

// Same user-selected target only. The no-site continuation is NOT a reservation:
// the writer must freshly validate absence under the destination lock. Never
// continue exporting merely because a session emitted settled or recovered.
WizMarkdownExportRecoverySession* open(QWidget* owner,
    WizDocumentCallbackEpoch::Ticket document, const QString& destination,
    std::function<void()> noSiteContinuation,
    std::function<void(const WizMarkdownExportRecovery::Result&)> result);

// Retain all app-owned sessions BEFORE local-save preparation can pump events.
// A failed/cancelled quit scope permits future explicit exports again. Commit
// keeps new work blocked. This scope never joins, saves, syncs or starts timers.
class QuitScope final {
public:
    QuitScope();
    ~QuitScope();
    QuitScope(const QuitScope&) = delete;
    QuitScope& operator=(const QuitScope&) = delete;
    void commit();
private:
    QPointer<QCoreApplication> m_application;
};

}
