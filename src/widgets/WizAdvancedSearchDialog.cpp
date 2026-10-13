#include "WizAdvancedSearchDialog.h"
#include "ui_WizAdvancedSearchDialog.h"
#include <QCursor>
#include <QLineEdit>
#include <QGroupBox>
#include <QMessageBox>
#include <QPainter>
#include <QButtonGroup>
#include <QDebug>
#include <QComboBox>
#include <QSet>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "share/WizMisc.h"
#include "WizNoteStyle.h"
#include "utils/WizStyleHelper.h"

#define PARAM_SELECT_PARAM    QObject::tr("Select search param")
#define PARAM_SELECT_FOLDER  QObject::tr("Folder")
#define PARAM_SELECT_TAG          QObject::tr("Tag")
#define PARAM_SELECT_CONTAIN  QObject::tr("Contains")
#define PARAM_SELECT_DTCREATED  QObject::tr("Created time")
#define PARAM_SELECT_DTMODIFIED  QObject::tr("Modified time")
#define PARAM_SELECT_DTACCESSED  QObject::tr("Accessed time")

#define PARAM_CONTAIN_ATTACHMENT  QObject::tr("Attachment")

#define PARAM_DATE_BEFORE   QObject::tr("Before")
#define PARAM_DATE_AFTER    QObject::tr("After")

#define PARAM_DATE_TODAY              QObject::tr("Today")
#define PARAM_DATE_YESTODAY         QObject::tr("Yestoday")
#define PARAM_DATE_DAYBEFOREYESTODAY            QObject::tr("Day before yestoday")
#define PARAM_DATE_LASTWEEK         QObject::tr("Last week")
#define PARAM_DATE_LASTMONTH        QObject::tr("Last month")
#define PARAM_DATE_LASTYEAR         QObject::tr("Last year")

#define PARAM_FOLDER_CHILDFOLDER QObject::tr("--Select child folder--")

#define PARAM_NAME      "name:"
#define PARAM_SCOPE     "scope:"
#define PARAM_KEYWORD       "keyword:"
#define PARAM_PARAM     "param:"

namespace {
const QString tagPrefix = QStringLiteral("tag-guid:");
const QString nameEncoded = QStringLiteral("name-encoded:");
const QString keywordEncoded = QStringLiteral("keyword-encoded:");
QString escapeLike(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("%", "\\%");
    value.replace("_", "\\_");
    return value;
}
QString encodeField(const QString& key, const QString& value)
{
    return value.contains('/') ? key.left(key.size() - 1) + "-encoded:" + QString::fromLatin1(QUrl::toPercentEncoding(value))
                               : key + value;
}
bool readTag(const QString& token, QString& guid, QString& label)
{
    const QString value = token.mid(tagPrefix.size());
    const int separator = value.indexOf(':');
    guid = value.left(separator);
    static const QRegularExpression uuid(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    if (!token.startsWith(tagPrefix) || separator < 0 || !uuid.match(guid).hasMatch())
        return false;
    label = QUrl::fromPercentEncoding(value.mid(separator + 1).toLatin1());
    return true;
}
}



WizAdvancedSearchDialog::WizAdvancedSearchDialog(bool searchOnly, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::WizAdvancedSearchDialog)
  , m_radioGroup(new QButtonGroup(this))
{
    ui->setupUi(this);
    // activated(QString) was removed in Qt 6. The index signal works on Qt 5 and 6.
    connect(ui->comboBox_first, qOverload<int>(&QComboBox::activated), this,
            [this](int i) { onFirstParameterActivated(ui->comboBox_first->itemText(i)); });
    connect(ui->comboBox_second, qOverload<int>(&QComboBox::activated), this,
            [this](int i) { onSecondParameterActivated(ui->comboBox_second->itemText(i)); });

    ui->lineEdit_keyword->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->lineEdit_name->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listWidget->setAttribute(Qt::WA_MacShowFocusRect, false);

    WizListItemStyle<WizSearchParamItem>* listStyle = new WizListItemStyle<WizSearchParamItem>();
    ui->listWidget->setStyle(listStyle);

    ui->lineEdit_name->setVisible(!searchOnly);
    ui->label->setVisible(!searchOnly);
    if (!searchOnly) {
        ui->lineEdit_name->setText(tr("Untitled Search"));
        setWindowTitle(tr("Custom advanced search"));
    }
    else {
        setWindowTitle(tr("Advanced search"));
    }
    ui->lineEdit_keyword->setPlaceholderText(tr("Multiple keywords should be separated by blank"));

    ui->comboBox_second->setVisible(false);
    ui->comboBox_third->setVisible(false);

    ui->radioButton_personalNotes->setChecked(true);
    initFirstCombox(false);

    m_radioGroup->addButton(ui->radioButton_allNotes, 0);
    m_radioGroup->addButton(ui->radioButton_personalNotes, 1);
    m_radioGroup->addButton(ui->radioButton_groupNotes, 2);
    connect(m_radioGroup, SIGNAL(buttonClicked(QAbstractButton*)),
            SLOT(onRadioButtonClicked(QAbstractButton*)));

    ui->listWidget->setStyleSheet(Utils::WizStyleHelper::wizCommonListViewStyleSheet());    

    //
//    ui->label_5->setVisible(false);
//    ui->groupBox->setVisible(false);
}

WizAdvancedSearchDialog::~WizAdvancedSearchDialog()
{
    delete ui;
}

QString WizAdvancedSearchDialog::getParams()
{
    QString params = encodeField(PARAM_NAME, ui->lineEdit_name->text()) + "/"
            + encodeField(PARAM_KEYWORD, ui->lineEdit_keyword->text()) + "/"
            + PARAM_SCOPE + QString::number(m_radioGroup->checkedId());
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        const auto* item = ui->listWidget->item(i);
        const QString token = item->data(Qt::UserRole).toString();
        params += "/" + (token.isEmpty() ? PARAM_PARAM + item->text() : token);
    }
    return params;
}

void WizAdvancedSearchDialog::setParams(const QString& params)
{
    ui->listWidget->clear();
    ui->lineEdit_keyword->clear();
    ui->radioButton_personalNotes->setChecked(true);
    for (const QString& part : params.split('/'))
    {
        if (part.startsWith(PARAM_NAME) || part.startsWith(PARAM_KEYWORD)
            || part.startsWith(nameEncoded) || part.startsWith(keywordEncoded))
        {
            const bool isName = part.startsWith(PARAM_NAME) || part.startsWith(nameEncoded);
            const bool encoded = part.startsWith(nameEncoded) || part.startsWith(keywordEncoded);
            const QString prefix = encoded ? (isName ? nameEncoded : keywordEncoded)
                                           : QString(isName ? PARAM_NAME : PARAM_KEYWORD);
            QString value = part.mid(prefix.size());
            if (encoded)
                value = QUrl::fromPercentEncoding(value.toLatin1());
            (isName ? ui->lineEdit_name : ui->lineEdit_keyword)->setText(value);
        }
        else if (part.startsWith(PARAM_SCOPE))
        {
            bool ok = false;
            const int id = part.mid(QString(PARAM_SCOPE).size()).toInt(&ok);
            if (ok && m_radioGroup->button(id))
                m_radioGroup->button(id)->setChecked(true);
        }
        else if (part.startsWith(PARAM_PARAM))
            new WizSearchParamItem(part.mid(QString(PARAM_PARAM).size()), ui->listWidget);
        else if (part.startsWith(tagPrefix))
        {
            QString guid, label;
            if (readTag(part, guid, label))
            {
                auto* item = new WizSearchParamItem(PARAM_SELECT_TAG + ", " + label, ui->listWidget);
                item->setData(Qt::UserRole, part);
            }
        }
    }
    initFirstCombox(m_radioGroup->checkedId() != 1);
}

bool WizAdvancedSearchDialog::paramToSQL(const QString& params, QString& sqlWhere,
                                       QString& keyword, QString& name, int& scope)
{
    sqlWhere.clear();
    keyword.clear();
    name.clear();
    scope = 1;
    bool hasTag = false;
    QStringList conditions;
    for (const QString& part : params.split('/'))
    {
        if (part.startsWith(PARAM_NAME) || part.startsWith(PARAM_KEYWORD)
            || part.startsWith(nameEncoded) || part.startsWith(keywordEncoded))
        {
            const bool isName = part.startsWith(PARAM_NAME) || part.startsWith(nameEncoded);
            const bool encoded = part.startsWith(nameEncoded) || part.startsWith(keywordEncoded);
            const QString prefix = encoded ? (isName ? nameEncoded : keywordEncoded)
                                           : QString(isName ? PARAM_NAME : PARAM_KEYWORD);
            QString value = part.mid(prefix.size());
            if (encoded)
                value = QUrl::fromPercentEncoding(value.toLatin1());
            (isName ? name : keyword) = value;
        }
        else if (part.startsWith(PARAM_SCOPE))
        {
            bool ok = false;
            scope = part.mid(QString(PARAM_SCOPE).size()).toInt(&ok);
            if (!ok || scope < 0 || scope > 2)
                return false;
        }
        else if (part.startsWith(tagPrefix))
        {
            QString guid, label;
            if (!readTag(part, guid, label))
                return false;
            conditions << "DOCUMENT_GUID IN (SELECT DOCUMENT_GUID FROM WIZ_DOCUMENT_TAG WHERE TAG_GUID="
                          + STR2SQL(guid) + ")";
            hasTag = true;
        }
        else if (part.startsWith(PARAM_PARAM))
        {
            const QStringList values = part.mid(QString(PARAM_PARAM).size()).split(',');
            if (values.size() < 2)
                return false;
            if (values.first() == PARAM_SELECT_FOLDER)
            {
                if (values.size() > 3 || values.at(1).isEmpty())
                    return false;
                QString path = "/" + values.at(1) + "/";
                if (values.size() == 3 && !values.at(2).isEmpty())
                    path += values.at(2) + "/";
                conditions << "DOCUMENT_LOCATION LIKE " + STR2SQL(escapeLike(path) + "%") + " ESCAPE '\\'";
            }
            else if (values.first() == PARAM_SELECT_CONTAIN)
            {
                if (values.at(1) != PARAM_CONTAIN_ATTACHMENT)
                    return false;
                conditions << "DOCUMENT_ATTACHEMENT_COUNT > 0";
            }
            else if (values.first() == PARAM_SELECT_DTCREATED
                     || values.first() == PARAM_SELECT_DTMODIFIED
                     || values.first() == PARAM_SELECT_DTACCESSED)
            {
                const QStringList dates = {PARAM_DATE_TODAY, PARAM_DATE_YESTODAY, PARAM_DATE_DAYBEFOREYESTODAY,
                                            PARAM_DATE_LASTWEEK, PARAM_DATE_LASTMONTH, PARAM_DATE_LASTYEAR};
                if (values.size() != 3 || !dates.contains(values.last())
                    || (values.at(1) != PARAM_DATE_AFTER && values.at(1) != PARAM_DATE_BEFORE))
                    return false;
                QString condition = values.first() == PARAM_SELECT_DTCREATED ? "DT_CREATED"
                        : values.first() == PARAM_SELECT_DTMODIFIED ? "DT_MODIFIED" : "DT_ACCESSED";
                condition += values.at(1) == PARAM_DATE_AFTER ? " > " : " < ";
                conditions << condition + ::WizTimeToSQL(getDateTimeByInterval(values.last()));
            }
            else
                return false;
        }
        else
            return false;
    }
    if (hasTag && scope != 1)
        return false;
    sqlWhere = conditions.join(" AND ");
    return true;
}

void WizAdvancedSearchDialog::onRadioButtonClicked(QAbstractButton* button)
{
    const bool personal = button == ui->radioButton_personalNotes;
    if (!personal)
    {
        for (int i = ui->listWidget->count() - 1; i >= 0; --i)
        {
            const auto* item = ui->listWidget->item(i);
            if (item->data(Qt::UserRole).toString().startsWith(tagPrefix)
                || item->text().startsWith(PARAM_SELECT_FOLDER + ","))
                delete ui->listWidget->takeItem(i);
        }
    }
    initFirstCombox(!personal);
    ui->comboBox_second->clear();
    ui->comboBox_second->setVisible(false);
    ui->comboBox_third->setVisible(false);
}

void WizAdvancedSearchDialog::getDateList(QStringList& dateList)
{
    dateList.clear();
    dateList.append(PARAM_DATE_TODAY);
    dateList.append(PARAM_DATE_YESTODAY);
    dateList.append(PARAM_DATE_DAYBEFOREYESTODAY);
    dateList.append(PARAM_DATE_LASTWEEK);
    dateList.append(PARAM_DATE_LASTMONTH);
    dateList.append(PARAM_DATE_LASTYEAR);
}

void WizAdvancedSearchDialog::getFirstLevelFolders(QStringList& folders)
{
    if (m_strFolders.isEmpty())
    {
        m_strFolders = WizDatabaseManager::instance()->db().getFolders();
    }

    QStringList folderList = m_strFolders.split('*', Qt::SkipEmptyParts);
    foreach (QString strFolder, folderList) {
        if (strFolder.count('/') == 2)
        {
            strFolder.remove('/');
            folders.append(strFolder);
        }
    }    
}

void WizAdvancedSearchDialog::getSecondLevelFolders(const QString& firstLevelFolder, QStringList& folders)
{
    if (m_strFolders.isEmpty())
    {
        m_strFolders = WizDatabaseManager::instance()->db().getFolders();
    }

    QStringList folderList = m_strFolders.split('*', Qt::SkipEmptyParts);
    foreach (QString strFolder, folderList) {
        if (strFolder.startsWith("/" + firstLevelFolder) && strFolder.count('/') == 3)
        {
            strFolder.remove("/");
            strFolder.remove(0, firstLevelFolder.length());
            folders.append(strFolder);
        }
    }
}

void WizAdvancedSearchDialog::populateTags()
{
    CWizTagDataArray tags;
    WizDatabaseManager::instance()->db().getAllTags(tags);
    QMap<QString, WIZTAGDATA> byGuid;
    for (const auto& tag : tags)
        byGuid.insert(tag.strGUID, tag);
    QList<QPair<QString, QString>> choices;
    for (const auto& tag : tags)
    {
        QStringList path;
        QSet<QString> seen;
        QString guid = tag.strGUID;
        while (!guid.isEmpty() && byGuid.contains(guid) && !seen.contains(guid))
        {
            seen.insert(guid);
            const auto& current = byGuid[guid];
            path.prepend(WizDatabase::tagNameToDisplayName(current.strName));
            guid = current.strParentGUID;
        }
        choices.append(qMakePair(path.join(" / "), tag.strGUID));
    }
    std::sort(choices.begin(), choices.end(), [](const QPair<QString, QString>& a,
                                               const QPair<QString, QString>& b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    for (const auto& choice : choices)
        ui->comboBox_second->addItem(choice.first, choice.second);
    ui->comboBox_second->setVisible(!choices.isEmpty());
}

WizOleDateTime WizAdvancedSearchDialog::getDateTimeByInterval(const QString& str)
{
    WizOleDateTime dt;
    if (str == PARAM_DATE_TODAY)
    {
        dt = dt.addDays(-1);
    }
    else if (str == PARAM_DATE_YESTODAY)
    {
        dt = dt.addDays(-2);
    }
    else if (str == PARAM_DATE_DAYBEFOREYESTODAY)
    {
        dt = dt.addDays(-3);
    }
    else if (str == PARAM_DATE_LASTWEEK)
    {
        dt = dt.addDays(-8);
    }
    else if (str == PARAM_DATE_LASTMONTH)
    {
        dt = dt.addMonths(-1);
    }
    else if (str == PARAM_DATE_LASTYEAR)
    {
        dt = dt.addYears(-1);
    }
    return dt;
}

void WizAdvancedSearchDialog::onFirstParameterActivated(const QString &arg1)
{
    ui->comboBox_second->setVisible(true);
    ui->comboBox_third->setVisible(false);
    ui->comboBox_second->clear();
    ui->comboBox_third->clear();
    if (arg1 == PARAM_SELECT_PARAM)
    {
        ui->comboBox_second->setVisible(false);
    }
    else if (arg1 == PARAM_SELECT_FOLDER)
    {
        QStringList folders;
        getFirstLevelFolders(folders);
        ui->comboBox_second->addItems(folders);

        //
        ui->comboBox_third->clear();

        if (folders.count() > 0)
        {
            QString strFirstFolder = folders.first();
            folders.clear();
            getSecondLevelFolders(strFirstFolder, folders);
            if (folders.count() > 0)
            {
                ui->comboBox_third->clear();
                ui->comboBox_third->addItem(PARAM_FOLDER_CHILDFOLDER);
                ui->comboBox_third->addItems(folders);
                ui->comboBox_third->setVisible(true);
                ui->comboBox_third->setCurrentIndex(0);
            }
        }
    }
    else if (arg1 == PARAM_SELECT_TAG)
    {
        populateTags();
    }

    else if (arg1 == PARAM_SELECT_CONTAIN)
    {
        ui->comboBox_second->addItem(PARAM_CONTAIN_ATTACHMENT);
    }
    else if (arg1 == PARAM_SELECT_DTCREATED || arg1 == PARAM_SELECT_DTMODIFIED
             || arg1 == PARAM_SELECT_DTACCESSED)
    {
        ui->comboBox_second->addItem(PARAM_DATE_BEFORE);
        ui->comboBox_second->addItem(PARAM_DATE_AFTER);
        ui->comboBox_second->setCurrentIndex(0);

        QStringList dateList;
        getDateList(dateList);
        ui->comboBox_third->setVisible(true);
        ui->comboBox_third->addItems(dateList);
        ui->comboBox_third->setCurrentIndex(0);
    }
}

void WizAdvancedSearchDialog::initFirstCombox(bool bSearchGroup)
{
    ui->comboBox_first->clear();

    ui->comboBox_first->addItem(PARAM_SELECT_PARAM);
    if (!bSearchGroup)
    {
        ui->comboBox_first->addItem(PARAM_SELECT_FOLDER);
        ui->comboBox_first->addItem(PARAM_SELECT_TAG);
    }
    ui->comboBox_first->addItem(PARAM_SELECT_CONTAIN);
    ui->comboBox_first->addItem(PARAM_SELECT_DTCREATED);
    ui->comboBox_first->addItem(PARAM_SELECT_DTMODIFIED);
    ui->comboBox_first->addItem(PARAM_SELECT_DTACCESSED);
    ui->comboBox_first->setCurrentIndex(0);
}

void WizAdvancedSearchDialog::onSecondParameterActivated(const QString &arg1)
{
    ui->comboBox_third->setVisible(false);
    QString firstParam = ui->comboBox_first->currentText();
    if (firstParam == PARAM_SELECT_FOLDER)
    {
        ui->comboBox_third->clear();

        QStringList folders;
        getSecondLevelFolders(arg1, folders);
        if (folders.count() > 0)
        {
            ui->comboBox_third->clear();
            ui->comboBox_third->addItem(PARAM_FOLDER_CHILDFOLDER);
            ui->comboBox_third->addItems(folders);
            ui->comboBox_third->setVisible(true);
            ui->comboBox_third->setCurrentIndex(0);
        }
    }
    else if (firstParam == PARAM_SELECT_DTCREATED || firstParam == PARAM_SELECT_DTMODIFIED
             || firstParam == PARAM_SELECT_DTACCESSED)
    {
        ui->comboBox_third->setVisible(true);
    }
}

void WizAdvancedSearchDialog::on_pushButton_cancel_clicked()
{
    reject();
}

void WizAdvancedSearchDialog::on_pushButton_ok_clicked()
{
    if (ui->listWidget->count() == 0 && ui->lineEdit_keyword->text().isEmpty())
    {
        QMessageBox::information(0, tr("Info"), tr("Both of keywords and search param were empty!"));
        return;
    }
    QString where, keyword, name;
    int scope;
    if (!paramToSQL(getParams(), where, keyword, name, scope))
    {
        QMessageBox::warning(this, tr("Info"), tr("Invalid search conditions."));
        return;
    }
    accept();
}

void WizAdvancedSearchDialog::on_toolButton_add_clicked()
{
    const QString first = ui->comboBox_first->currentText();
    if (first == PARAM_SELECT_PARAM || ui->comboBox_second->currentIndex() < 0)
        return;
    const QString second = ui->comboBox_second->currentText();
    QString third = !ui->comboBox_third->isHidden() ? ui->comboBox_third->currentText() : QString();
    if (third == PARAM_FOLDER_CHILDFOLDER)
        third.clear();
    QString token, label = first + "," + second + "," + third;
    if (first == PARAM_SELECT_TAG)
    {
        const QString guid = ui->comboBox_second->currentData().toString();
        if (guid.isEmpty())
            return;
        token = tagPrefix + guid + ":" + QString::fromLatin1(QUrl::toPercentEncoding(second));
        label = first + ", " + second;
    }
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        const auto* item = ui->listWidget->item(i);
        if (token.isEmpty() ? item->text() == label : item->data(Qt::UserRole).toString() == token)
            return;
    }
    auto* item = new WizSearchParamItem(label, ui->listWidget);
    item->setData(Qt::UserRole, token);
}

WizSearchParamItem::WizSearchParamItem(const QString& text, QListWidget* view, int type)
    : QListWidgetItem(text, view, type)
{
    QString strThemeName = Utils::WizStyleHelper::themeName();

    m_pix = QPixmap(::WizGetSkinResourceFileName(strThemeName, "listItem_delete"));
}

void WizSearchParamItem::draw(QPainter* p, const QStyleOptionViewItem* vopt) const
{
    p->save();

    bool bSelected = vopt->state & QStyle::State_Selected;
    bool bFocused = listWidget()->hasFocus();
    drawItemBackground(p, vopt->rect, bSelected, bFocused);

    QPen pen;
    bSelected ? pen.setColor(Qt::white) : pen.setColor(Qt::black);
    p->setPen(pen);
    p->drawText(vopt->rect, Qt::AlignLeft | Qt::AlignVCenter, text());

     p->restore();
}

QRect WizSearchParamItem::drawItemBackground(QPainter* p, const QRect& rect, bool selected, bool focused) const
{
    QBrush brush;
    selected ? brush.setColor(Qt::blue) : brush.setColor(Qt::white);
    brush.setStyle(Qt::SolidPattern);
    p->setPen(Qt::NoPen);
    p->setBrush(brush);
    p->drawRect(rect);


    if (selected)
    {
        QRect pixRect(rect.right() - m_pix.width(), rect.top() + (rect.height() - m_pix.height()) / 2,
                      m_pix.width(), m_pix.height());
        p->drawPixmap(pixRect, m_pix);
    }
    return rect;
}

bool WizSearchParamItem::removeIconClicked()
{
    QPoint pos = QCursor::pos();
    pos = listWidget()->mapFromGlobal(pos);
    QRect border = listWidget()->visualItemRect(this);
    QRect pixRect(border.right() - m_pix.width(), border.top() + (border.height() - m_pix.height()) / 2,
                  m_pix.width(), m_pix.height());
    return pixRect.contains(pos);
}

void WizAdvancedSearchDialog::on_listWidget_itemClicked(QListWidgetItem *item)
{
    WizSearchParamItem* paramItem = dynamic_cast<WizSearchParamItem*>(item);
    Q_ASSERT(paramItem);
    if (paramItem)
    {
          if (paramItem->removeIconClicked())
          {
              delete ui->listWidget->takeItem(ui->listWidget->row(item));
          }
    }
}


