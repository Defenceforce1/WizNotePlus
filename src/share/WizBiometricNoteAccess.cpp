#include "WizBiometricNoteAccess.h"
#include "WizSettings.h"
#include "WizSessionAccess.h"
#include "database/WizDatabase.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
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
Session::Session(WizSessionAccess::Monitor* monitor,PolicyEvents* policies,QObject* parent)
    :QObject(parent),m_monitor(monitor),m_hasMonitor(monitor!=nullptr)
{
    if(monitor)connect(monitor,&WizSessionAccess::Monitor::accessRetired,this,[this](bool){retire();});
    if(policies)connect(policies,&PolicyEvents::accessRetired,this,[this](const QString& policy){retire(policy);});
    if(auto* app=QCoreApplication::instance())connect(app,&QCoreApplication::aboutToQuit,this,&Session::beginShutdown);
}
Session* Session::applicationSession()
{
    auto* app=QCoreApplication::instance();
    if(!app||QThread::currentThread()!=app->thread()||QCoreApplication::closingDown())return nullptr;
    static QPointer<Session> value;
    // Connect retirement BEFORE document views subscribe to the monitor.
    if(!value)value=new Session(WizSessionAccess::Monitor::applicationMonitor(),PolicyEvents::applicationEvents(),app);
    return value;
}
bool Session::available() const
{
    return !m_closed&&QThread::currentThread()==thread()
        &&(!m_hasMonitor||(m_monitor&&!m_monitor->isClosing()));
}
Session::Ticket Session::contextTicket(const WizBiometricKeychain::Scope& scope)
{
    if(!available())return {};
    const auto identity=accessIdentity(scope),policy=policyIdentity(scope);
    if(identity.isEmpty()||policy.isEmpty())return {};
    auto account=scope;account.kbGuid.clear();const auto accountId=policyIdentity(account);
    if(m_account!=accountId){retire();m_account=accountId;}
    auto it=m_entries.find(policy);
    if(it==m_entries.end()||it->identity!=identity){
        // Replacing a certificate also destroys its old generation; reverting
        // later must not revive a grant for an earlier revision.
        m_entries.remove(policy);
        if(m_entries.size()>=128)m_entries.erase(m_entries.begin());
        it=m_entries.insert(policy,Entry{identity,std::make_shared<const char>(0),false});
    }
    return it->generation;
}
bool Session::current(const WizBiometricKeychain::Scope& scope,const Ticket& ticket) const
{
    if(!available()||!WizDocumentCallbackEpoch::current(ticket))return false;
    const auto it=m_entries.constFind(policyIdentity(scope));
    return it!=m_entries.cend()&&it->identity==accessIdentity(scope)
        &&!ticket.owner_before(it->generation)&&!it->generation.owner_before(ticket);
}
bool Session::authorized(const WizBiometricKeychain::Scope& scope,const Ticket& ticket) const
{
    return current(scope,ticket)&&m_entries.value(policyIdentity(scope)).authorized;
}
bool Session::grant(const WizBiometricKeychain::Scope& scope,const Ticket& ticket)
{
    if(!current(scope,ticket))return false;
    const auto policy=policyIdentity(scope);auto& entry=m_entries[policy];
    if(entry.authorized)return true;
    entry.authorized=true;
    // Notify other locked visible views only after the current proof returns.
    // Retirement cancels this queued notification through its weak generation.
    const auto identity=entry.identity;
    QTimer::singleShot(0,this,[this,identity,ticket,policy]{
        const auto it=m_entries.constFind(policy);
        if(available()&&it!=m_entries.cend()&&it->authorized&&it->identity==identity
            &&WizDocumentCallbackEpoch::current(ticket)&&!ticket.owner_before(it->generation)
            &&!it->generation.owner_before(ticket))emit authorizationAvailable(policy);
    });
    return true;
}
void Session::retire(const QString& policy)
{
    if(QThread::currentThread()!=thread())return;
    if(policy.isEmpty()){m_entries.clear();m_account.clear();}
    else m_entries.remove(policy);
}
void Session::beginShutdown(){m_closed=true;retire();}
void Gate::setSession(Session* session){revoke();m_session=session;m_sessionBound=true;}
void Gate::retireSession(bool all)
{
    if(!m_session)return;
    const auto policy=policyIdentity(m_context.scope);
    if(all||!policy.isEmpty())m_session->retire(all?QString():policy);
}
void Gate::begin(const Context& context, bool required)
{
    revoke();m_context=context;m_required=required;
    if(required&&m_sessionBound&&m_session){
        m_sessionTicket=m_session->contextTicket(context.scope);
        m_authorized=m_session->authorized(context.scope,m_sessionTicket);
    }
}
void Gate::revoke() { m_epoch.invalidate(); m_authorized=false;m_sessionTicket.reset(); }
bool Gate::current(const Context& context) const
{
    return context.openSequence && text(context.documentGuid,256)
        && context.openSequence==m_context.openSequence && context.documentGuid==m_context.documentGuid
        && !accessIdentity(context.scope).isEmpty()
        && accessIdentity(context.scope)==accessIdentity(m_context.scope)
        &&(!m_sessionBound||(m_session&&m_session->current(context.scope,m_sessionTicket)));
}
bool Gate::grant(const Context& context, const Ticket& ticket)
{
    const auto own=m_epoch.ticket();
    if (!WizDocumentCallbackEpoch::current(ticket) || own.owner_before(ticket) || ticket.owner_before(own)
        || !current(context)) return false;
    if(m_sessionBound&&(!m_session||!m_session->grant(context.scope,m_sessionTicket)))return false;
    m_authorized=true; return true;
}
bool Gate::requiresAuthorization(bool cachedPasswordExists) const
{
    if(!cachedPasswordExists)return true;
    if(!m_required)return false;
    if(m_sessionBound)return !m_session||!m_session->authorized(m_context.scope,m_sessionTicket);
    return !m_authorized;
}
}
