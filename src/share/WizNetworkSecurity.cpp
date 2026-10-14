#include "WizNetworkSecurity.h"
#include "WizBuildIdentity.h"
#include "WizEventLoop.h"
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>

namespace WizNetworkSecurity {
static QSslConfiguration secureConfiguration() {
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    return configuration;
}

void installSecureDefaults() {
    QSslConfiguration::setDefaultConfiguration(secureConfiguration());
}

int checkPublicHttps() {
    installSecureDefaults();
    int failures = 0;
    for (const auto& host : {QStringLiteral("api.wiz.cn"), QStringLiteral("as.wiz.cn")}) {
        QSslSocket socket;
        socket.setSslConfiguration(secureConfiguration());
        socket.connectToHostEncrypted(host, 443);
        const bool ok = socket.waitForEncrypted(15000);
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "verified public TLS handshake:" << host;
        if (!ok) qWarning() << "TLS error:" << socket.errorString();
        socket.abort(); failures += !ok;
    }
    qInfo() << "No login, account data, credentials or HTTP request sent.";
    return failures ? 1 : 0;
}

bool allowsAccountUrl(const QUrl& url) {
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    return url.scheme() == "https"
        || (!WizBuildIdentity::appleSiliconTrial && url.scheme() == "http");
}

bool prepareAccountRequest(QNetworkRequest& request, bool resourceDownload) {
    if (!allowsAccountUrl(request.url())) {
        qWarning() << "Account request rejected: HTTPS server address required";
        return false;
    }
    request.setSslConfiguration(secureConfiguration());
    // Credentials in the legacy API query/body must not follow another origin.
    // Resource download URLs may point to a CDN; HTTPS downgrade is still denied.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        resourceDownload ? QNetworkRequest::NoLessSafeRedirectPolicy
                         : QNetworkRequest::SameOriginRedirectPolicy);
    return true;
}

bool downloadAccountData(const QString& url, QByteArray& data,
                         QObject* receiver, const char* member) {
    QNetworkRequest request{QUrl(url)};
    if (!prepareAccountRequest(request, true)) return false;
    QNetworkAccessManager manager;
    auto* reply = manager.get(request);
    WizAutoTimeOutEventLoop loop(reply);
    if (receiver && member)
        QObject::connect(&loop, SIGNAL(downloadProgress(QUrl,qint64,qint64)), receiver, member);
    loop.setTimeoutWaitSeconds(NETWORK_TIMEOUT_SECS);
    loop.exec();
    if (loop.error() != QNetworkReply::NoError) {
        qWarning() << "Account resource download failed:" << int(loop.error());
        return false;
    }
    data = loop.result();
    return true;
}

QString safeLogUrl(const QUrl& url) {
    return url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QString redactLogUrls(const QString& message) {
    static const QRegularExpression urls(QStringLiteral(R"(https?://[^\s<>"']+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString result; int position = 0;
    auto matches = urls.globalMatch(message);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += message.mid(position, match.capturedStart() - position);
        const QUrl url(match.captured(), QUrl::StrictMode);
        result += url.isValid() ? safeLogUrl(url) : QStringLiteral("[redacted URL]");
        position = match.capturedEnd();
    }
    return result + message.mid(position);
}
}
