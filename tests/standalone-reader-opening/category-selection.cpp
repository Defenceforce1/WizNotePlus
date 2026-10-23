#include "share/WizCategoryInitialSelection.h"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
int main(int argc,char**argv){
 QApplication app(argc,argv);int checks=0,failed=0;const auto check=[&](bool ok,const char* label){++checks;failed+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
 WizCategoryInitialSelection first,second;
 check(!first.claimRestore(),"group completion during construction does not restore an incomplete tree");
 first.begin();check(!first.claimRestore(),"new cycle defers restoration until tree ready");first.finish();check(first.claimRestore(),"zero-group tree restores at initialization completion");check(!first.claimRestore(),"late group notifications never restore again");
 check(!second.claimRestore(),"second window does not inherit a global completion counter");second.finish();check(second.claimRestore(),"second window restores independently");first.begin();first.finish();check(first.claimRestore()&&!first.claimRestore(),"rebuild gets one new restoration");
 QTreeWidget tree;int selected=0;QObject::connect(&tree,&QTreeWidget::itemSelectionChanged,[&]{++selected;});
 WizCategoryInitialSelection::ensureSelection(&tree,nullptr);check(tree.topLevelItemCount()==0&&selected==0,"empty tree never creates a folder");
 auto* all=new QTreeWidgetItem(&tree,QStringList{"All notes"});auto* folder=new QTreeWidgetItem(all,QStringList{"Synthetic folder"});
 WizCategoryInitialSelection::ensureSelection(&tree,all);check(tree.currentItem()==all&&tree.selectedItems()==QList<QTreeWidgetItem*>{all}&&selected==1,"missing saved ID selects all-notes root and emits list-loading signal");
 WizCategoryInitialSelection::ensureSelection(&tree,all);check(selected==1,"fallback is idempotent");tree.setCurrentItem(folder);const int manual=selected;
 WizCategoryInitialSelection::ensureSelection(&tree,all);check(tree.currentItem()==folder&&tree.selectedItems().contains(folder)&&selected==manual,"valid restored or user-selected folder is preserved");
 tree.clearSelection();check(tree.currentItem()==folder&&tree.selectedItems().isEmpty(),"Qt current index can outlive cleared selection");WizCategoryInitialSelection::ensureSelection(&tree,all);check(tree.currentItem()==all&&tree.selectedItems().contains(all),"empty selection with stale current index gets explicit fallback");
 QTreeWidget other;auto* foreign=new QTreeWidgetItem(&other,QStringList{"Other tree"});tree.clearSelection();WizCategoryInitialSelection::ensureSelection(&tree,foreign);check(tree.selectedItems().isEmpty(),"foreign-tree fallback ignored");
 check(tree.topLevelItemCount()==1&&all->childCount()==1,"restore never creates renames or removes folders");check(!tree.isVisible()&&!other.isVisible(),"no window exposed");
 qInfo().noquote()<<QJsonDocument(QJsonObject{{"accepted",!failed},{"checks",checks},{"failures",failed},{"accountOrDataUsed",false}}).toJson(QJsonDocument::Compact);return failed?1:0;
}
