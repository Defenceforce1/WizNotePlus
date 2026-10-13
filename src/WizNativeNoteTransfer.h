#ifndef WIZ_NATIVE_NOTE_TRANSFER_H
#define WIZ_NATIVE_NOTE_TRANSFER_H
#include "share/WizObject.h"
#include <QStringList>
class QWidget;
class WizDatabase;
namespace WizNativeNotes {
bool exportNotes(WizDatabase& db, const CWizDocumentDataArray& documents,
                 const QString& destination, QString& error);
// A new directory containing original HTML/resources and named attachment files.
// Plaintext notes only; never silently decrypts or overwrites a destination.
bool exportPortableHtml(WizDatabase& db, const CWizDocumentDataArray& documents,
                        const QString& destination, QString& error);
struct ImportResult {
    int imported = 0;
    QString folder;
    QString error;
};
// Entire archive is verified before writes. Each imported note gets new IDs.
ImportResult importNotes(WizDatabase& db, const QString& archive);
CWizDocumentDataArray chooseNotes(QWidget* parent, WizDatabase& db,
                                 const CWizDocumentDataArray& selected);
void showImport(QWidget* parent, WizDatabase& db);
}
#endif
