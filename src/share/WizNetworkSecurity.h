#pragma once

#include <QNetworkRequest>
#include <QString>
#include <functional>

class QObject;

namespace WizNetworkSecurity {
// Scoped to the current sync worker only. No URL, credentials or payload is
// retained; unrelated requests cannot extend another worker's quit timeout.
class ActivityScope final {
public:
    explicit ActivityScope(std::function<void()> observer);
    ~ActivityScope();
    ActivityScope(const ActivityScope&) = delete;
    ActivityScope& operator=(const ActivityScope&) = delete;
private:
    std::function<void()> m_previous;
};
void noteActivity();
// Keep the system trust store. Never bypass certificate errors or retry HTTP.
void installSecureDefaults();
// One-way shutdown affects network work only, never local document saves.
void beginShutdown();
bool isShuttingDown();
bool isNetworkCancellationRequested();
int checkPublicHttps(); // TLS handshake only; no login, tokens or application request.
bool allowsAccountUrl(const QUrl& url);
bool prepareAccountRequest(QNetworkRequest& request, bool resourceDownload = false);
bool downloadAccountData(const QString& url, QByteArray& data,
                         QObject* receiver = nullptr, const char* member = nullptr);
QString safeLogUrl(const QUrl& url);
QString redactLogUrls(const QString& message);
}
