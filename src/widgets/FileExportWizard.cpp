#include "FileExportWizard.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QHeaderView>
#include <QFrame>
#include <QTextEdit>
#include <QMessageBox>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDebug>
#include <QLineEdit>
#include <QInputDialog>
#include <QScopeGuard>

#include "WizDef.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "share/WizSettings.h"
#include "WizFileExporter.h"
#include "share/WizLocalFileUrl.h"
#include "widgets/FileLineEdit.h"
#include "share/WizBiometricNoteAccess.h"
#include "sync/WizApiEntry.h"

FileExportWizard::FileExportWizard(WizExplorerApp& app, QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(tr("Export Wizard"));
#ifndef Q_OS_MAC
    setWizardStyle(ModernStyle);
#endif

    addPage(new FileExportPageIntro);
    addPage(new FileExportPageDocList(app));
    addPage(new FileExportPageOptions);
    addPage(new FileExportPageExport(app));
}

FileExportWizard::FileExportWizard(const QString &location, WizExplorerApp& app, QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(tr("Export Wizard"));
#ifndef Q_OS_MAC
    setWizardStyle(ModernStyle);
#endif

    addPage(new FileExportPageDocList(location, app));
    addPage(new FileExportPageOptions);
    addPage(new FileExportPageExport(app));
}

FileExportPageIntro::FileExportPageIntro(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle(tr("Introduction"));

    QLabel *label = new QLabel(tr(
        "This wizard will help you export documents to "
        "various formats."));
    label->setWordWrap(true);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    setLayout(layout);
}

/////////////////////////////////////////////////////////////////////
/// Document List Page
/////////////////////////////////////////////////////////////////////

class BaseItem : public QTreeWidgetItem
{

public:
    BaseItem(WizExplorerApp& app, QString strName, QString kbGUID)
        : m_name(strName)
        , m_kbGUID(kbGUID)
    {
        setText(0, strName);
    }

    QString name() const { return m_name; }
    QString kbGUID() const { return m_kbGUID; }
    virtual bool isFolder() const { return false; }

protected:
    QString m_name;
    QString m_kbGUID;
};

class FolderItem : public BaseItem
{

public:
    FolderItem(WizExplorerApp& app, QString strName, QString kbGUID)
        : BaseItem(app, strName, kbGUID)
    {

        auto name = WizDatabase::getLocationDisplayName(strName);
        if (name.isEmpty()) name = strName;
        setText(0, name);

        QIcon icon;
        if (::WizIsPredefinedLocation(strName) && strName == "/My Journals/") {
            icon = WizLoadSkinIcon(app.userSettings().skin(), "category_folder_diary");
        } else {
            icon  = WizLoadSkinIcon(app.userSettings().skin(), "category_folder");
        }

        setIcon(0, icon);
    }

    bool isFolder() const override { return true; }
    QString location() const { return m_name; }
};

class NoteItem : public BaseItem
{

public:
    NoteItem(WizExplorerApp& app, QString strName, QString kbGUID, QString docGUID, QString docType)
        : BaseItem(app, strName, kbGUID)
        , m_docGUID(docGUID)
        , m_docType(docType)
    {
        // TODO: display icon for encrypted notes
        QString iconKey = "document_badge";
        QIcon icon = WizLoadSkinIcon(app.userSettings().skin(), iconKey);
        setIcon(0, icon);
    }

    bool isFolder() const override { return false; }
    QString docGUID() const { return m_docGUID; }
    QString docType() const { return m_docType; }

private:
    QString m_docGUID;
    QString m_docType;
};


FileExportPageDocList::FileExportPageDocList(const QString &location, WizExplorerApp& app, QWidget *parent)
    : QWizardPage(parent)
    , m_app(app)
    , m_dbMgr(app.databaseManager())
    , m_cancel(false)
    , m_isUpdateItemStatus(false)
    , m_rootLocation(location)
{
    setTitle(tr("Select Documents"));
    setSubTitle(
        tr("Please select documents you want to export.") +
        " " + tr("Collaboration notes are not supported now!")
    );
    //setCommitPage(true);

    m_statusText = new QLabel;

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->hide();
    m_treeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_treeWidget->setHeaderHidden(true);
    registerField("documents", m_treeWidget);

    m_progress = new QProgressBar(this);

    QHBoxLayout *progressLayout = new QHBoxLayout;
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(m_statusText);
    layout->addLayout(progressLayout);
    progressLayout->addWidget(m_progress);
    layout->addWidget(m_treeWidget);
    setLayout(layout);

    auto init = [this]() {
        if (m_rootLocation.isEmpty()) {
            initFolders();
        } else {
            initFoldersFromLocation(m_rootLocation);
        }
    };

    connect(this, &FileExportPageDocList::initialized,
            this, init, Qt::QueuedConnection);
    connect(m_treeWidget, &QTreeWidget::itemChanged,
            this, &FileExportPageDocList::handleItemChanged);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &FileExportPageDocList::handleItemDoubleClicked);
    connect(this, &FileExportPageDocList::willLeave,
            this, &FileExportPageDocList::updateSelection, Qt::DirectConnection);
}

void FileExportPageDocList::initializePage()
{
    connect(wizard(), &QWizard::finished,
            this, &FileExportPageDocList::handleCancelBtnClicked);
    Q_EMIT initialized();
}

bool FileExportPageDocList::isComplete() const
{
    if (m_progress->value() != m_progress->maximum()) {
        return false;
    } else {
        return true;
    }
}

int FileExportPageDocList::nextId() const
{
    Q_EMIT willLeave();
    return QWizardPage::nextId();
}

void FileExportPageDocList::initFolders()
{
    auto pAllFoldersItem = new FolderItem(m_app, tr("Personal Notes"), m_dbMgr.db().kbGUID());
    pAllFoldersItem->setIcon(0, WizLoadSkinIcon(m_app.userSettings().skin(), "category_folders"));
    pAllFoldersItem->setCheckState(0, Qt::Unchecked);
    m_treeWidget->addTopLevelItem(pAllFoldersItem);
    m_rootItem = pAllFoldersItem;

    CWizStdStringArray arrayAllLocation;
    m_dbMgr.db().getAllLocations(arrayAllLocation);

    // folder cache
    CWizStdStringArray arrayExtLocation;
    m_dbMgr.db().getExtraFolder(arrayExtLocation);

    if (!arrayExtLocation.empty()) {
        for (CWizStdStringArray::const_iterator it = arrayExtLocation.begin();
             it != arrayExtLocation.end();
             it++) {
            if (-1 == ::WizFindInArray(arrayAllLocation, *it)) {
                arrayAllLocation.push_back(*it);
            }
        }
    }

    if (arrayAllLocation.empty()) {
        arrayAllLocation.push_back(m_dbMgr.db().getDefaultNoteLocation());
    }

    m_statusText->setText(tr("<i>Scanning Database...</i>"));
    m_progress->setRange(1, (int)arrayAllLocation.size());

    initFolderItem(pAllFoldersItem, "", arrayAllLocation);

    pAllFoldersItem->setExpanded(true);
    //pAllFoldersItem->sortChildren(0, Qt::AscendingOrder);

    m_treeWidget->show();
    m_progress->hide();

    m_statusText->setText(tr("Choose documents:"));
    Q_EMIT completeChanged();
}

void FileExportPageDocList::initFoldersFromLocation(const QString &location)
{
    auto pAllFoldersItem = new FolderItem(m_app, location, m_dbMgr.db().kbGUID());
    pAllFoldersItem->setIcon(0, WizLoadSkinIcon(m_app.userSettings().skin(), "category_folder"));
    pAllFoldersItem->setCheckState(0, Qt::Unchecked);
    m_treeWidget->addTopLevelItem(pAllFoldersItem);
    m_rootItem = pAllFoldersItem;

    CWizStdStringArray childLocations;
    m_dbMgr.db().getAllChildLocations(location, childLocations);

    if (childLocations.size() > 1) {
        m_statusText->setText(tr("<i>Scanning Database...</i>"));
        m_progress->setRange(1, (int)childLocations.size());
    } else {
        m_progress->setRange(1, 1);
    }

    initFolderItem(pAllFoldersItem, location, childLocations);

    pAllFoldersItem->setExpanded(true);
    //pAllFoldersItem->sortChildren(0, Qt::AscendingOrder);

    m_treeWidget->show();
    m_progress->hide();

    m_statusText->setText(tr("Choose documents:"));
    Q_EMIT completeChanged();
}

void FileExportPageDocList::initFolderItem(QTreeWidgetItem *pParent, const QString &strParentLocation,
                                      const CWizStdStringArray &arrayAllLocation)
{
    m_progress->setValue(m_progress->value() + 1);
    qApp->processEvents();

    if (m_cancel) return;

    CWizStdStringArray arrayLocation;
    WizDatabase::getChildLocations(arrayAllLocation, strParentLocation, arrayLocation);

    // Find all sub-folders
    CWizStdStringArray::const_iterator it;
    for (const auto& strLocation : arrayLocation) {
        qApp->processEvents();
        if (m_cancel) return;

        if (m_dbMgr.db().isInDeletedItems(strLocation))
            continue;

        auto pFolderItem = new FolderItem(m_app, strLocation, m_dbMgr.db().kbGUID());
        pFolderItem->setCheckState(0, Qt::Unchecked);
        pParent->addChild(pFolderItem);

        initFolderItem(pFolderItem, strLocation, arrayAllLocation);
    }

    // Find all direct documents
    CWizDocumentDataArray arrayDocument;
    m_dbMgr.db().getDocumentsByLocation(strParentLocation, arrayDocument);
    for (const auto& doc: arrayDocument) {
        qApp->processEvents();
        if (m_cancel) return;

        if (doc.strType == "collaboration")
            continue;

        auto pNoteItem = new NoteItem(m_app, doc.strTitle, m_dbMgr.db().kbGUID(), doc.strGUID, doc.strType);
        pNoteItem->setCheckState(0, Qt::Unchecked);
        pParent->addChild(pNoteItem);
    }

}

void FileExportPageDocList::handleItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_isUpdateItemStatus) return;
    m_isUpdateItemStatus = true;
    updateChildItemStatus(item);
    updateParentItemStatus(item);
    m_isUpdateItemStatus = false;
}

void FileExportPageDocList::updateParentItemStatus(QTreeWidgetItem* item)
{
    auto parent = item->parent();
    if (Q_NULLPTR == parent)
    {
        return;
    }

    parent->setCheckState(0, item->checkState(0));
    int nCount = parent->childCount();
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        auto child = parent->child(nIndex);
        if (child->checkState(0) != parent->checkState(0))
        {
            parent->setCheckState(0, Qt::PartiallyChecked);
            break;
        }
    }

    updateParentItemStatus(parent);
}

void FileExportPageDocList::updateChildItemStatus(QTreeWidgetItem* item)
{
    int nCount = item->childCount();
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        auto child = item->child(nIndex);
        child->setCheckState(0, item->checkState(0));
        if (child->childCount() > 0)
        {
            updateChildItemStatus(child);
        }
    }
}

void FileExportPageDocList::handleItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    auto _item = static_cast<BaseItem*>(item);
    if (_item->isFolder()) {
        return;
    }
    auto state = item->checkState(0);
    if (state == Qt::Unchecked) {
        item->setCheckState(0, Qt::Checked);
    } else if (state == Qt::Checked) {
        item->setCheckState(0, Qt::Unchecked);
    }
}

QStringList FileExportPageDocList::findSelectedNotes(BaseItem* item)
{
    QStringList notes;
    //QList<NoteItem*> notes;
    if (item->checkState(0) == Qt::Unchecked)
        return notes;

    int nCount = item->childCount();
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        auto child = static_cast<BaseItem*>(item->child(nIndex));
        if (child->isFolder()) {
            if (child->checkState(0) == Qt::Unchecked)
                continue;
            notes.append(findSelectedNotes(child));
        } else {
            if (child->checkState(0) == Qt::Checked) {
                notes << static_cast<NoteItem*>(child)->docGUID();
            }
        }
    }

    return notes;
}

void FileExportPageDocList::updateSelection()
{
    if (isComplete())
        setField("documents", findSelectedNotes(m_rootItem));
}

/////////////////////////////////////////////////////////////////////
/// Options Page
/////////////////////////////////////////////////////////////////////

FileExportPageOptions::FileExportPageOptions(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle(tr("Options"));
    setSubTitle(tr("Please choose exporting options."));

    m_outputFolder = new DirLineEdit;
    m_outputFolder->setLabelText(tr("Output Folder:"));
    registerField("outputFolder*", m_outputFolder->lineEdit());

    m_keepFolder = new QCheckBox;
    m_keepFolder->setChecked(true);
    m_keepFolder->setText(tr("Keep document folder"));
    m_keepFolder->setToolTip(tr(
            "Ensure that the folder hierarchy "
            "of the output is consistent with that in the software"));
    registerField("keepFolder", m_keepFolder);

    m_compress = new QCheckBox;
    m_compress->setText(tr("Compress document folder"));
    m_compress->setToolTip(tr("Compress each note's file and its associated "
                              "resources together"));
    registerField("compress", m_compress);

    m_exportMetainfo = new QCheckBox;
    m_exportMetainfo->setChecked(true);
    m_exportMetainfo->setText(tr("Export meta information"));
    m_exportMetainfo->setToolTip(tr("Export meta information of notes in JSON format"));
    registerField("metainformation", m_exportMetainfo);

    m_noTitleFolderIfPossible = new QCheckBox;
    m_noTitleFolderIfPossible->setText(tr("Don't create title folder if possible"));
    m_noTitleFolderIfPossible->setToolTip(tr(
        "Generally, notes are associated with many resource files, such "
        "as images, CSS files, and attachments, which are referenced in "
        "the text of the note. So it is necessary to create a title folder "
        "to place these closely related files together."));
    registerField("noTitleFolderIfPossible", m_noTitleFolderIfPossible);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(m_outputFolder);
    layout->addWidget(m_keepFolder);
    layout->addWidget(m_compress);
    layout->addWidget(m_exportMetainfo);
    layout->addWidget(m_noTitleFolderIfPossible);
    setLayout(layout);
}

/////////////////////////////////////////////////////////////////////
/// Export Page
/////////////////////////////////////////////////////////////////////

FileExportPageExport::FileExportPageExport(WizExplorerApp& app, QWidget *parent)
    : QWizardPage(parent)
    , m_app(app)
    , m_dbMgr(app.databaseManager())
    , m_cancel(false)
{
    setTitle(tr("Exporting"));
    setFinalPage(true);

    m_exporter = new WizFileExporter(m_dbMgr, this);
    m_progress = new QProgressBar(this);
    m_details = new QTextEdit(this);
    m_details->setReadOnly(true);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(m_progress);
    layout->addWidget(m_details);
    setLayout(layout);

    connect(this, &FileExportPageExport::initialized,
            this, &FileExportPageExport::handleExportFile,
            Qt::QueuedConnection);
}

void FileExportPageExport::initializePage()
{
    m_exportCallbacks.invalidate();
    m_cancel = false;
    m_finished = false;
    m_restartPending = m_exporting;
    connect(wizard(), &QWizard::finished,
            this, &FileExportPageExport::handleCancelBtnClicked, Qt::UniqueConnection);
    Q_EMIT initialized();
}

void FileExportPageExport::handleCancelBtnClicked()
{
    m_cancel = true;
    m_finished = false;
    m_restartPending = false;
    m_exportCallbacks.cancel();
}

void FileExportPageExport::cleanupPage()
{
    handleCancelBtnClicked();
    QWizardPage::cleanupPage();
}

void FileExportPageExport::insertLog(const QString& text)
{
    QTextCursor cursor = m_details->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
}


void FileExportPageExport::handleExportFile()
{
    if (m_cancel || m_finished || m_exporting) return;
    const auto ticket = m_exportCallbacks.ticket();
    if (!WizDocumentCallbackEpoch::current(ticket)) return;
    const QPointer<FileExportPageExport> self(this);
    m_exporting = true;
    m_restartPending = false;
    const auto finish = qScopeGuard([self, ticket] {
        if (!self) return;
        self->m_exporting = false;
        if (WizDocumentCallbackEpoch::current(ticket)) self->m_exportCallbacks.cancel();
        if (self->m_restartPending && !self->m_cancel && !self->m_finished) Q_EMIT self->initialized();
    });
    // Personal-note wizard only. Bind a fresh batch to the local account/server;
    // a displayed document's permit and its save-password cache are not grants.
    WizDatabase& db = m_dbMgr.db();
    const auto scope = WizBiometricNoteAccess::snapshot(db, db, WizCommonApiEntry::server());
    const auto identity = WizBiometricNoteAccess::policyIdentity(scope);
    const auto current = [self, ticket, identity] {
        if (!self || self->m_cancel || !WizDocumentCallbackEpoch::current(ticket)) return false;
        auto& db = self->m_dbMgr.db();
        const auto live = WizBiometricNoteAccess::snapshot(db, db, WizCommonApiEntry::server());
        if (identity.isEmpty() || identity != WizBiometricNoteAccess::policyIdentity(live)) {
            self->handleCancelBtnClicked();
            return false;
        }
        return true;
    };
    const auto mayContinue = [self, current](const QString& error) {
        if (!current()) return false;
        const auto result = QMessageBox::critical(self, tr("Do you want to continue?"), error,
                                                  QMessageBox::Ok | QMessageBox::Abort, QMessageBox::Ok);
        return current() && result != QMessageBox::Abort;
    };
    if (!current()) return;
    QStringList notes = field("documents").toStringList();
    QString outputFolder = field("outputFolder").toString();
    bool keepFolder = field("keepFolder").toBool();
    bool compress = field("compress").toBool();
    bool metainfo = field("metainformation").toBool();
    bool noTitleFolderIfPossible = field("noTitleFolderIfPossible").toBool();

    m_progress->setRange(0, qMax(1, int(notes.size())));
    if (!current()) return;
    m_progress->setValue(0);
    if (!current()) return;

    QString verifiedCertificate;
    foreach (auto &note, notes) {
        m_progress->setValue(m_progress->value() + 1);
        if (!current()) return;
        qApp->processEvents();
        if (!current()) return;

        WIZDOCUMENTDATA data;
        if (!db.documentFromGuid(note, data)) {
            if (!mayContinue(tr("Can't find document for GUID: %1").arg(note))) return;
            continue;
        }

        insertLog(QString("Exporting %1\n").arg(data.strTitle));
        if (!current()) return;

        QString certificate;
        if (data.nProtected == 1) {
            const auto local = WizBiometricNoteAccess::snapshot(db, db, WizCommonApiEntry::server());
            certificate = WizBiometricKeychain::itemIdentity(local);
            if (certificate.isEmpty()) {
                if (!mayContinue(tr("Can't load user cert."))) return;
                continue;
            }
            if (certificate != verifiedCertificate) {
                insertLog("Asking for password...\n");
                if (!current()) return;
                bool ok;
                QString password = QInputDialog::getText(
                    this, tr("Password for Encrypted Notes"),
                    tr("Password:"), QLineEdit::Password, "", &ok);
                if (!current() || !ok) return;
                const auto live = WizBiometricNoteAccess::snapshot(db, db, WizCommonApiEntry::server());
                if (certificate != WizBiometricKeychain::itemIdentity(live)) {
                    handleCancelBtnClicked();
                    return;
                }
                const auto& cert = local.certificate;
                const bool valid = !password.isEmpty()
                    && db.cacheCertPasswordLocally(password, cert.n, cert.e, cert.encryptedD);
                password.clear();
                if (!current()) return;
                if (!valid) {
                    if (!mayContinue(tr("Invalid password."))) return;
                    continue;
                }
                verifiedCertificate = certificate;
            }
            if (!db.loadUserCert()) {
                if (!mayContinue(tr("Can't load user cert."))) return;
                continue;
            }
        }

        const auto canRead = [self, current, data, certificate] {
            if (!current()) return false;
            auto& db = self->m_dbMgr.db();
            WIZDOCUMENTDATA live;
            bool valid = data.strKbGUID == db.kbGUID()
                && db.documentFromGuid(data.strGUID, live) && live.strKbGUID == data.strKbGUID
                && live.nProtected == data.nProtected;
            if (data.nProtected == 1) {
                const auto scope = WizBiometricNoteAccess::snapshot(db, db, WizCommonApiEntry::server());
                valid = valid && !db.getCertPassword().isEmpty()
                    && certificate == WizBiometricKeychain::itemIdentity(scope);
            }
            if (!valid) self->handleCancelBtnClicked();
            return valid;
        };
        if (!canRead()) return;

        QString destFolder = outputFolder;
        if (keepFolder)
            destFolder = outputFolder + data.strLocation;

        QString error = "Unknown error";

        auto format = WizFileExporter::HTML;
        if (WizIsMarkdownNote(data))
            format = WizFileExporter::Markdown;

        bool ok = m_exporter->exportNote(
            data, destFolder, format, compress, metainfo,
            noTitleFolderIfPossible, &error, canRead);
        if (!current()) return;
        if (!ok) {
            if (!mayContinue(error)) return;
        }
    }

    if (!current()) return;
    m_finished = true;
    Q_EMIT completeChanged();
    if (!current()) return;
    const bool opened = QDesktopServices::openUrl(WizLocalFileUrl(outputFolder));
    if (!current()) return;
    if (!opened) {
        QMessageBox::information(this, tr("Export complete"),
            QStringLiteral("导出操作已结束，但无法自动打开输出目录。请在 Finder 中手动打开：\n%1").arg(outputFolder));
    }
}

bool FileExportPageExport::isComplete() const
{
    return m_finished;
}



