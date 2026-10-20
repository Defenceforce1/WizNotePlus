#ifndef WIZ_ACCOUNT_WEB_CONTENT_H
#define WIZ_ACCOUNT_WEB_CONTENT_H
#include <QWebEnginePage>
#include <QWebEngineProfile>

// Account pages can retain an old avatar under an unchanged avatarVersion URL.
// Do not clear shared caches or alter cookies/storage, upload handling or TLS.
// Configure only the dialog-owned profile, before its first account navigation.
inline bool wizUseFreshAccountContent(QWebEnginePage* page)
{
    if (!page || page->profile() == QWebEngineProfile::defaultProfile()) return false;
    page->profile()->setHttpCacheType(QWebEngineProfile::NoCache);
    return page->profile()->httpCacheType() == QWebEngineProfile::NoCache;
}
#endif
