#ifndef WIZ_LOCAL_FILE_URL_H
#define WIZ_LOCAL_FILE_URL_H
#include <QDir>
#include <QUrl>

// A filesystem path is not a URL. Preserve Unicode, spaces, # and % literally.
inline QUrl WizLocalFileUrl(const QString& path)
{
    return QUrl::fromLocalFile(QDir(path).absolutePath());
}
#endif
