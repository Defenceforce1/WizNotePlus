// Actual production item/database objects, disposable schema and synthetic data.
// Test-only access allows observing the existing SQLite connection and cache;
// no production API is exposed and no ordinary application entry is linked.
#include <QtWidgets>
#include <functional>
#include "WizDef.h"
#include "share/WizQtHelper.h"
#include "share/WizObject.h"
#define private public
#define protected public
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "gui/doclistviewer/WizDocumentListViewDocumentItem.h"
#undef protected
#undef private
#include "gui/doclistviewer/WizDocumentListView.h"
#include "share/WizGlobal.h"

class SyntheticListApp final:public WizExplorerApp {
    WizDatabaseManager& manager;
public:
    explicit SyntheticListApp(WizDatabaseManager& m):manager(m){}
    QWidget* mainWindow()override{return nullptr;}
    QObject* object()override{return qApp;}
    WizDatabaseManager& databaseManager()override{return manager;}
    WizCategoryBaseView& category()override{qFatal("Unrequested synthetic category access");}
    WizUserSettings& userSettings()override{qFatal("Unrequested synthetic user settings access");}
};

int main(int argc,char** argv){
    const bool optimized=argc==2&&QByteArray(argv[1])=="--optimized";
    if(!optimized&&!(argc==2&&QByteArray(argv[1])=="--baseline"))return 4;
    QTemporaryDir root;if(!root.isValid())return 2;
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_PluginApplication);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QSettings settings(root.filePath("ui.ini"),QSettings::IniFormat),global(root.filePath("global.ini"),QSettings::IniFormat);
    WizGlobal::setSettings(&settings);WizGlobal::setGlobalSettings(&global);
    WizDatabaseManager manager("synthetic-tag-cache-only");if(!manager.open())return 2;
    auto& db=manager.db();SyntheticListApp api(manager);int checks=0,failures=0,reads=0;
    const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
    QString sentinel;check(sentinel.isNull()&&sentinel.isEmpty(),"default tag cache is unloaded");
    sentinel=QStringLiteral("");check(!sentinel.isNull()&&sentinel.isEmpty(),"empty literal distinguishes a loaded empty result");
    sentinel.clear();check(sentinel.isNull(),"existing clear invalidation returns the null sentinel");
    WIZDOCUMENTDATA doc;
    if(!check(db.createDocument("Synthetic tag cache","","/Synthetic/","",0,doc),"create metadata-only synthetic document in temporary database"))return 3;
    WizDocumentListViewItemData data{};data.nType=WizDocumentListViewDocumentItem::TypePrivateDocument;
    static_cast<WIZDOCUMENTDATA&>(data.doc)=doc;
    sqlite3* connection=db.m_db.mpDB;
    check(sqlite3_trace_v2(connection,SQLITE_TRACE_STMT,[](unsigned,void* context,void* statement,void*){
        const QString sql=QString::fromUtf8(sqlite3_sql(static_cast<sqlite3_stmt*>(statement)));
        if(sql.contains("from WIZ_TAG",Qt::CaseInsensitive)&&sql.contains("WIZ_DOCUMENT_TAG",Qt::CaseInsensitive))++*static_cast<int*>(context);
        return 0;
    },&reads)==SQLITE_OK,"observe only local synthetic tag-select counts");
    int emptyReads=0,reloadReads=0,transitionReads=0,removedReads=0;double elapsedMs=0;
    {
        WizDocumentListViewDocumentItem item(api,data);
        reads=0;bool empty=true;QElapsedTimer timer;timer.start();
        for(int i=0;i<500;i++)empty&=item.tags().isEmpty();elapsedMs=timer.nsecsElapsed()/1e6;emptyReads=reads;
        check(empty,"500 repeated tagless reads preserve empty display");
        check(reads==(optimized?1:500),"successful empty result has the expected baseline/optimized query count");
        reads=0;item.reload(db);for(int i=0;i<500;i++)empty&=item.tags().isEmpty();reloadReads=reads;
        check(empty&&reads==(optimized?1:501),"document reload invalidates and reloads empty tags exactly as expected");
        item.setLeadInfoState(DocumentLeadInfo_PersonalRoot);reads=0;
        for(int i=0;i<50;i++)empty&=item.tags().isEmpty();
        check(empty&&reads==0,"personal root suppresses tags without querying");
        item.setLeadInfoState(DocumentLeadInfo_PersonalFolder);reads=0;
        for(int i=0;i<500;i++)empty&=item.tags().isEmpty();transitionReads=reads;
        check(empty&&reads==(optimized?1:500),"returning from hidden root invalidates the existing cache");
        int reloadSignals=0;
        const auto connection=QObject::connect(&manager,&WizDatabaseManager::documentModified,&item,
            [&](const WIZDOCUMENTDATA&,const WIZDOCUMENTDATA& changed){if(changed.strGUID==doc.strGUID){++reloadSignals;item.reload(db);}});
        WIZTAGDATA tag;check(db.createTag("","Synthetic tag","",tag)&&db.insertDocumentTag(doc,tag.strGUID),"add a synthetic tag to the same metadata document");
        check(reloadSignals==1&&!item.tags().isEmpty(),"actual manager modification signal invalidates the formerly empty cache");
        QObject::disconnect(connection);
        const QString expected=db.getDocumentTagDisplayNameText(doc.strGUID);reads=0;bool same=!expected.isEmpty();
        for(int i=0;i<500;i++)same&=item.tags()==expected;
        check(same&&reads==0,"nonempty display and existing positive cache semantics are unchanged");
        check(db.deleteDocumentTags(doc),"remove synthetic tags without touching note body");
        reads=0;item.reload(db);for(int i=0;i<500;i++)empty&=item.tags().isEmpty();removedReads=reads;
        check(empty&&reads==(optimized?1:501),"removing the last tag reloads and caches the successful empty display");
        item.m_strTags.clear();const bool begun=db.execSQL("BEGIN");
        const bool renamed=begun&&db.execSQL("ALTER TABLE WIZ_TAG RENAME TO WIZ_TAG_TEMPORARILY_UNAVAILABLE");
        check(renamed,"temporary local transaction simulates unavailable tag storage");
        if(renamed){
            check(item.tags().isEmpty()&&item.m_strTags.isNull(),"failed query is not cached as success");
            check(item.tags().isEmpty()&&item.m_strTags.isNull(),"second failed query still permits retry");
        }
        check(begun&&db.execSQL("ROLLBACK"),"restore the original temporary schema transactionally");
        reads=0;const bool recovered=item.tags().isEmpty();
        check(recovered&&reads==1,"first post-failure request retries the actual database successfully");
        reads=0;check(item.tags().isEmpty()&&reads==(optimized?0:1),"successful recovery follows normal empty-cache behavior");
        check(QApplication::allWindows().isEmpty(),"no main or foreground window created");
    }
    sqlite3_trace_v2(connection,0,nullptr,nullptr);
    qInfo().noquote()<<"TAG_CACHE_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"optimized",optimized},{"emptyReads",emptyReads},{"reloadReads",reloadReads},{"transitionReads",transitionReads},{"removedReads",removedReads},{"empty500ElapsedMs",elapsedMs},{"syntheticOnly",true},{"normalClientEntryLinked",false}}).toJson(QJsonDocument::Compact));
    return failures?1:0;
}
