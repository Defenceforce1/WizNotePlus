#include "WizBiometricNoteAccess.h"
#include "WizSettings.h"
#include "database/WizDatabase.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QThread>
namespace WizBiometricNoteAccess {
namespace {
bool text(const QString& s, int max, bool empty = false)
{
    return (empty || !s.trimmed().isEmpty()) && s.size() <= max && !s.contains(QChar::Null)
        && QString::fromUtf8(s.toUtf8()) == s;
}
}
WizBiometricKeychain::Scope snapshot(WizDatabase& personal, WizDatabase& target, const QString& server)
{
    WizBiometricKeychain::Scope scope{server, personal.getUserGuid(), target.kbGUID(), {}};
    target.getUserCert(scope.certificate.n, scope.certificate.e, scope.certificate.encryptedD, scope.certificate.hint);
    return scope;
}
QString policyIdentity(const WizBiometricKeychain::Scope& scope)
{
    if (!text(scope.serverIdentity,2048) || !text(scope.accountGuid,256) || !text(scope.kbGuid,256,true)) return {};
    const QJsonArray fields{QStringLiteral("WizNotePlus.biometric-policy.v1"),scope.serverIdentity,scope.accountGuid,scope.kbGuid};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(fields).toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256).toHex());
}
bool optedIn(WizUserSettings& settings, const WizBiometricKeychain::Scope& scope)
{
    const auto id = policyIdentity(scope);
    return !id.isEmpty() && settings.get("BiometricUnlock",id) == "1";
}
QString accessIdentity(const WizBiometricKeychain::Scope& scope)
{
    if(policyIdentity(scope).isEmpty()||!text(scope.certificate.n,32768)
        ||!text(scope.certificate.e,32768)||!text(scope.certificate.encryptedD,32768))return {};
    const QJsonArray fields{QStringLiteral("WizNotePlus.encrypted-note-access.v1"),scope.serverIdentity,
        scope.accountGuid,scope.kbGuid,scope.certificate.n,scope.certificate.e,scope.certificate.encryptedD};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(fields).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}
PolicyEvents* PolicyEvents::applicationEvents()
{
    auto* app=QCoreApplication::instance();
    if(!app||QThread::currentThread()!=app->thread())return nullptr;
    static QPointer<PolicyEvents> events;
    if(!events)events=new PolicyEvents(app);
    return events;
}
void PolicyEvents::retire(const QString& policy)
{
    if(QThread::currentThread()!=thread()||policy.size()!=64)return;
    for(const auto c:policy)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return;
    emit accessRetired(policy); // Receivers may destroy this object.
}
void Gate::begin(const Context& context, bool required) { revoke(); m_context=context; m_required=required; }
void Gate::revoke() { m_epoch.invalidate(); m_authorized=false; }
bool Gate::current(const Context& context) const
{
    return context.openSequence && text(context.documentGuid,256)
        && context.openSequence==m_context.openSequence && context.documentGuid==m_context.documentGuid
        && !accessIdentity(context.scope).isEmpty()
        && accessIdentity(context.scope)==accessIdentity(m_context.scope);
}
bool Gate::grant(const Context& context, const Ticket& ticket)
{
    const auto own=m_epoch.ticket();
    if (!WizDocumentCallbackEpoch::current(ticket) || own.owner_before(ticket) || ticket.owner_before(own)
        || !current(context)) return false;
    m_authorized=true; return true;
}
bool Gate::requiresAuthorization(bool cachedPasswordExists) const
{
    return !cachedPasswordExists || (m_required && !m_authorized);
}
}
