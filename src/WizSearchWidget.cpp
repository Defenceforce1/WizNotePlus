#include "WizSearchWidget.h"

#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QMouseEvent>
#include <QDebug>

#include "share/WizSettings.h"
#include "share/WizAnalyzer.h"
#include "WizDef.h"
#include "utils/WizStyleHelper.h"

WizSearchView::WizSearchView(QWidget* parent /* = 0 */)
    : QWidget(parent)
    , m_widthHint(360)
{
    QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setSizePolicy(sizePolicy);
    setContentsMargins(1, 0, 1, 1);

    m_editSearch = new WizSearchEdit(this);
    m_editSearch->setObjectName("wiz-note-search-field");
    m_editSearch->setPlaceholderText(QStringLiteral("搜索笔记内容…"));
    m_editSearch->setToolTip(QStringLiteral("搜索标题和正文；点击左侧放大镜，在菜单中选择“高级搜索”可按标签筛选。"));
    m_editSearch->setTextMargins(25, 1, 0, 1);

    m_editSearch->setStyleSheet(
        "QLineEdit { color:#333333; background-color:#ffffff; border:1px solid #b8b8b8; border-radius:8px;"
        "selection-background-color:#3377cc; selection-color:#ffffff; }"
        "QLineEdit:focus { border:1px solid #3377cc; }");


    // avoid focus rect on OSX, this should be a bug of qt style sheet
    m_editSearch->setAttribute(Qt::WA_MacShowFocusRect, 0);    

    QHBoxLayout* layout = new QHBoxLayout(this);
    setLayout(layout);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editSearch);
    layout->setStretch(1, 1);


    connect(m_editSearch, SIGNAL(returnPressed()), \
            SLOT(on_search_returnPressed()));
    connect(m_editSearch, SIGNAL(textChanged(QString)), \
            SLOT(on_searchTextChanged(QString)), Qt::QueuedConnection);
    connect(m_editSearch, SIGNAL(addCustomSearchRequest()),
            SIGNAL(addCustomSearchRequest()));
    connect(m_editSearch, &WizSearchEdit::advanceSearchRequest,
            this, &WizSearchView::advancedSearchRequest);
}

void WizSearchView::clear()
{
    m_editSearch->clear();
}

void WizSearchView::focus()
{
    m_editSearch->selectAll();
    m_editSearch->setFocus();
}

QSize WizSearchView::sizeHint() const
{
    return QSize(m_widthHint, height());
}

void WizSearchView::setWidthHint(int nWidth)
{
    m_widthHint = nWidth;
}

void WizSearchView::on_search_returnPressed()
{
    Q_EMIT doSearch(m_editSearch->text());
}

void WizSearchView::on_searchTextChanged(QString str)
{
//    if (str.isEmpty())
//    {
//        Q_EMIT doSearch("");
    //    }
}


WizSearchEdit::WizSearchEdit(QWidget* parent)
    : QLineEdit(parent)
    , m_menu(new QMenu(this))
{    
    QString strSearchIcon = Utils::WizStyleHelper::skinResourceFileName("mactoolbarsearch", true);
    m_searchIcon = QPixmap(strSearchIcon);
    QString strDeleteIcon = Utils::WizStyleHelper::skinResourceFileName("mactoolbardelete", true);
    m_deleteIcon = QPixmap(strDeleteIcon);

    m_menu->setObjectName("wiz-note-search-menu");
    auto* advanced = m_menu->addAction(tr("Advanced search"), this, SLOT(on_actionAdvancedSearch()));
    advanced->setObjectName("actionAdvancedSearch");
    m_menu->addAction(tr("Add custom search"), this, SLOT(on_addCustomSearch()));

}

void WizSearchEdit::on_actionAdvancedSearch()
{
    WizGetAnalyzer().logAction("advancedSearchOnSearchWidget");
    emit advanceSearchRequest();
}

void WizSearchEdit::on_addCustomSearch()
{
    WizGetAnalyzer().logAction("customSearchOnSearchWidget");
    emit addCustomSearchRequest();
}

QRect WizSearchEdit::searchIconRect() const
{
    QSize size = m_searchIcon.size();
    WizScaleIconSizeForRetina(size);
    return QRect(QPoint(4, (height() - size.height()) / 2 + 1), size);
}

QRect WizSearchEdit::deleteIconRect() const
{
    QSize size = m_deleteIcon.size();
    WizScaleIconSizeForRetina(size);
    return QRect(QPoint(width() - size.width() - 4, (height() - size.height()) / 2), size);
}

void WizSearchEdit::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);

    QPainter pt(this);
    pt.drawPixmap(searchIconRect(), m_searchIcon);

    if (!text().isEmpty())
    {
        pt.drawPixmap(deleteIconRect(), m_deleteIcon);
    }
}

void WizSearchEdit::mousePressEvent(QMouseEvent* event)
{
    // Events use local logical coordinates, just like the painted Retina icon.
    const QRect rcBtnSearch = searchIconRect();
    if (rcBtnSearch.contains(event->pos()))
    {
        event->accept();
        m_menu->popup(mapToGlobal(event->pos()));
        clearFocus();
        return;
    }

    if (!text().isEmpty())
    {
        const QRect rect = deleteIconRect();
        if (rect.contains(event->pos()))
        {
            setText("");
            //send returnPress signal to reset search status
            emit returnPressed();
        }
    }
    QLineEdit::mousePressEvent(event);
}

void WizSearchEdit::mouseMoveEvent(QMouseEvent* event)
{
    QLineEdit::mouseMoveEvent(event);

    const QRect rcBtnSearch = searchIconRect();
    if (rcBtnSearch.contains(event->pos()))
    {
        if (cursor().shape() != Qt::ArrowCursor)
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
    else
    {
        if (!text().isEmpty())
        {
            const QRect rect = deleteIconRect();
            if (rect.contains(event->pos()))
            {
                if (cursor().shape() != Qt::ArrowCursor)
                {
                    setCursor(QCursor(Qt::ArrowCursor));
                }
                return;
            }
        }

        if (cursor().shape() != Qt::IBeamCursor)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
    }
}
