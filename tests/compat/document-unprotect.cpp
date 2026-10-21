#include "share/WizDocumentUnprotect.h"
#include "share/WizZiwReader.h"
#include "widgets/WizUnprotectDialog.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QWidget>
#include <quazip.h>
#include <quazipfile.h>
namespace {
QByteArray read(const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
bool put(const QString& path,const QByteArray& bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
}
int wizDocumentUnprotectSelfTest(int argc,char** argv){
    // Early dispatch: no normal account, native authentication, service or
    // real note is opened. The actual database/reader/saver use disposable data.
    QTemporaryDir root;if(!root.isValid())return 2;QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());qputenv("XDG_CONFIG_HOME",root.filePath("config").toUtf8());qputenv("TMPDIR",root.filePath("tmp").toUtf8());
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    using namespace WizDocumentUnprotect;int checks=0,failures=0;
    const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;};
    WizDatabaseManager manager("synthetic-unprotect-only");if(!check(manager.open(),"actual temporary database opens without normal login"))return 1;
    auto& db=manager.db();db.setMeta("Account","GUID","synthetic-unprotect-account");
    const QString password="Synthetic-unprotect-密码-2026";const auto generated=WizCertificateCreation::generate(password,password,{});const auto& cert=generated.certificate;
    if(!check(bool(generated)&&db.setUserCert(cert.n,cert.e,cert.encryptedD,{})&&db.cacheCertPasswordLocally(password,cert.n,cert.e,cert.encryptedD),"synthetic certificate is installed only into temporary database"))return 1;
    const QString plain=root.filePath("source.ziw");QuaZip zip(plain);bool zipped=zip.open(QuaZip::mdCreate);
    for(const auto& entry:QList<QPair<QString,QByteArray>>{{"index.html","<html><body>synthetic <img src='index_files/a.bin'></body></html>"},{"index_files/a.bin",QByteArray::fromHex("000102ff0a")},{"index_files/empty",{}}}){
        QuaZipFile file(&zip);if(!zipped||!file.open(QIODevice::WriteOnly,QuaZipNewInfo(entry.first))){zipped=false;break;}zipped=file.write(entry.second)==entry.second.size();file.close();zipped&=file.getZipError()==UNZ_OK;
    }
    zip.close();zipped&=zip.getZipError()==UNZ_OK;
    WizZiwReader reader;reader.setDatabase(&db);reader.setRSAKeys(cert.n.toUtf8(),cert.e.toUtf8(),cert.encryptedD.toUtf8(),{});const auto encryptedPath=root.filePath("encrypted.ziw");
    if(!check(zipped&&reader.encryptFileToFile(plain,encryptedPath),"actual native reader encrypts synthetic ZIP and embedded binary resources"))return 1;
    const auto original=read(plain),encrypted=read(encryptedPath);
    const auto create=[&]{WIZDOCUMENTDATA doc;if(!db.createDocument("synthetic","source.ziw","/synthetic/",{},1,doc)||!put(db.getDocumentFileName(doc.strGUID),encrypted)||!db.setObjectDataDownloaded(doc.strGUID,"document",true))doc.strGUID.clear();return doc;};
    const auto protectedOriginal=[&](const WIZDOCUMENTDATA& doc){WIZDOCUMENTDATA actual;return db.documentFromGuid(doc.strGUID,actual)&&actual.nProtected&&read(db.getDocumentFileName(doc.strGUID))==encrypted;};
    auto doc=create();auto prepared=prepare(db,doc);
    check(prepared.valid&&prepared.error==Error::None&&current(db,prepared),"prepared conversion binds current personal note certificate metadata and encrypted bytes");
    check(execute(db,prepared,password,false).error==Error::ConsentRequired&&protectedOriginal(doc),"explicit consent is required before any conversion");
    check(execute(db,prepared,"wrong",true).error==Error::PasswordInvalid&&protectedOriginal(doc)&&db.getCertPassword()==password,"wrong fresh password cannot borrow warm shared cache or mutate original");
    check(execute(db,prepared,password,true,[]{return false;}).error==Error::Changed&&protectedOriginal(doc),"retired UI owner cannot start conversion");
    check(!db.cancelDocumentEncryption(doc)&&protectedOriginal(doc),"legacy no-password cancellation API refuses destructive conversion");
    auto malformed=doc;malformed.strGUID="not-a-guid";check(!prepare(db,malformed).valid,"invalid or missing note refuses preparation");
    malformed=doc;malformed.strKbGUID="different-library";check(!prepare(db,malformed).valid,"another knowledge base cannot use a personal conversion context");
    const auto originalAccount=db.getUserGuid();db.setMeta("Account","GUID","different-account");
    check(execute(db,prepared,password,true).error==Error::Changed&&protectedOriginal(doc),"changed account refuses old confirmation scope");db.setMeta("Account","GUID",originalAccount);
    db.setUserCert(cert.n,cert.e,cert.encryptedD+"A",{});
    check(execute(db,prepared,password,true).error==Error::Changed&&protectedOriginal(doc),"changed certificate refuses old confirmation scope");db.setUserCert(cert.n,cert.e,cert.encryptedD,{});
    put(prepared.archive,encrypted+"changed");check(execute(db,prepared,password,true).error==Error::Changed&&read(prepared.archive)==encrypted+"changed","changed stored bytes are not overwritten by a prepared conversion");put(prepared.archive,encrypted);
    for(const char* step:{"prepared","metadata","publish","release"}){
        doc=create();prepared=prepare(db,doc);const auto result=execute(db,prepared,password,true,{},[step](const char* at){return QByteArray(at)!=step;});
        check(!result&&!result.backup.isEmpty()&&read(result.backup)==encrypted&&protectedOriginal(doc),"ordinary injected conversion failure preserves full encrypted backup and rolls original file/metadata back");
    }
    doc=create();prepared=prepare(db,doc);
    const auto failedRestore=execute(db,prepared,password,true,{},[](const char* at){return QByteArray(at)!="release"&&QByteArray(at)!="restore";});
    check(failedRestore.error==Error::RecoveryRequired&&failedRestore.published&&read(failedRestore.backup)==encrypted,"failed automatic restore reports recovery required and retains complete encrypted backup");
    put(prepared.archive,encrypted); // disposable failure fixture only
    doc=create();prepared=prepare(db,doc);doc.nReadCount=17;const bool ancillaryUpdated=db.modifyDocumentReadCount(doc);WizDatabase::clearCertPassword();int notifications=0,metadataNotifications=0;
    const auto metadataChanged=QObject::connect(&db,&WizDatabase::documentModified,&app,[&](const WIZDOCUMENTDATA& old,const WIZDOCUMENTDATA& value){if(old.nProtected&&!value.nProtected&&value.strGUID==doc.strGUID)++metadataNotifications;});
    const auto changed=QObject::connect(&db,&WizDatabase::documentDataModified,&app,[&](const WIZDOCUMENTDATA& value){if(value.strGUID==doc.strGUID&&!value.nProtected)++notifications;});
    const auto result=execute(db,prepared,password,true);WIZDOCUMENTDATA converted;db.documentFromGuid(doc.strGUID,converted);
    check(bool(result)&&result.published&&!converted.nProtected&&ancillaryUpdated&&converted.nReadCount==17&&read(prepared.archive)==original&&read(result.backup)==encrypted,"known password conversion preserves latest ancillary metadata publishes byte-identical original ZIP and retains encrypted original");
    check(notifications==1&&metadataNotifications==1&&db.getCertPassword().isEmpty()&&!prepare(db,converted).valid,"success emits one post-commit metadata/body update without populating shared password cache or converting twice");QObject::disconnect(changed);QObject::disconnect(metadataChanged);
    QString n,e,d,h;db.getUserCert(n,e,d,h);check(n==cert.n&&e==cert.e&&d==cert.encryptedD,"permanent note conversion never deletes or replaces certificate");
    check(QDir(QFileInfo(prepared.archive).absolutePath()).entryList({".unprotect-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty(),"temporary plaintext ZIP staging is released after ordinary completion");
    // Actual saver fence and shared UI workflow; prompts are synthetic functions,
    // not native dialogs. No password is requested from the user or logged.
    WizDocumentSaverThread saver(manager,nullptr);QWidget owner;int prompts=0,confirms=0,reports=0;Error lastReported=Error::None;
    auto services=WizUnprotectDialog::Services{
        [&](QWidget*,bool* accepted){++prompts;*accepted=true;return password;},
        [&](QWidget*,const QString& folder){++confirms;return folder.endsWith("/encrypted-backups");},
        [&](QWidget*,const Result& value){++reports;lastReported=value.error;}};
    doc=create();db.setUserCert({}, {}, {}, {});
    auto missingCertificate=WizUnprotectDialog::run(&owner,db,doc,&saver,services);
    check(!missingCertificate&&missingCertificate.error==Error::CertificateUnavailable&&lastReported==Error::CertificateUnavailable&&protectedOriginal(doc)
          &&prompts==0&&confirms==0&&reports==1,
          "missing local certificate is reported distinctly before requesting a password without changing the encrypted original");
    db.setUserCert("not-a-certificate",cert.e,cert.encryptedD,{});
    const auto unusableCertificate=WizUnprotectDialog::run(&owner,db,doc,&saver,services);
    QString unavailableN,unavailableE,unavailableD,unavailableHint;db.getUserCert(unavailableN,unavailableE,unavailableD,unavailableHint);
    check(unusableCertificate.error==Error::CertificateUnavailable&&prompts==0&&confirms==0&&reports==2
          &&unavailableN=="not-a-certificate"&&protectedOriginal(doc),
          "unusable existing certificate is not silently replaced and never starts password or conversion work");
    db.setUserCert(cert.n,cert.e,cert.encryptedD,{});reports=0;
    doc=create();auto canceled=services;canceled.password=[&](QWidget*,bool* accepted){++prompts;*accepted=false;return QString();};
    check(WizUnprotectDialog::run(&owner,db,doc,&saver,canceled).error==Error::ConsentRequired&&protectedOriginal(doc)&&confirms==0&&reports==0,"password cancel preserves original and does not request confirmation");
    canceled=services;canceled.confirm=[&](QWidget*,const QString&){++confirms;return false;};
    check(WizUnprotectDialog::run(&owner,db,doc,&saver,canceled).error==Error::ConsentRequired&&protectedOriginal(doc)&&reports==0,"explicit confirmation refusal never converts or reports success");
    auto invalid=services;invalid.password=[](QWidget*,bool* accepted){*accepted=true;return QString("wrong");};
    check(WizUnprotectDialog::run(&owner,db,doc,&saver,invalid).error==Error::PasswordInvalid&&protectedOriginal(doc)&&reports==1,"shared UI uses fresh password verification after actual save fence");
    check(lastReported==Error::PasswordInvalid&&WizUnprotectDialog::resultMessage({Error::PasswordInvalid,{},false}).contains("账户登录密码"),
          "wrong certificate password has specific guidance without claiming account-password failure");
    auto stale=services;stale.confirm=[&](QWidget*,const QString&){db.setMeta("Account","GUID","changed-during-prompt");return true;};
    check(WizUnprotectDialog::run(&owner,db,doc,&saver,stale).error==Error::Changed&&protectedOriginal(doc),"scope changed inside modal confirmation refuses publication");db.setMeta("Account","GUID",originalAccount);
    bool sameOwnerNote=true;stale=services;stale.contextCurrent=[&]{return sameOwnerNote;};stale.confirm=[&](QWidget*,const QString&){sameOwnerNote=false;return true;};
    check(WizUnprotectDialog::run(&owner,db,doc,&saver,stale).error==Error::Changed&&protectedOriginal(doc),"changed information-window note refuses conversion after modal confirmation");
    auto* transient=new QWidget;auto destroyed=services;destroyed.password=[&](QWidget*,bool* accepted){delete transient;*accepted=true;return password;};
    check(!WizUnprotectDialog::run(transient,db,doc,&saver,destroyed)&&protectedOriginal(doc),"owner destroyed in password callback cannot continue confirmation or conversion");
    auto missing=doc;missing.strGUID="missing-synthetic-note";saver.save(missing,"ignored",{},0);const int beforePrompts=prompts,beforeReports=reports;
    const auto failedSave=WizUnprotectDialog::run(&owner,db,missing,&saver,services);
    check(failedSave.error==Error::SaveFailed&&prompts==beforePrompts&&saver.hasUnresolvedSaveFailure(),"actual failed save frontier prevents password prompt and preserves outstanding failure");
    check(reports==beforeReports+1&&lastReported==Error::SaveFailed,"failed save frontier produces one failure report rather than silently returning");
    check(bool(WizUnprotectDialog::run(&owner,db,doc,&saver,services))&&read(db.getDocumentFileName(doc.strGUID))==original,"shared dialog completes known-password conversion with actual saver and isolated database");
    const QString retainedPath=root.filePath("retained-encrypted-original.ziw");
    for(const auto error:{Error::PasswordInvalid,Error::Unsupported,Error::Changed,Error::ReadFailed,Error::BackupFailed,Error::WriteFailed,Error::MetadataFailed,Error::RecoveryRequired}){
        const auto message=WizUnprotectDialog::resultMessage({error,retainedPath,false});
        check(message.contains(retainedPath)&&!message.contains(password)&&!message.contains(cert.encryptedD),
              "each conversion failure keeps its actual backup path visible without revealing password or encrypted key");
    }
    check(WizUnprotectDialog::resultMessage({Error::CertificateUnavailable,{},false}).contains("不会自动创建或替换")
          &&WizUnprotectDialog::resultMessage({Error::SaveFailed,{},false}).contains("不会取消待保存任务")
          &&WizUnprotectDialog::resultMessage({Error::RecoveryRequired,retainedPath,true}).contains("不要继续编辑或同步"),
          "certificate,save and incomplete-rollback messages preserve their distinct user actions");
    saver.waitForDone();WizDatabase::clearCertPassword();manager.closeAll();
    qInfo()<<"Document unprotect checks"<<checks<<"failures"<<failures;return failures?1:0;
}
