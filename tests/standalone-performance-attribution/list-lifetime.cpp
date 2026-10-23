// Real production list/item objects with a temporary metadata-only database.
// No normal application entry, account, editor, network or user data is used.
#include <QtWidgets>
#include "WizDef.h"
#include "share/WizQtHelper.h"
#include "share/WizSettings.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#define private public
#define protected public
#include "gui/doclistviewer/WizDocumentListView.h"
#include "gui/doclistviewer/WizDocumentListViewDocumentItem.h"
#undef protected
#undef private
#include "share/WizGlobal.h"
#include "WizThumbCache.h"
#include "sync/WizAvatarHost.h"

class SyntheticLifetimeApp final:public WizExplorerApp {
    WizDatabaseManager& manager;
    WizUserSettings settings;
public:
    explicit SyntheticLifetimeApp(WizDatabaseManager& m):manager(m),settings(m.db()){}
    QWidget* mainWindow()override{return nullptr;}
    QObject* object()override{return qApp;}
    WizDatabaseManager& databaseManager()override{return manager;}
    WizCategoryBaseView& category()override{qFatal("Unrequested category access");}
    WizUserSettings& userSettings()override{return settings;}
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
    WizDatabaseManager manager("synthetic-list-lifetime-only");if(!manager.open())return 2;
    SyntheticLifetimeApp api(manager);WizThumbCache thumbs;WizAvatarHost avatars;
    int checks=0,failures=0,destroyed=0;
    const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
    WIZDOCUMENTDATA doc;if(!check(manager.db().createDocument("Synthetic list lifetime","","/Synthetic/","",0,doc),"create only synthetic metadata in temporary database"))return 3;
    QList<QPointer<WizDocumentListViewDocumentItem>> observed;
    const auto drain=[](){QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCoreApplication::processEvents();};
    const auto alive=[&](){int n=0;for(const auto& p:observed)n+=!p.isNull();return n;};
    int liveAfterRemoval=0,liveAfterViewDestruction=0;const int cycles=300;
    {
        WizDocumentListView list(api);list.setAttribute(Qt::WA_DontShowOnScreen);
        const auto add=[&](){
            list.addDocument(WIZDOCUMENTDATAEX(doc));
            auto* item=list.documentItemAt(list.documentIndexFromGUID(doc.strGUID));
            observed.append(item);QObject::connect(item,&QObject::destroyed,&app,[&](){++destroyed;});
            return item;
        };
        auto* item=add();QPointer<WizDocumentListViewDocumentItem> first(item);
        list.m_rightButtonFocusedItems.append(item);
        const auto beforeCount=list.documentCount();
        // Exercise the real manager signal -> list slot connection.
        emit manager.documentDeleted(doc);
        check(beforeCount==1&&list.documentCount()==0,"real manager deletion signal removes the visible row");
        check(first&&first->listWidget()==nullptr,"removal detaches before deferred destruction so active callers can unwind");
        check(list.m_rightButtonFocusedItems.isEmpty()==optimized,"unselected right-click reference is cleared only by the fix");
        drain();check(first.isNull()==optimized,"removed production item is released after normal deferred-delete boundary");
        emit manager.documentDeleted(doc);drain();
        check(list.documentCount()==0,"repeated notification for an absent row is harmless");
        item=add();item->setSelected(true);emit manager.documentDeleted(doc);drain();
        check(list.selectedItems().isEmpty()&&list.m_rightButtonFocusedItems.isEmpty(),"selected deletion preserves selection cleanup");
        for(int i=0;i<cycles;i++){
            item=add();item->m_data.thumb.image=QImage(128,128,QImage::Format_ARGB32_Premultiplied);
            item->m_data.thumb.image.fill(qRgb(i%256,83,127));
            emit manager.documentDeleted(doc);drain();
        }
        liveAfterRemoval=alive();
        check(liveAfterRemoval==(optimized?0:cycles+2),"300 repeated removals do not retain list items or their unique image payloads after the fix");
        check(list.documentCount()==0&&list.m_sectionItems.isEmpty(),"empty list leaves no date sections");
        item=add();QPointer<WizDocumentListViewDocumentItem> clearing(item);
        list.m_rightButtonFocusedItems.clear();list.m_rightButtonFocusedItems.append(item);
        list.clearAllItems();
        check(clearing.isNull()&&list.documentCount()==0,"whole-list refresh still destroys attached items");
        check(list.m_rightButtonFocusedItems.isEmpty()==optimized,"whole-list refresh clears an unselected context reference");
        list.m_rightButtonFocusedItems.clear();
        // Preserve a survivor while another row is removed and sections rebuild.
        WIZDOCUMENTDATA other=doc;other.strGUID="synthetic-survivor";other.strTitle="Survivor";
        list.addDocument(WIZDOCUMENTDATAEX(other));
        auto* survivor=list.documentItemAt(list.documentIndexFromGUID(other.strGUID));
        QPointer<WizDocumentListViewDocumentItem> survivorGuard(survivor);survivor->setSelected(true);
        item=add();emit manager.documentDeleted(doc);drain();
        check(survivorGuard&&survivorGuard->isSelected()&&list.documentCount()==1,"unrelated selected row remains alive and selected");
        check(list.m_sectionItems.size()==1,"date section is rebuilt for the surviving row");
        const int rows=list.count();WIZDOCUMENTDATA absent=doc;absent.strGUID="absent";
        emit manager.documentDeleted(absent);drain();
        check(list.count()==rows&&survivorGuard,"absent notification does not alter surviving rows or sections");
#ifdef WIZ_LIST_LIFETIME_EXTRA
        list.removeDocumentItem(-1);list.removeDocumentItem(list.count());
        check(list.count()==rows&&survivorGuard,"invalid removal indices leave documents unchanged");
        auto* section=list.m_sectionItems.first();
        list.removeDocumentItem(list.row(section));
        check(list.m_sectionItems.first()==section&&list.count()==rows,"document removal helper cannot detach a section");
        item=add();QPointer<WizDocumentListViewDocumentItem> blocked(item);
        list.m_rightButtonFocusedItems.clear();list.m_rightButtonFocusedItems.append(survivor);list.m_rightButtonFocusedItems.append(item);
        list.blockSignals(true);emit manager.documentDeleted(doc);list.blockSignals(false);
        check(blocked&&list.m_rightButtonFocusedItems.size()==1&&list.m_rightButtonFocusedItems.first()==survivor,"blocked selection signals still remove exactly the deleted menu alias");
        list.on_menu_aboutToHide();drain();
        check(!blocked&&survivorGuard&&survivor->isSelected(),"menu close after deferred destruction leaves the selected survivor valid");
        // Qt foreach snapshots the menu list; removals may mutate its original.
        auto* a=add();QPointer<WizDocumentListViewDocumentItem> aGuard(a);
        list.m_rightButtonFocusedItems.clear();list.m_rightButtonFocusedItems.append(a);
        list.m_rightButtonFocusedItems.append(survivor);
        bool snapshotValid=true;int visited=0;
        list.blockSignals(true);
        foreach(auto* active,list.m_rightButtonFocusedItems){
            ++visited;list.removeDocumentItem(list.row(active));
            snapshotValid&=active->listWidget()==nullptr&&!active->document().strGUID.isEmpty();
        }
        list.blockSignals(false);
        check(visited==2&&snapshotValid,"active menu snapshot can unwind after each row is detached");
        check(list.documentCount()==0&&list.m_rightButtonFocusedItems.isEmpty(),"all detached menu aliases are cleared with signals blocked");
        drain();check(!aGuard&&!survivorGuard,"both menu snapshot objects are subsequently destroyed");
#endif
        list.clearAllItems();drain();
        check(!survivorGuard&&list.m_sectionItems.isEmpty(),"whole-list cleanup releases surviving rows and sections");
    }
    drain();liveAfterViewDestruction=alive();
    check(liveAfterViewDestruction==(optimized?0:cycles+3),"no detached items survive list destruction after the fix");
    // The baseline intentionally exposes missing ownership; reclaim only this
    // fixture's known detached objects so diagnostics do not leave a real leak.
    for(const auto& p:observed)if(p)delete p.data();drain();
    check(alive()==0&&destroyed==observed.size(),"fixture reclaims all baseline observations without touching user objects");
    WIZDOCUMENTDATA preserved;
    check(manager.db().documentFromGuid(doc.strGUID,preserved)&&preserved.strTitle==doc.strTitle&&preserved.strLocation==doc.strLocation,"UI removal never deletes or changes database metadata");
    bool hidden=true;for(auto* window:QApplication::topLevelWidgets())hidden&=!window->isVisible();
    check(hidden,"no foreground window or normal client opened");
    qInfo().noquote()<<"LIST_LIFETIME_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"optimized",optimized},{"cycles",cycles},{"liveAfterRemoval",liveAfterRemoval},{"liveAfterViewDestruction",liveAfterViewDestruction},{"imagePayloadPerCycleBytes",128*128*4},{"syntheticOnly",true}}).toJson(QJsonDocument::Compact));
    return failures?1:0;
}
