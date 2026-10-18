#ifndef WIZSERVICE_ASYNCAPI_H
#define WIZSERVICE_ASYNCAPI_H

#include <QObject>
#include <atomic>
#include <memory>

class QString;
struct WIZUSERINFO;


class WizAsyncApi : public QObject
{
    Q_OBJECT

    // Internal UI helper: construct, call and read state on the application
    // thread. Only immutable request values and reply values cross workers.

public:
    explicit WizAsyncApi(QObject *parent = 0);
    ~WizAsyncApi();

    void registerAccount(const QString& strUserId, const QString& strPasswd, const QString& strInviteCode,
                         const QString& strCaptchaID = "", const QString& strCaptcha = "");
    void setMessageReadStatus(const QString& ids, bool bRead);
    void setMessageDeleteStatus(const QString& ids, bool bDelete);

    int lastErrorCode() { return m_nErrorCode; }
    QString lastErrorMessage() { return m_strErrorMessage; }

private:
    int m_nErrorCode;
    QString m_strErrorMessage;
    std::shared_ptr<std::atomic_bool> m_canceled;

Q_SIGNALS:
    void registerAccountFinished(bool bOk);    
    void uploadMessageReadStatusFinished(const QString& ids);
    void uploadMessageDeleteStatusFinished(const QString& ids);
};

#endif // WIZSERVICE_ASYNCAPI_H
