#pragma once
#include "WizCertificateCreation.h"
#include "WizObject.h"
#include <functional>
class WizDatabase;
namespace WizDocumentUnprotect {
enum class Error { None, ConsentRequired, Unsupported, Changed, PasswordInvalid, ReadFailed, BackupFailed, WriteFailed, MetadataFailed, RecoveryRequired };
struct Prepared {
    WIZDOCUMENTDATA document;
    WizCertificateCreation::Certificate certificate;
    QString account,archive,digest;
    bool valid=false;
};
struct Result {
    Error error=Error::ReadFailed;
    QString backup;
    bool published=false;
    explicit operator bool()const{return error==Error::None;}
};
// Local personal-note conversion only. No server/certificate/cache changes.
Prepared prepare(WizDatabase&,const WIZDOCUMENTDATA&);
bool current(WizDatabase&,const Prepared&);
Result execute(WizDatabase&,const Prepared&,const QString& password,bool explicitConsent,
               const std::function<bool()>& ownerCurrent={},
               const std::function<bool(const char*)>& checkpoint={});
}
