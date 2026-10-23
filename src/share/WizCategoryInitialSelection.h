#pragma once
#include <QItemSelectionModel>
#include <QTreeWidget>

// One tree instance owns one initialization cycle, independent of group counts
// or other windows. Late group callbacks must not replace the user's selection.
class WizCategoryInitialSelection
{
public:
    void begin() { m_building=true; m_restored=false; }
    void finish() { m_building=false; }
    bool claimRestore() {
        if(m_building||m_restored)return false;
        m_restored=true;return true;
    }
    static void ensureSelection(QTreeWidget* tree,QTreeWidgetItem* fallback) {
        if(!tree||!fallback||fallback->treeWidget()!=tree||!tree->selectedItems().isEmpty())return;
        tree->setCurrentItem(fallback,0,QItemSelectionModel::ClearAndSelect);
    }
private:
    bool m_building=true;
    bool m_restored=false;
};
