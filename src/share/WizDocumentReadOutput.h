#ifndef WIZ_DOCUMENT_READ_OUTPUT_H
#define WIZ_DOCUMENT_READ_OUTPUT_H
#include "WizDocumentReadPermit.h"
#include <QSaveFile>

namespace WizDocumentReadOutput {
// A final plaintext output, not a note save. No target deletion or in-place
// fallback: stale/failed reads must preserve the user's previous output.
inline bool writeUtf8(const WizDocumentReadPermit& permit, const QString& destination,
                      const QString& text, bool bom=false)
{
    if (destination.isEmpty() || !permit.current()) return false;
    QSaveFile file(destination);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes=(bom ? QByteArray::fromHex("efbbbf") : QByteArray())+text.toUtf8();
    if (file.write(bytes)!=bytes.size() || !permit.current()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
}
#endif
