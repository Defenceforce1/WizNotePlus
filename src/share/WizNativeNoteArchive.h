#ifndef WIZ_NATIVE_NOTE_ARCHIVE_H
#define WIZ_NATIVE_NOTE_ARCHIVE_H

#include <QJsonObject>
#include <QList>
#include <QString>

namespace WizNativeNotes {
struct File {
    QString path;
    QString source;
};
// An application-owned ZIP envelope, not an official WizNote interchange format.
// Payloads remain original ZIW/attachment bytes; no account secrets are included.
bool writeArchive(const QString& destination, const QJsonObject& notes,
                  const QList<File>& files, QString& error);
// Destination must be a fresh, empty private staging directory.
bool readArchive(const QString& archive, const QString& staging,
                 QJsonObject& notes, QString& error);
bool safePath(const QString& path);
bool validateZiw(const QString& path, QString& error);
}
#endif
