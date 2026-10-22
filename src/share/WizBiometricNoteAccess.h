#ifndef WIZ_BIOMETRIC_NOTE_ACCESS_H
#define WIZ_BIOMETRIC_NOTE_ACCESS_H
#include "widgets/WizBiometricUnlockWidget.h"
#include <QHash>
#include <QSet>
class WizDatabase;
class WizUserSettings;
namespace WizSessionAccess { class Monitor; }
namespace WizBiometricNoteAccess {
using Context = WizBiometricUnlockWidget::Context;
WizBiometricKeychain::Scope snapshot(WizDatabase& personal, WizDatabase& target, const QString& server);
// Opt-in survives key revision; old cached passwords must not turn into a
// legacy bypass merely because the certificate changed.
QString policyIdentity(const WizBiometricKeychain::Scope& scope);
// Access timing is independent of biometric enrollment or keychain eligibility.
QString accessIdentity(const WizBiometricKeychain::Scope& scope);
bool optedIn(WizUserSettings& settings, const WizBiometricKeychain::Scope& scope);
// Process-local retirement only. Carries a framed policy digest, never a
// password/certificate. Receivers still check their own live scope and policy.
class PolicyEvents final : public QObject {
    Q_OBJECT
public:
    explicit PolicyEvents(QObject* parent=nullptr):QObject(parent){}
    static PolicyEvents* applicationEvents();
    void retire(const QString& policy);
Q_SIGNALS:
    void accessRetired(const QString& policy);
};
// Application-lifetime proof, scoped to account/server/knowledge base/current
// certificate AND individually verified note. A shared certificate/cache does
// not unlock other notes. Stores only digests/generations, never passwords.
class Session final : public QObject {
    Q_OBJECT
public:
    using Ticket = WizDocumentCallbackEpoch::Ticket;
    explicit Session(WizSessionAccess::Monitor* monitor=nullptr,PolicyEvents* policies=nullptr,QObject* parent=nullptr);
    static Session* applicationSession();
    void retire(const QString& policy={});
    void beginShutdown();
Q_SIGNALS:
    void authorizationAvailable(const QString& policy,const QString& documentGuid);
private:
    friend class Gate;
    struct Entry { QString identity; std::shared_ptr<const char> generation; QSet<QString> authorizedNotes; };
    Ticket contextTicket(const Context& context);
    bool current(const Context& context,const Ticket& ticket) const;
    bool authorized(const Context& context,const Ticket& ticket) const;
    bool grant(const Context& context,const Ticket& ticket);
    bool available() const;
    QHash<QString,Entry> m_entries;
    QString m_account;
    QPointer<WizSessionAccess::Monitor> m_monitor;
    bool m_hasMonitor=false,m_closed=false;
};
class Gate final {
public:
    using Ticket = WizDocumentCallbackEpoch::Ticket;
    void setSession(Session* session);
    bool hasSession() const { return m_sessionBound; }
    void retireSession(bool all=false);
    void begin(const Context& context, bool required);
    void revoke();
    Ticket ticket() const { return m_epoch.ticket(); }
    bool current(const Context& context) const;
    // Only after a fresh credential was locally verified and accepted.
    bool grant(const Context& context, const Ticket& ticket);
    bool requiresAuthorization(bool cachedPasswordExists) const;
private:
    Context m_context;
    bool m_required = false, m_authorized = false;
    WizDocumentCallbackEpoch m_epoch;
    QPointer<Session> m_session;
    bool m_sessionBound=false;
    Ticket m_sessionTicket;
};
}
#endif
