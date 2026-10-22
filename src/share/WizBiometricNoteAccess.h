#ifndef WIZ_BIOMETRIC_NOTE_ACCESS_H
#define WIZ_BIOMETRIC_NOTE_ACCESS_H
#include "widgets/WizBiometricUnlockWidget.h"
class WizDatabase;
class WizUserSettings;
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
class Gate final {
public:
    using Ticket = WizDocumentCallbackEpoch::Ticket;
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
};
}
#endif
