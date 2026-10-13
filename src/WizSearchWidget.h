#ifndef WIZSEARCHWIDGET_H
#define WIZSEARCHWIDGET_H

#include <QtGlobal>
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QMap>
#include <QTimer>

class QCompleter;
class QStandardItemModel;
class QListWidget;
class QComboBox;
class QCheckBox;

// Wrap widget for qt widget and mac native search field
// refer to mac/wizSearchWidget.mm for osx NSSearchField

class WizExplorerApp;

class WizSearchEdit : public QLineEdit
{
    Q_OBJECT
public:
    WizSearchEdit(QWidget* parent = 0);
    void setFilterPanel(QWidget* panel);
    void hideFilterMenu();

public slots:
    void on_actionAdvancedSearch();
    void on_addCustomSearch();

signals:
    void advanceSearchRequest();
    void addCustomSearchRequest();

protected:
    void paintEvent(QPaintEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);

private:
    QRect searchIconRect() const;
    QRect deleteIconRect() const;
    QPixmap m_searchIcon;
    QPixmap m_deleteIcon;
    QMenu* m_menu;
};

class WizSearchView : public QWidget
{
    Q_OBJECT

public:
    WizSearchView(QWidget* parent = 0);
    void clear();
    void focus();
    QString keyword() const;
    bool hasFilters() const;
    QString queryParams() const;
    bool setQueryParams(const QString& params);
    void setAvailableTags(const QList<QPair<QString, QString>>& labelGuids);
    void setAvailableFolders(const QStringList& paths);
    // The main window places filters in a separate row below the toolbar,
    // keeping the actual search field aligned with the other toolbar controls.
    QWidget* takeFilterChipRow();

    virtual QSize sizeHint() const;
    //
    void setCurrentKb(const QString& kbGuid) { m_strCurrentKbGuid = kbGuid; }
    QString currentKb() const { return m_strCurrentKbGuid; }


public Q_SLOTS:
    void on_search_returnPressed();
    void on_searchTextChanged(QString str);
    void setWidthHint(int nWidth);

private:
    void rebuildChips();
    void refreshFilterPanel();
    void updateTagSuggestions();
    void selectTag(const QString& guid, bool consumeText);
    void scheduleSearch();
    void clearFilters();
    bool eventFilter(QObject* watched, QEvent* event) override;
    WizSearchEdit* m_editSearch;
    QWidget* m_chipRow;
    QHBoxLayout* m_chipLayout;
    QCompleter* m_tagCompleter;
    QStandardItemModel* m_tagModel;
    QListWidget* m_panelTags;
    QLineEdit* m_panelTagFilter;
    QComboBox* m_panelFolder;
    QCheckBox* m_panelAttachments;
    QMap<QString, QString> m_tagLabels;
    QStringList m_tagOrder;
    QStringList m_selectedTags;
    QStringList m_folderChoices;
    QString m_folder;
    QStringList m_extraConditions;
    bool m_withAttachments = false;
    QTimer m_suggestionTimer;
    QTimer m_submitTimer;
    int m_widthHint;
    QString m_strCurrentKbGuid;

Q_SIGNALS:
    void advancedSearchRequest();
    void addCustomSearchRequest();
    void doSearch(const QString& keywords);
};



#endif // WIZSEARCHWIDGET_H
