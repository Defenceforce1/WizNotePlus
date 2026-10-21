#ifndef WIZ_HTML_RESOURCE_URL_H
#define WIZ_HTML_RESOURCE_URL_H
#include <QString>
#include <QUrl>

// Keep the existing on-disk directory name. HTML text escaping is NOT URL
// encoding: the literal '&amp;' in that directory must survive HTML parsing,
// and '#', '?', '%' etc. must not become URL syntax or be decoded twice.
inline QString WizHtmlResourceUrl(const QString& fileTitle)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(fileTitle.toHtmlEscaped() + "_files")) + '/';
}
#endif
