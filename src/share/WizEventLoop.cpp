#include "WizEventLoop.h"

#include <QTimer>
#include "WizNetworkSecurity.h"
#include <QDebug>

const int NETWORK_TIMEOUT_SECS = 60;

WizAutoTimeOutEventLoop::WizAutoTimeOutEventLoop(QNetworkReply* pReply, QObject *parent /*= 0*/)
    : QEventLoop(parent)
    , m_error(QNetworkReply::NoError)
    , m_timeOut(false)
    , m_timeOutSeconds(NETWORK_TIMEOUT_SECS)
    , m_downloadBytes(0)
    , m_lastDownloadBytes(0)
    , m_uploadBytes(0)
    , m_lastUploadBytes(0)
    , m_reply(pReply)
    , m_finished(false)
{
    m_url = pReply->request().url();
    connect(pReply, SIGNAL(finished()), SLOT(on_replyFinished()));
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(pReply, &QNetworkReply::errorOccurred, this, &WizAutoTimeOutEventLoop::on_replyError);
#else
    connect(pReply, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(on_replyError(QNetworkReply::NetworkError)));
#endif
    connect(pReply, SIGNAL(downloadProgress(qint64,qint64)), SLOT(on_downloadProgress(qint64,qint64)));
    connect(pReply, SIGNAL(uploadProgress(qint64,qint64)), SLOT(on_uploadProgress(qint64,qint64)));
    connect(pReply, &QNetworkReply::encrypted, this, [] { WizNetworkSecurity::noteActivity(); });
    connect(&m_timer, SIGNAL(timeout()), SLOT(on_timeOut()));
    m_cancelTimer.setInterval(50);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this] {
        if (!WizNetworkSecurity::isNetworkCancellationRequested()) return;
        m_error = QNetworkReply::OperationCanceledError;
        m_errorString = QStringLiteral("Network work canceled during shutdown");
        m_reply->abort();
        quit();
    });
}

WizAutoTimeOutEventLoop::~WizAutoTimeOutEventLoop()
{
    if (m_reply)
    {
        m_reply->close();
        m_reply->deleteLater();
    }
}

QNetworkReply::NetworkError WizAutoTimeOutEventLoop::error() const
{
    if (m_error != QNetworkReply::NoError)
        return m_error;

    if (!m_finished) {
        return QNetworkReply::OperationCanceledError;
    }

    return m_error;
}

QString WizAutoTimeOutEventLoop::errorString() const
{
    return m_errorString;
}

void WizAutoTimeOutEventLoop::setTimeoutWaitSeconds(int seconds)
{
    m_timeOutSeconds = seconds;
}

QNetworkReply*WizAutoTimeOutEventLoop::networkReply() const
{
    return m_reply;
}

void WizAutoTimeOutEventLoop::doFinished(QNetworkReply* reply)
{
    m_finished = true;
    if (m_error == QNetworkReply::NoError) {
        m_error = reply->error();
        if (m_error) {
            m_errorString = reply->errorString();
            return;
        }
        m_result = reply->readAll();
        WizNetworkSecurity::noteActivity();
    }
}

void WizAutoTimeOutEventLoop::doError(QNetworkReply::NetworkError error)
{
    m_error = error;
    m_errorString = m_reply->errorString();
}

int WizAutoTimeOutEventLoop::exec(QEventLoop::ProcessEventsFlags flags)
{
    if (WizNetworkSecurity::isNetworkCancellationRequested()) {
        m_error = QNetworkReply::OperationCanceledError;
        m_errorString = QStringLiteral("Network work canceled during shutdown");
        m_reply->abort();
        return 0;
    }
    if (m_reply->isFinished()) { on_replyFinished(); return 0; }
    m_cancelTimer.start();
    m_timer.start(m_timeOutSeconds * 1000);
    const int result = QEventLoop::exec(flags);
    m_cancelTimer.stop();
    m_timer.stop();
    return result;
}

void WizAutoTimeOutEventLoop::on_replyFinished()
{
    m_timer.stop();

    doFinished(m_reply);

    quit();
}

void WizAutoTimeOutEventLoop::on_replyError(QNetworkReply::NetworkError error)
{
    m_timer.stop();
    doError(error);

    quit();
}

void WizAutoTimeOutEventLoop::on_timeOut()
{
    qDebug() << "network status check, " << "downloaded bytes : " << m_downloadBytes << "  last time : " << m_lastDownloadBytes
             << " uploaded bytes : " << m_uploadBytes << " last time : " << m_lastUploadBytes;
    if (m_downloadBytes != m_lastDownloadBytes)
    {
        m_lastDownloadBytes = m_downloadBytes;
    }
    else if (m_uploadBytes != m_lastUploadBytes)
    {
        m_lastUploadBytes = m_uploadBytes;
    }
    else
    {
        m_timeOut = true;
        m_error = QNetworkReply::TimeoutError;
        m_errorString = "Event loop time out, can not get response from network reply";
        qWarning() << "[sync]Xml rpc event loop time out";
        m_timer.stop();
        m_reply->abort();

        quit();
    }
}

void WizAutoTimeOutEventLoop::on_downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesReceived > m_downloadBytes) WizNetworkSecurity::noteActivity();
    m_downloadBytes = bytesReceived;
    emit downloadProgress(m_url, bytesReceived, bytesTotal);
}

void WizAutoTimeOutEventLoop::on_uploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    if (bytesSent > m_uploadBytes) WizNetworkSecurity::noteActivity();
    m_uploadBytes = bytesSent;
}
