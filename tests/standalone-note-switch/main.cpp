#include "share/WizDocumentAsync.h"
#include <QCoreApplication>
#include <QPointer>
#include <QDateTime>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDebug>
#include <functional>
#include <memory>
#include <vector>
struct WIZDOCUMENTDATA {QString strGUID="old",strKbGUID="synthetic-kb",strTitle;int nProtected=0,nReadCount=0;QDateTime tAccessed;};
using WIZDOCUMENTDATAEX=WIZDOCUMENTDATA;
static QString archivePath;
struct Database {bool group=false,downloaded=true;bool isGroup(){return group;}QString getDocumentFileName(QString){return archivePath;}bool isObjectDataDownloaded(QString,QString){return downloaded;}bool isEncryptAllData(){return false;}bool initCert(bool){return true;}bool loadUserCert(){return true;}void modifyDocumentReadCount(WIZDOCUMENTDATA&){}void modifyDocumentDateAccessed(WIZDOCUMENTDATA&){} };
using WizDatabase=Database;
struct Manager {Database database;Database& db(QString){return database;}};
struct WizZiwReader {static bool isEncryptedFile(QString){return false;}};
bool WizPathFileExists(QString name){return QFileInfo::exists(name);}QDateTime WizGetCurrentTime(){return QDateTime::currentDateTime();}
struct Web {std::vector<std::function<void(const QVariant&)>> replies;QString dom="unsaved original DOM";int continuations=0;quint64 pendingLoadSequence(){return 7;}void cancelLinkInsert(){}void cancelPdfExport(){}void trySaveDocument(const WIZDOCUMENTDATA&,bool,std::function<void(const QVariant&)> done){replies.push_back(std::move(done));}void complete(int index,bool result){auto callback=std::move(replies.at(index));callback(result);}void continuePendingLoadAfterFailedOpen(quint64){continuations++;}void clear(){}void isPersonalDocumentChanged(){}void hasEditPermissionOnCurrentNoteChanged(){} };
struct Stack {void setCurrentWidget(void*){}};
class WizDocumentView:public QObject {public:
 Web storage;Web* m_web=&storage;Manager m_dbMgr;Stack stack;Stack* m_stack=&stack;void* m_transitionView=nullptr;void* m_docView=nullptr;void* m_blankView=nullptr;
 WizDocumentCallbackEpoch m_openEpoch;quint64 m_openSequence=10,m_noteSwitchSequence=0;bool m_accessClosing=false,m_noteLoaded=true,m_biometricReloadPending=false,m_encryptedAccessEnabled=false;
 WIZDOCUMENTDATAEX m_note;int revocations=0,loads=0;std::function<void()> reentrantRevoke;
 void revokeBiometricAccess(bool,bool,bool){revocations++;m_openEpoch.invalidate();if(reentrantRevoke){auto action=std::move(reentrantRevoke);reentrantRevoke={};action();}}
 void stopDocumentEditingStatus(){}void initStat(const WIZDOCUMENTDATA&,bool){}void downloadNoteFromServer(const WIZDOCUMENTDATA&){}void showCertificateUnavailable(){}bool prepareEncryptedAccess(Database&){return false;}void loadNote(const WIZDOCUMENTDATAEX& x){loads++;m_note=x;m_noteLoaded=true;}void titleChanged(QString){}
 void viewNote(const WIZDOCUMENTDATAEX&,bool);
};
#include "view-note.inc"
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTemporaryFile file;if(!file.open())return 2;archivePath=file.fileName();int checks=0,failures=0;auto check=[&](bool ok,const char* message){checks++;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<message;};WIZDOCUMENTDATAEX next,latest;next.strGUID="next";latest.strGUID="latest";
 {WizDocumentView v;const auto live=v.m_openEpoch.ticket();v.viewNote(next,false);check(v.m_openSequence==10&&WizDocumentCallbackEpoch::current(live)&&v.revocations==0,"switch request preserves current page initialization and authorization until save completes");v.storage.complete(0,false);check(v.m_openSequence==10&&WizDocumentCallbackEpoch::current(live)&&v.revocations==0&&v.loads==0&&v.m_note.strGUID=="old"&&v.storage.dom=="unsaved original DOM","failed save preserves original document, pending setup and unsaved DOM");v.viewNote(next,false);v.storage.complete(1,true);check(v.loads==1&&v.revocations==1&&v.m_openSequence==11&&v.m_note.strGUID=="next"&&!WizDocumentCallbackEpoch::current(live),"successful retry commits one new document identity and revokes old access");}
 {WizDocumentView v;v.viewNote(next,false);v.viewNote(latest,false);v.storage.complete(0,true);check(v.loads==0&&v.revocations==0,"late save from superseded request cannot commit its destination");v.storage.complete(1,true);check(v.loads==1&&v.m_note.strGUID=="latest"&&v.revocations==1,"only latest pending request commits after successful save");}
 {WizDocumentView v;v.viewNote(next,false);v.viewNote(latest,false);v.storage.complete(1,false);v.storage.complete(0,true);check(v.loads==0&&v.m_note.strGUID=="old"&&v.revocations==0,"failed latest request cannot resurrect older successful destination");}
 {WizDocumentView v;v.viewNote(next,false);v.m_accessClosing=true;v.m_openEpoch.invalidate();v.storage.complete(0,true);check(v.loads==0&&v.revocations==0,"close or current-page retirement suppresses pending navigation without cancelling save");}
 {auto v=std::make_unique<WizDocumentView>();v->viewNote(next,false);auto callback=std::move(v->storage.replies[0]);v.reset();callback(true);check(true,"owner destruction before save reply is safe");}
 {WizDocumentView v;v.m_noteLoaded=false;v.viewNote(next,false);v.storage.complete(0,false);check(v.loads==1&&v.m_note.strGUID=="next","initial blank host still opens its first note without a nonexistent snapshot");}
 {WizDocumentView v;v.viewNote(next,false);v.reentrantRevoke=[&]{v.viewNote(latest,false);};v.storage.complete(0,true);check(v.loads==0&&v.m_note.strGUID=="old","new request during commit revocation cancels obsolete remaining UI steps");v.storage.complete(1,true);check(v.loads==1&&v.m_note.strGUID=="latest","reentrant latest request can complete its own commit");}
 qInfo()<<"NOTE_SWITCH_CHECKS"<<checks<<"FAILURES"<<failures;return failures?1:0;}
