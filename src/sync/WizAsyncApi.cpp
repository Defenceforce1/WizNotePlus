#include "WizAsyncApi.h"

#include <QCoreApplication>
#include <QPointer>
#include "share/WizThreads.h"

#ifdef WIZNOTE_ASYNC_API_TEST_TRANSPORT
// Only the isolated unit target replaces the transport, never the App target.
#include "async-api-transport-stub.h"
#else
#include "WizKMServer.h"
#include "WizToken.h"
#endif

namespace {
struct Reply {
    bool available = false;
    bool succeeded = false;
    int errorCode = 0;
    QString errorMessage;
};
Reply resultOf(WizKMAccountsServer& server, bool succeeded)
{
    Reply result;
    result.available = true;
    result.succeeded = succeeded;
    if (!succeeded) {
        result.errorCode = server.getLastErrorCode();
        result.errorMessage = server.getLastErrorMessage();
    }
    return result;
}

template<class Work, class Deliver>
void enqueue(const std::shared_ptr<std::atomic_bool>& canceled, Work work, Deliver deliver)
{
    // The application dispatcher outlives the queued workers. No API QObject
    // or UI member is dereferenced on a worker, even after a pointer check.
    QObject* const dispatcher = QCoreApplication::instance();
    if (!dispatcher || canceled->load()) return;
    WizExecuteOnThread(WIZ_THREAD_NETWORK, [canceled, dispatcher, work, deliver] {
        if (canceled->load()) return;
        const Reply result = work();
        if (!result.available || canceled->load()) return;
        QMetaObject::invokeMethod(dispatcher, [canceled, deliver, result] {
            if (!canceled->load()) deliver(result);
        }, Qt::QueuedConnection);
    });
}
}

WizAsyncApi::WizAsyncApi(QObject* parent)
    : QObject(parent), m_nErrorCode(0), m_canceled(std::make_shared<std::atomic_bool>(false))
{
    qRegisterMetaType<WIZUSERINFO>("WIZUSERINFO");
}

WizAsyncApi::~WizAsyncApi()
{
    m_canceled->store(true);
}

void WizAsyncApi::registerAccount(const QString& user, const QString& password,
                                const QString& invite, const QString& captchaID, const QString& captcha)
{
    const QPointer<WizAsyncApi> owner(this);
    enqueue(m_canceled, [user, password, invite, captchaID, captcha] {
        WizKMAccountsServer server;
        const bool succeeded = server.createAccount(user, password, invite, captchaID, captcha);
        return resultOf(server, succeeded);
    }, [owner](const Reply& result) {
        if (!owner) return;
        owner->m_nErrorCode = result.errorCode;
        owner->m_strErrorMessage = result.errorMessage;
        Q_EMIT owner->registerAccountFinished(result.succeeded);
        // A connected slot may synchronously delete owner: no access follows.
    });
}

void WizAsyncApi::setMessageReadStatus(const QString& ids, bool read)
{
    const QPointer<WizAsyncApi> owner(this);
    enqueue(m_canceled, [ids, read] {
        const QString token = WizToken::token();
        if (token.isEmpty()) return Reply{};
        WizKMAccountsServer server;
        WIZUSERINFO info = WizToken::userInfo();
        info.strToken = token;
        server.setUserInfo(info);
        const bool succeeded = server.setMessageReadStatus(ids, read);
        return resultOf(server, succeeded);
    }, [owner, ids](const Reply& result) {
        if (!owner) return;
        owner->m_nErrorCode = result.errorCode;
        owner->m_strErrorMessage = result.errorMessage;
        if (result.succeeded) Q_EMIT owner->uploadMessageReadStatusFinished(ids);
    });
}

void WizAsyncApi::setMessageDeleteStatus(const QString& ids, bool deleted)
{
    const QPointer<WizAsyncApi> owner(this);
    enqueue(m_canceled, [ids, deleted] {
        const QString token = WizToken::token();
        if (token.isEmpty()) return Reply{};
        WizKMAccountsServer server;
        WIZUSERINFO info = WizToken::userInfo();
        info.strToken = token;
        server.setUserInfo(info);
        const bool succeeded = server.setMessageDeleteStatus(ids, deleted);
        return resultOf(server, succeeded);
    }, [owner, ids](const Reply& result) {
        if (!owner) return;
        owner->m_nErrorCode = result.errorCode;
        owner->m_strErrorMessage = result.errorMessage;
        if (result.succeeded) Q_EMIT owner->uploadMessageDeleteStatusFinished(ids);
    });
}
