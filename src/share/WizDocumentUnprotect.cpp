#include "WizDocumentUnprotect.h"
#include "WizZiwReader.h"
#include "database/WizDatabase.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QPointer>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QUuid>
#include <quazip.h>
#include <quazipfile.h>
#include <mutex>

namespace WizDocumentUnprotect {
namespace {
std::mutex mutation;
QString digest(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?QString::fromLatin1(hash.result().toHex()):QString();}
bool replace(const QString& from,const QString& to){
    QFile source(from);QSaveFile destination(to);destination.setDirectWriteFallback(false);
    if(!source.open(QIODevice::ReadOnly)||!destination.open(QIODevice::WriteOnly))return false;
    destination.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    while(!source.atEnd()){const auto data=source.read(65536);if(data.isEmpty()&&source.error()!=QFile::NoError){destination.cancelWriting();return false;}if(destination.write(data)!=data.size()){destination.cancelWriting();return false;}}
    return destination.commit();
}
bool validZip(const QString& path){
    QuaZip zip(path);if(!zip.open(QuaZip::mdUnzip))return false;bool html=false,ok=true;
    for(bool next=zip.goToFirstFile();next;next=zip.goToNextFile()){
        html|=zip.getCurrentFileName()=="index.html";QuaZipFile file(&zip);
        if(!file.open(QIODevice::ReadOnly)){ok=false;break;}
        while(!file.atEnd()){const auto bytes=file.read(65536);if(bytes.isEmpty()){ok=false;break;}}
        file.close();if(file.getZipError()!=UNZ_OK){ok=false;break;}
    }
    const bool complete=zip.getZipError()==UNZ_END_OF_LIST_OF_FILE||zip.getZipError()==UNZ_OK;zip.close();return html&&ok&&complete;
}
}
Prepared prepare(WizDatabase& db,const WIZDOCUMENTDATA& requested){
    Prepared p;if(db.isGroup()||db.isEncryptAllData()||requested.strKbGUID!=db.kbGUID()||QUuid(requested.strGUID).isNull())return p;
    if(!db.documentFromGuid(requested.strGUID,p.document)||!p.document.nProtected||!db.canEditDocument(p.document))return p;
    p.archive=db.getDocumentFileName(p.document.strGUID);const QFileInfo file(p.archive);
    if(!file.isFile()||file.isSymLink()||!WizZiwReader::isEncryptedFile(p.archive))return p;
    p.account=db.getUserGuid();
    if(!db.getUserCert(p.certificate.n,p.certificate.e,p.certificate.encryptedD,p.certificate.hint)
       ||WizCertificateCreation::publicFingerprint(p.certificate).isEmpty()){
        p.error=Error::CertificateUnavailable;return p;
    }
    p.digest=digest(p.archive);if(p.digest.isEmpty()){p.error=Error::ReadFailed;return p;}
    p.valid=!p.account.isEmpty();if(p.valid)p.error=Error::None;return p;
}
bool current(WizDatabase& db,const Prepared& expected){
    if(!expected.valid)return false;const auto p=prepare(db,expected.document);
    return p.valid&&p.account==expected.account&&p.archive==expected.archive&&p.digest==expected.digest
        &&p.certificate.n==expected.certificate.n&&p.certificate.e==expected.certificate.e&&p.certificate.encryptedD==expected.certificate.encryptedD
        &&p.document.strDataMD5==expected.document.strDataMD5&&p.document.nVersion==expected.document.nVersion
        &&p.document.strTitle==expected.document.strTitle&&p.document.strLocation==expected.document.strLocation
        &&p.document.tModified==expected.document.tModified&&p.document.tDataModified==expected.document.tDataModified;
}
Result execute(WizDatabase& db,const Prepared& p,const QString& password,bool consent,const std::function<bool()>& ownerCurrent,const std::function<bool(const char*)>& checkpoint){
    Result result;if(!consent){result.error=Error::ConsentRequired;return result;}
    const std::unique_lock<std::mutex> lock(mutation,std::try_to_lock);if(!lock.owns_lock()||!p.valid){result.error=!p.valid&&p.error!=Error::None?p.error:Error::Unsupported;return result;}
    const auto allowed=[&]{return (!ownerCurrent||ownerCurrent())&&current(db,p);};
    if(!allowed()){result.error=Error::Changed;return result;}
    if(WizCertificateCreation::verify(p.certificate,password)!=WizCertificateCreation::Error::None){result.error=Error::PasswordInvalid;return result;}
    QTemporaryDir staging(QFileInfo(p.archive).absolutePath()+"/.unprotect-XXXXXX");if(!staging.isValid())return result;
    QFile::setPermissions(staging.path(),QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    const auto plain=staging.filePath("plain.ziw");WizZiwReader reader;reader.setRSAKeys(p.certificate.n.toUtf8(),p.certificate.e.toUtf8(),p.certificate.encryptedD.toUtf8(),p.certificate.hint);reader.setOperationPassword(password);
    if(!reader.decryptFileToFile(p.archive,plain)||!validZip(plain))return result;
    if(!allowed()){result.error=Error::Changed;return result;}
    const QString folder=QFileInfo(p.archive).absolutePath()+"/encrypted-backups";
    if(!QDir().mkpath(folder)){result.error=Error::BackupFailed;return result;}
    QFile::setPermissions(folder,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    const QString backup=folder+"/"+p.document.strGUID+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".ziw";
    if(!QFile::copy(p.archive,backup)){result.error=Error::BackupFailed;return result;}
    result.backup=backup;QFile::setPermissions(backup,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    if(digest(backup)!=p.digest){result.error=Error::BackupFailed;return result;}
    if(!allowed()||(checkpoint&&(!checkpoint("prepared")||!allowed()))){result.error=Error::Changed;return result;}
    // Metadata is rolled back on ordinary failure. File publication uses an
    // atomic replacement and the complete encrypted backup is never removed.
    // This is not a crash/concurrent-writer transaction across file and SQLite.
    QSignalBlocker blocker(&db);if(!db.execSQL("SAVEPOINT wiz_unprotect")){result.error=Error::MetadataFailed;return result;}
    const auto rollback=[&]{const bool rolled=db.execSQL("ROLLBACK TO wiz_unprotect");const bool released=db.execSQL("RELEASE wiz_unprotect");return rolled&&released;};
    // Preserve the latest ancillary metadata (flags/read count/etc.), rather
    // than overwriting it with a copy captured before the modal prompts.
    WIZDOCUMENTDATA converted;
    if(!db.documentFromGuid(p.document.strGUID,converted)){rollback();result.error=Error::Changed;return result;}
    const auto before=converted;converted.nProtected=0;
    if((checkpoint&&!checkpoint("metadata"))||!db.WizIndex::updateDocumentDataMD5(converted,plain,false)){rollback();result.error=Error::MetadataFailed;return result;}
    if((checkpoint&&!checkpoint("publish"))||!replace(plain,p.archive)){const bool restored=rollback();result.error=restored?Error::WriteFailed:Error::RecoveryRequired;return result;}
    result.published=true;
    if((checkpoint&&!checkpoint("release"))||!db.execSQL("RELEASE wiz_unprotect")){
        const bool restored=(!checkpoint||checkpoint("restore"))&&replace(backup,p.archive);const bool metadata=rollback();result.published=!restored;result.error=restored&&metadata?Error::MetadataFailed:Error::RecoveryRequired;return result;
    }
    result.error=Error::None;blocker.unblock();const QPointer<WizDatabase> alive(&db);
    db.updateDocumentAbstract(converted.strGUID);
    if(alive)emit alive->documentModified(before,converted);
    if(alive)emit alive->documentDataModified(converted);
    return result;
}
}
