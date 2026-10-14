#pragma once

#include <QNetworkRequest>
#include <QString>

class QObject;

namespace WizNetworkSecurity {
// Keep the system trust store. Never bypass certificate errors or retry HTTP.
void installSecureDefaults();
int checkPublicHttps(); // TLS handshake only; no login, tokens or application request.
bool allowsAccountUrl(const QUrl& url);
bool prepareAccountRequest(QNetworkRequest& request, bool resourceDownload = false);
bool downloadAccountData(const QString& url, QByteArray& data,
                         QObject* receiver = nullptr, const char* member = nullptr);
QString safeLogUrl(const QUrl& url);
QString redactLogUrls(const QString& message);
}
