#include "WizSearchWidget.h"

#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QMouseEvent>
#include <QDebug>
#include <QCompleter>
#include <QAbstractItemView>
#include <QStandardItemModel>
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QWidgetAction>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QUrl>
#include "widgets/WizAdvancedSearchDialog.h"

#include "share/WizSettings.h"
#include "share/WizAnalyzer.h"
#include "WizDef.h"
#include "utils/WizStyleHelper.h"

WizSearchView::WizSearchView(QWidget* parent /* = 0 */)
    : QWidget(parent)
    , m_widthHint(360)
{
    setObjectName("wiz-note-search-view");
    QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setSizePolicy(sizePolicy);
    setContentsMargins(1, 0, 1, 1);

    m_editSearch = new WizSearchEdit(this);
    m_editSearch->setObjectName("wiz-note-search-field");
    m_editSearch->setPlaceholderText(QStringLiteral("搜索内容或选择标签…"));
    m_editSearch->setToolTip(QStringLiteral("直接回车搜索内容；点选标签提示后，再输入内容关键词。放大镜可筛选标签、文件夹和附件。"));
    // Reserve room for both painted controls; long keywords must not overlap
    // the clear icon. Keep drawing and pointer targets in logical coordinates.
    m_editSearch->setTextMargins(32, 0, 28, 0);
    m_editSearch->setFixedHeight(30);
    QFont searchFont = m_editSearch->font();
    searchFont.setPointSizeF(13);
    m_editSearch->setFont(searchFont);

    m_editSearch->setStyleSheet(
        "QLineEdit { color:#263445; background-color:#f5f7fa; border:1px solid #cbd3df; border-radius:9px;"
        "padding:0px;"
        "selection-background-color:#3377cc; selection-color:#ffffff; }"
        "QLineEdit:hover { border-color:#aab8ca; }"
        "QLineEdit:focus { background-color:#ffffff; border-color:#6b9bd3; }");
    QPalette searchPalette = m_editSearch->palette();
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    searchPalette.setColor(QPalette::PlaceholderText, QColor("#7b8798"));
#endif
    m_editSearch->setPalette(searchPalette);


    // avoid focus rect on OSX, this should be a bug of qt style sheet
    m_editSearch->setAttribute(Qt::WA_MacShowFocusRect, 0);    

    QVBoxLayout* layout = new QVBoxLayout(this);
    setLayout(layout);
    layout->setSpacing(3);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editSearch);
    m_chipRow = new QWidget(this);
    m_chipRow->setObjectName("wiz-note-search-chips");
    m_chipLayout = new QHBoxLayout(m_chipRow);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(3);
    layout->addWidget(m_chipRow);
    m_chipRow->hide();

    auto* panel = new QWidget;
    panel->setObjectName("wiz-note-filter-panel");
    panel->setFixedWidth(340);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(12, 10, 12, 10);
    panelLayout->setSpacing(7);
    panelLayout->addWidget(new QLabel(QStringLiteral("筛选个人笔记"), panel));
    m_panelTagFilter = new QLineEdit(panel);
    m_panelTagFilter->setObjectName("wiz-note-panel-tag-filter");
    m_panelTagFilter->setPlaceholderText(QStringLiteral("找标签…"));
    panelLayout->addWidget(m_panelTagFilter);
    m_panelTags = new QListWidget(panel);
    m_panelTags->setObjectName("wiz-note-panel-tags");
    m_panelTags->setFixedHeight(132);
    panelLayout->addWidget(m_panelTags);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(new QLabel(QStringLiteral("文件夹"), panel));
    m_panelFolder = new QComboBox(panel);
    m_panelFolder->setObjectName("wiz-note-panel-folder");
    m_panelFolder->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_panelFolder->setMinimumContentsLength(15);
    folderRow->addWidget(m_panelFolder, 1);
    panelLayout->addLayout(folderRow);
    m_panelAttachments = new QCheckBox(QStringLiteral("仅含附件"), panel);
    m_panelAttachments->setObjectName("wiz-note-panel-attachments");
    panelLayout->addWidget(m_panelAttachments);
    auto* hint = new QLabel(QStringLiteral("多个标签需同时匹配；选择后立即搜索"), panel);
    hint->setStyleSheet("color:#707070;");
    panelLayout->addWidget(hint);
    auto* clear = new QPushButton(QStringLiteral("清除筛选"), panel);
    clear->setObjectName("wiz-note-panel-clear");
    panelLayout->addWidget(clear);
    m_editSearch->setFilterPanel(panel);
    connect(m_editSearch->findChild<QMenu*>("wiz-note-search-menu"), &QMenu::aboutToShow,
            this, [this] { m_tagCompleter->popup()->hide(); refreshFilterPanel(); });
    connect(m_panelTagFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_panelTags->count(); ++i)
            m_panelTags->item(i)->setHidden(!m_panelTags->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(m_panelTags, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const QString guid = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked) {
            if (!m_selectedTags.contains(guid)) m_selectedTags.append(guid);
        } else m_selectedTags.removeAll(guid);
        rebuildChips(); scheduleSearch();
    });
    connect(m_panelFolder, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        m_folder = m_panelFolder->itemData(index).toString();
        rebuildChips(); scheduleSearch();
    });
    connect(m_panelAttachments, &QCheckBox::toggled, this, [this](bool checked) {
        m_withAttachments = checked; rebuildChips(); scheduleSearch();
    });
    connect(clear, &QPushButton::clicked, this, [this] { clearFilters(); scheduleSearch(); });

    m_tagModel = new QStandardItemModel(this);
    m_tagCompleter = new QCompleter(m_tagModel, this);
    m_tagCompleter->setCompletionRole(Qt::UserRole + 1);
    m_tagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_tagCompleter->setFilterMode(Qt::MatchContains);
    m_tagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_tagCompleter->setMaxVisibleItems(8);
    m_tagCompleter->setWidget(m_editSearch);
    m_tagCompleter->popup()->setObjectName("wiz-note-tag-suggestions");
    m_tagCompleter->popup()->setStyleSheet("QAbstractItemView { color:#333333; background:white;"
        "selection-background-color:#3377cc; selection-color:white; border:1px solid #b8b8b8; }");
    m_editSearch->installEventFilter(this);
    m_tagCompleter->popup()->installEventFilter(this);
    connect(m_tagCompleter, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) { selectTag(index.data(Qt::UserRole).toString(), true); });
    m_suggestionTimer.setSingleShot(true);
    m_suggestionTimer.setInterval(120);
    connect(&m_suggestionTimer, &QTimer::timeout, this, &WizSearchView::updateTagSuggestions);
    connect(m_editSearch, &QLineEdit::textEdited, this, [this] { m_suggestionTimer.start(); });
    m_submitTimer.setSingleShot(true);
    connect(&m_submitTimer, &QTimer::timeout, this, &WizSearchView::on_search_returnPressed);


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
    m_submitTimer.stop(); m_suggestionTimer.stop();
    m_tagCompleter->popup()->hide(); m_editSearch->hideFilterMenu();
    m_editSearch->clear();
    clearFilters();
}

void WizSearchView::focus()
{
    m_editSearch->selectAll();
    m_editSearch->setFocus();
}

QSize WizSearchView::sizeHint() const
{
    return QSize(m_widthHint, QWidget::sizeHint().height());
}

QWidget* WizSearchView::takeFilterChipRow()
{
    layout()->removeWidget(m_chipRow);
    m_chipLayout->setContentsMargins(12, 0, 12, 6);
    return m_chipRow;
}

void WizSearchView::setWidthHint(int nWidth)
{
    m_widthHint = nWidth;
}

void WizSearchView::on_search_returnPressed()
{
    m_submitTimer.stop(); m_suggestionTimer.stop();
    m_tagCompleter->popup()->hide(); m_editSearch->hideFilterMenu();
    Q_EMIT doSearch(m_editSearch->text());
}

void WizSearchView::on_searchTextChanged(QString str)
{
    if (str.isEmpty()) m_tagCompleter->popup()->hide();
}

QString WizSearchView::keyword() const { return m_editSearch->text(); }

bool WizSearchView::hasFilters() const
{
    return !m_selectedTags.isEmpty() || !m_folder.isEmpty() || m_withAttachments || !m_extraConditions.isEmpty();
}

QString WizSearchView::queryParams() const
{
    const QString text = keyword();
    QStringList parts = {QStringLiteral("name:组合搜索"),
        text.contains('/') ? "keyword-encoded:" + QString::fromLatin1(QUrl::toPercentEncoding(text)) : "keyword:" + text,
        QStringLiteral("scope:1")};
    for (const QString& guid : m_selectedTags)
        parts << "tag-guid:" + guid + ":" + QString::fromLatin1(QUrl::toPercentEncoding(m_tagLabels.value(guid, guid)));
    if (!m_folder.isEmpty())
        parts << "folder-path-encoded:" + QString::fromLatin1(QUrl::toPercentEncoding(m_folder));
    if (m_withAttachments)
        parts << "param:" + QObject::tr("Contains") + "," + QObject::tr("Attachment") + ",";
    parts << m_extraConditions;
    return parts.join('/');
}

bool WizSearchView::setQueryParams(const QString& params)
{
    QString where, text, name; int scope = 1;
    if (!WizAdvancedSearchDialog::paramToSQL(params, where, text, name, scope) || scope != 1) {
        clear(); return false;
    }
    clear();
    m_editSearch->setText(text);
    for (const QString& part : params.split('/')) {
        if (part.startsWith("tag-guid:")) {
            const QString value = part.mid(9);
            const int separator = value.indexOf(':');
            const QString guid = value.left(separator);
            if (!m_selectedTags.contains(guid)) m_selectedTags.append(guid);
            if (!m_tagLabels.contains(guid))
                m_tagLabels.insert(guid, QUrl::fromPercentEncoding(value.mid(separator + 1).toLatin1()));
        } else if (part.startsWith("folder-path-encoded:"))
            m_folder = QUrl::fromPercentEncoding(part.mid(20).toLatin1());
        else if (part.startsWith("param:")) {
            const QStringList values = part.mid(6).split(',');
            if (values.first() == QObject::tr("Contains") && values.value(1) == QObject::tr("Attachment"))
                m_withAttachments = true;
            else m_extraConditions.append(part);
        }
    }
    rebuildChips(); refreshFilterPanel();
    return true;
}

void WizSearchView::setAvailableTags(const QList<QPair<QString, QString>>& labelGuids)
{
    QMap<QString, QString> labels;
    for (const QString& guid : m_selectedTags) labels.insert(guid, m_tagLabels.value(guid, guid));
    m_tagLabels = labels;
    m_tagOrder.clear();
    for (const auto& choice : labelGuids) {
        if (choice.second.isEmpty() || m_tagOrder.contains(choice.second)) continue;
        m_tagLabels.insert(choice.second, choice.first); m_tagOrder.append(choice.second);
    }
    m_tagModel->clear();
    for (const QString& guid : m_tagOrder) {
        if (m_selectedTags.contains(guid)) continue;
        auto* item = new QStandardItem(QStringLiteral("标签：") + m_tagLabels.value(guid));
        item->setData(guid, Qt::UserRole);
        item->setData(m_tagLabels.value(guid), Qt::UserRole + 1);
        m_tagModel->appendRow(item);
    }
    rebuildChips(); refreshFilterPanel();
}

void WizSearchView::setAvailableFolders(const QStringList& paths)
{
    m_folderChoices = paths; m_folderChoices.removeDuplicates();
    m_folderChoices.sort(Qt::CaseInsensitive);
    refreshFilterPanel();
}

void WizSearchView::refreshFilterPanel()
{
    const QSignalBlocker blockTags(m_panelTags), blockFolders(m_panelFolder), blockAttachment(m_panelAttachments);
    m_panelTags->clear();
    QStringList all = m_tagOrder;
    for (const QString& guid : m_selectedTags) if (!all.contains(guid)) all.append(guid);
    for (const QString& guid : all) {
        const QString label = m_tagLabels.value(guid, guid)
            + (m_tagOrder.contains(guid) ? QString() : QStringLiteral("（已删除）"));
        auto* item = new QListWidgetItem(label, m_panelTags);
        item->setData(Qt::UserRole, guid);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_selectedTags.contains(guid) ? Qt::Checked : Qt::Unchecked);
        item->setHidden(!label.contains(m_panelTagFilter->text(), Qt::CaseInsensitive));
    }
    m_panelFolder->clear();
    m_panelFolder->addItem(QStringLiteral("全部文件夹"), QString());
    for (const QString& path : m_folderChoices) m_panelFolder->addItem(path, path);
    if (!m_folder.isEmpty() && m_panelFolder->findData(m_folder) < 0)
        m_panelFolder->addItem(m_folder + QStringLiteral("（已删除）"), m_folder);
    m_panelFolder->setCurrentIndex(qMax(0, m_panelFolder->findData(m_folder)));
    m_panelAttachments->setChecked(m_withAttachments);
}

void WizSearchView::rebuildChips()
{
    while (auto* item = m_chipLayout->takeAt(0)) {
        if (auto* widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
    struct Chip { QString label; QString kind; QString id; };
    QList<Chip> chips;
    for (const QString& guid : m_selectedTags)
        chips.append({m_tagLabels.value(guid, guid) + (m_tagOrder.contains(guid) ? QString() : QStringLiteral("（已删除）")), "tag", guid});
    if (!m_folder.isEmpty()) chips.append({m_folder, "folder", QString()});
    if (m_withAttachments) chips.append({QStringLiteral("有附件"), "attachment", QString()});
    for (const QString& extra : m_extraConditions) chips.append({extra.mid(6), "extra", extra});
    QStringList labels;
    for (const auto& chip : chips) labels << chip.label;
    const int visibleCount = chips.size() > 3 ? 2 : int(chips.size());
    for (int i = 0; i < visibleCount; ++i) {
        const auto chip = chips.at(i);
        auto* button = new QPushButton(m_chipRow);
        button->setObjectName("wiz-note-filter-chip");
        button->setProperty("filterKind", chip.kind); button->setProperty("filterId", chip.id);
        button->setText(fontMetrics().elidedText(chip.label, Qt::ElideRight, 90) + QStringLiteral(" ×"));
        button->setToolTip(QStringLiteral("取消条件：") + chip.label);
        button->setFixedHeight(24);
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        button->setStyleSheet("QPushButton { color:#245387; background:#eaf2fc; border:1px solid #c2d5ed;"
            "border-radius:7px; padding:1px 5px; } QPushButton:hover { background:#d8e8fa; }");
        m_chipLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, chip] {
            if (chip.kind == "tag") m_selectedTags.removeAll(chip.id);
            else if (chip.kind == "folder") m_folder.clear();
            else if (chip.kind == "attachment") m_withAttachments = false;
            else m_extraConditions.removeAll(chip.id);
            rebuildChips(); refreshFilterPanel(); scheduleSearch();
        });
    }
    if (chips.size() > 3) {
        auto* more = new QPushButton(QStringLiteral("＋%1").arg(chips.size() - visibleCount), m_chipRow);
        more->setObjectName("wiz-note-more-filters");
        more->setToolTip(labels.join('\n'));
        more->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        more->setStyleSheet("QPushButton { color:#52657d; background:#edf1f6; border:1px solid #d6deea;"
            "border-radius:7px; padding:1px 7px; } QPushButton:hover { background:#e1e9f3; }");
        more->setFixedHeight(24); m_chipLayout->addWidget(more);
        connect(more, &QPushButton::clicked, this, [this] {
            auto* menu = m_editSearch->findChild<QMenu*>("wiz-note-search-menu");
            menu->popup(m_editSearch->mapToGlobal(QPoint(0, m_editSearch->height() + 4)));
        });
    }
    if (!chips.isEmpty()) {
        auto* clear = new QPushButton(QStringLiteral("清除"), m_chipRow);
        clear->setObjectName("wiz-note-clear-filters"); clear->setFixedHeight(24);
        clear->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        clear->setStyleSheet("QPushButton { color:#65758b; background:transparent; border:none;"
            "border-radius:7px; padding:1px 8px; } QPushButton:hover { color:#245387; background:#e6edf6; }");
        clear->setToolTip(QStringLiteral("清除所有条件，保留内容关键词"));
        m_chipLayout->addWidget(clear);
        connect(clear, &QPushButton::clicked, this, [this] { clearFilters(); scheduleSearch(); });
    }
    m_chipLayout->addStretch();
    m_chipRow->setVisible(!chips.isEmpty());
    updateGeometry();
}

void WizSearchView::clearFilters()
{
    m_selectedTags.clear(); m_folder.clear(); m_withAttachments = false; m_extraConditions.clear();
    rebuildChips(); refreshFilterPanel();
}

void WizSearchView::selectTag(const QString& guid, bool consumeText)
{
    if (!m_tagOrder.contains(guid) || m_selectedTags.contains(guid)) return;
    m_selectedTags.append(guid);
    if (consumeText) m_editSearch->clear();
    m_tagCompleter->popup()->hide(); m_suggestionTimer.stop();
    rebuildChips(); refreshFilterPanel(); scheduleSearch();
}

void WizSearchView::scheduleSearch()
{
    m_tagCompleter->popup()->hide(); m_editSearch->hideFilterMenu();
    // Let popup teardown finish before the main window enters its search progress modal.
    m_submitTimer.start(0);
}

void WizSearchView::updateTagSuggestions()
{
    const QString prefix = m_editSearch->text().trimmed();
    if (prefix.isEmpty() || !m_editSearch->hasFocus()) { m_tagCompleter->popup()->hide(); return; }
    // Exclude selected tags without mutating query state or sending any network request.
    m_tagModel->clear();
    for (const QString& guid : m_tagOrder) {
        if (m_selectedTags.contains(guid)) continue;
        auto* item = new QStandardItem(QStringLiteral("标签：") + m_tagLabels.value(guid));
        item->setData(guid, Qt::UserRole); item->setData(m_tagLabels.value(guid), Qt::UserRole + 1);
        m_tagModel->appendRow(item);
    }
    m_tagCompleter->setCompletionPrefix(prefix);
    if (!m_tagCompleter->completionCount()) { m_tagCompleter->popup()->hide(); return; }
    m_tagCompleter->complete();
    // No implicit first choice: an ordinary Return must remain a content search.
    m_tagCompleter->popup()->setCurrentIndex(QModelIndex());
}

bool WizSearchView::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_editSearch || watched == m_tagCompleter->popup()) && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && m_tagCompleter->popup()->isVisible() && !m_tagCompleter->popup()->currentIndex().isValid()) {
            scheduleSearch(); return true;
        }
        if (key->key() == Qt::Key_Escape) m_tagCompleter->popup()->hide();
    }
    return QWidget::eventFilter(watched, event);
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
    auto* advanced = m_menu->addAction(QStringLiteral("更多条件（日期等）…"), this, SLOT(on_actionAdvancedSearch()));
    advanced->setObjectName("actionAdvancedSearch");
    auto* save = m_menu->addAction(QStringLiteral("保存当前搜索…"), this, SLOT(on_addCustomSearch()));
    save->setObjectName("actionSaveSearch");

}

void WizSearchEdit::setFilterPanel(QWidget* panel)
{
    auto* action = new QWidgetAction(m_menu);
    action->setObjectName("actionInlineSearchFilters");
    action->setDefaultWidget(panel);
    m_menu->insertAction(m_menu->actions().first(), action);
    m_menu->insertSeparator(m_menu->actions().at(1));
}

void WizSearchEdit::hideFilterMenu() { m_menu->hide(); }

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
    return QRect(QPoint(8, (height() - size.height()) / 2), size);
}

QRect WizSearchEdit::deleteIconRect() const
{
    QSize size = m_deleteIcon.size();
    WizScaleIconSizeForRetina(size);
    return QRect(QPoint(width() - size.width() - 8, (height() - size.height()) / 2), size);
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
        m_menu->popup(mapToGlobal(QPoint(0, height() + 4)));
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
