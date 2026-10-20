#include "WizEmailShareDialog.h"
#include "ui_WizEmailShareDialog.h"
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTextCodec>
#include <QPixmap>
#include <QVBoxLayout>
#include <QDebug>
#include <QPointer>
#include <QScopeGuard>

#include "share/WizMessageBox.h"
#include "sync/WizApiEntry.h"
#include "sync/WizToken.h"
#include "share/WizSettings.h"
#include "database/WizDatabaseManager.h"
#include "share/jsoncpp/json/json.h"
#include "utils/WizStyleHelper.h"

#define EMAIL_CONTACTS "EMAILCONTACTS"

#define AUTO_INSERT_COMMENT_TO_NOTE  "EmailAutoInsertComment"
#define Signature_TEXT          "EmailSignatureText"

enum returnCode {
    codeOK = 200,                   //:ok,
    codeErrorParam = 322,      //:参数错误，
    codeErrorFrequent = 429,    //请求超出频率限制！
    codeErrorFile = 3330,         //: 笔记不存在，
    codeErrorSize = 3370,        //:邮件大小超出尺寸，
    codeErrorEncrypt = 3371,   //:加密笔记无法分享，
    codeErrorEmail = 3372,      //：邮箱未验证，无法发送分享邮件，
    codeErrorServer = 500           //:服务器错误
};

WizEmailShareDialog::WizEmailShareDialog(WizExplorerApp& app, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WizEmailShareDialog)
    , m_app(app)
    , m_net(nullptr)
{
    ui->setupUi(this);
//    ui->checkBox_saveNotes->setVisible(false);
    QPixmap pix(Utils::WizStyleHelper::skinResourceFileName("send_email"));
    QIcon icon(Utils::WizStyleHelper::skinResourceFileName("send_email"));
    ui->toolButton_send->setIcon(icon);
    ui->toolButton_send->setIconSize(pix.size());
    ui->toolButton_contacts->setIcon(QIcon(Utils::WizStyleHelper::skinResourceFileName("document_badge")));
    ui->toolButton_contacts->setToolTip(tr("Show frequent contacts list"));

    m_contactDialog = new QDialog(this);
    m_contactDialog->setWindowTitle(tr("Frequent Contacts List"));
    QVBoxLayout* layout = new QVBoxLayout(m_contactDialog);
    m_contactList = new QListWidget(m_contactDialog);
    m_contactList->setAttribute(Qt::WA_MacShowFocusRect, 0);
    layout->addWidget(m_contactList);
    connect(m_contactList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            SLOT(on_contactsListItemClicked(QListWidgetItem*)));

    //
    QString stext = m_app.userSettings().get(Signature_TEXT);
    if (!stext.isEmpty())
    {
        ui->textEdit_notes->setPlainText("\n\n"+stext);
    }

    bool autoInsert = m_app.userSettings().get(AUTO_INSERT_COMMENT_TO_NOTE).toInt() == 1;
    ui->checkBox_saveNotes->setChecked(autoInsert);
}

WizEmailShareDialog::~WizEmailShareDialog()
{
    delete ui;
}

void WizEmailShareDialog::done(int result)
{
    m_shareCallbacks.invalidate();
    m_request.clear();m_read = {};
    QDialog::done(result);
}

void WizEmailShareDialog::setNote(const WIZDOCUMENTDATA& note, const QString& sendTo)
{
    setNote(note, sendTo, {}); // Retain the old interface, never invent permission.
}

void WizEmailShareDialog::setNote(const WIZDOCUMENTDATA& note, const QString& sendTo, const WizDocumentReadPermit& read)
{
    m_shareCallbacks.invalidate();
    m_request.clear(); // Do not reinterpret a previous request as this note.
    m_read = read;
    m_sentNote = {};
    m_sentComment.clear();m_sentTo.clear();m_sentInsert = false;
    m_note = note;
    ui->lineEdit_subject->setText(m_note.strTitle);
    ui->lineEdit_to->setText(sendTo);
    ui->comboBox_replyTo->insertItem(0, m_app.userSettings().userId());
    ui->comboBox_replyTo->insertItem(1, m_app.userSettings().myWizMail());
    if (m_app.databaseManager().db(note.strKbGUID).isGroup())
    {
        WIZGROUPDATA group;
        m_app.databaseManager().db().getGroupData(note.strKbGUID, group);
        qDebug() << "group my wiz : strkbguid " <<  note.strKbGUID << group.strMyWiz;
        ui->comboBox_replyTo->insertItem(0, group.strMyWiz);
        ui->comboBox_replyTo->setCurrentIndex(0);
    }
}

bool WizEmailShareDialog::isInsertCommentToNote() const
{
    return ui->checkBox_saveNotes->isChecked();
}

QString WizEmailShareDialog::getCommentsText() const
{
    return QString::fromUtf8(QUrl::toPercentEncoding(ui->textEdit_notes->toPlainText()));
}

void WizEmailShareDialog::on_toolButton_send_clicked()
{
    Q_ASSERT(!m_note.strGUID.isEmpty());

    sendEmails();
}

QString WizEmailShareDialog::getExInfo()
{
    const QPointer<WizEmailShareDialog> self(this);
    const auto read = m_read;
    const auto ticket = m_shareCallbacks.ticket();
    if (!read.current()) return QString();
//    kb_guid
//    document_guid
//    token
//    mail_to            收件人，标准的邮箱格式 `"name" <xxx@xxx.com>` 或者 `xxx@xxx.com`,可以指定多个收件人
//    subject            nullable, 邮件主题，如果为空，则取笔记标题
//    cc_to_self         nullable, 是否抄送自己，, 字符串的 true or false, 默认false
//    reply_to           nullable, 可为空，不为空时要求为标准格式
//    note
//    api_version        4

    QString strToken = WizToken::token();
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(ticket) || strToken.isEmpty()) return QString();
    QString cc_to_self = ui->checkBox_sendMeCopy->isChecked() ? "true" : "false";
    QString result = "?kb_guid=" + m_note.strKbGUID + "&document_guid=" + m_note.strGUID + "&token=" + strToken
            + "&mail_to=" + ui->lineEdit_to->text() + "&subject=" + ui->lineEdit_subject->text() + "&cc_to_self=" + cc_to_self
            + "&reply_to=" + ui->comboBox_replyTo->currentText() + "&note=" + getCommentsText()
            + "&api_version=4";

    return result;
}

void WizEmailShareDialog::on_mailShare_finished(int nCode, const QString& returnMessage)
{
    const auto read = m_read;
    const auto ticket = m_shareCallbacks.ticket();
    const QPointer<WizEmailShareDialog> self(this);
    if (!read.current() || m_sentNote.strGUID.isEmpty()) return;
    switch (nCode) {
    case codeOK:
        ui->labelInfo->setText(tr("Send success"));
        if (m_sentInsert)
        {
            emit insertCommentToNoteRequest(m_sentNote.strGUID, m_sentComment);
        }
        if (self && read.current() && WizDocumentCallbackEpoch::current(ticket)) accept();
        break;
    case codeErrorParam:
    case codeErrorFile:
    case codeErrorSize:
    case codeErrorEncrypt:
    case codeErrorEmail:
    case codeErrorFrequent:
    case codeErrorServer:
        on_networkError(returnMessage);
        break;
    default:
        on_networkError(tr("Unkown error."));
        break;
    }
}

void WizEmailShareDialog::processReturnMessage(const QString& returnMessage, int& nCode, QString& message)
{
    nCode = -1;
    if (!m_read.current()) return;
    Json::Value d;
    Json::Reader reader;
    if (!reader.parse(returnMessage.toUtf8().constData(), d))
        return;

    if (d.isMember("error_code")) {
        qDebug() << QString::fromStdString(d["error"].asString());
        return;
    }

    if (d.isMember("return_code") && d["return_code"].isInt()) {
        nCode = d["return_code"].asInt();
        if (nCode == 200) {
            qDebug() <<"[EmailShar]:send email successed!";
            saveContacts();
            return;
        } else {
            message = QString::fromStdString(d["return_message"].asString());
            qDebug() << message << ", code = " << nCode;
            return;
        }
    }
}

void WizEmailShareDialog::saveContacts()
{
    if (!m_read.current()) return;
    QString strText = m_sentTo;
    QStringList toList = strText.split(QRegularExpression(",|;"));
    QString strContact = m_app.userSettings().get(EMAIL_CONTACTS);
    foreach (QString str, toList)
    {
        if (!strContact.contains(str))
            strContact.append(str + ";");
    }
    m_app.userSettings().set(EMAIL_CONTACTS, strContact);
}

void WizEmailShareDialog::updateContactList()
{
    m_contactList->clear();
    QString strContact = m_app.userSettings().get(EMAIL_CONTACTS);
    QStringList contactList = strContact.split(';');
    foreach(QString str, contactList)
    {
        m_contactList->addItem(str);
    }
}

void WizEmailShareDialog::sendEmails()
{
    const auto read = m_read;
    if (!read.current()) {
        ui->labelInfo->setText(QStringLiteral("笔记已锁定或切换，请重新打开分享窗口。"));
        return;
    }
    if (m_sending || m_request) return;
    const QPointer<WizEmailShareDialog> self(this);
    const auto ticket = m_shareCallbacks.ticket();
    const WIZDOCUMENTDATA note = m_note;
    m_sending = true;
    const auto preparing = qScopeGuard([self]{if (self) self->m_sending = false;});
    ui->labelInfo->setText(tr("Sending..."));

    QString strKS = m_app.databaseManager().db().getKbServer(m_note.strKbGUID);
    QString strExInfo = getExInfo();
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(ticket)) return;
    if (strExInfo.isEmpty()) {ui->labelInfo->setText(tr("Unable to prepare email sharing."));return;}
    const QString sentTo = ui->lineEdit_to->text(), sentComment = getCommentsText();
    const bool sentInsert = isInsertCommentToNote();
    QString strUrl = WizCommonApiEntry::mailShareUrl(strKS, strExInfo);
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(ticket)) return;
    if (strUrl.isEmpty()) {ui->labelInfo->setText(tr("Unable to prepare email sharing."));return;}

    if(!m_net)
    {
        m_net = new QNetworkAccessManager(this);
        connect(m_net, SIGNAL(finished(QNetworkReply*)), SLOT(on_networkFinished(QNetworkReply*)));
    }
    m_sentNote = note;m_sentTo = sentTo;m_sentComment = sentComment;m_sentInsert = sentInsert;
    m_request = m_net->get(QNetworkRequest(strUrl));
    if (!m_request) ui->labelInfo->setText(tr("Unable to prepare email sharing."));
}

void WizEmailShareDialog::on_toolButton_contacts_clicked()
{
    updateContactList();
    m_contactDialog->exec();
    return;
}

void WizEmailShareDialog::on_contactsListItemClicked(QListWidgetItem *item)
{
    QString strUserName = item->text();
    QString strTo = ui->lineEdit_to->text();
    if (!strTo.isEmpty() && strTo.right(1) != "," && strTo.right(1) != ";")
    {
        strTo.append(";");
    }
    strTo += strUserName + ";";
    ui->lineEdit_to->setText(strTo);
}

void WizEmailShareDialog::on_networkFinished(QNetworkReply* reply)
{
    if (!reply) return;
    if (reply != m_request || !m_read.current()) {
        if (reply == m_request) m_request.clear();
        reply->deleteLater();return;
    }
    const QPointer<WizEmailShareDialog> self(this);
    const QPointer<QNetworkReply> response(reply);
    m_sending = true;
    const auto delivering = qScopeGuard([self]{if (self) self->m_sending = false;});
    const auto read = m_read;
    const auto ticket = m_shareCallbacks.ticket();
    m_request.clear();
    const auto error = reply->error();
    const QString content = error == QNetworkReply::NoError ? QString::fromUtf8(reply->readAll()) : reply->errorString();
    if (response) response->deleteLater();
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(ticket)) return;
    if (error != QNetworkReply::NoError) {on_networkError(content);return;}
    int nCode = -1;QString returnMessage;
    processReturnMessage(content, nCode, returnMessage);
    if (self && read.current() && WizDocumentCallbackEpoch::current(ticket)) on_mailShare_finished(nCode, returnMessage);
}

void WizEmailShareDialog::on_networkError(const QString& errorMsg)
{
    const auto read = m_read;
    const auto ticket = m_shareCallbacks.ticket();
    if (!read.current()) return;
    const QPointer<WizEmailShareDialog> self(this);
    WizMessageBox::information(this, tr("Info"), errorMsg);
    if (self && read.current() && WizDocumentCallbackEpoch::current(ticket)) self->ui->labelInfo->setText(errorMsg);
}

void WizEmailShareDialog::on_toolButton_settings_clicked()
{
    QDialog dlg;
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QCheckBox* checkbox = new QCheckBox(&dlg);
    checkbox->setText(tr("Auto insert comment to note"));
    QLabel* label = new QLabel(&dlg);
    label->setText(tr("Signature:"));
    QTextEdit* textEdit = new QTextEdit(&dlg);
    layout->addWidget(checkbox);
    layout->addWidget(label);
    layout->addWidget(textEdit);

    bool autoInsert = m_app.userSettings().get(AUTO_INSERT_COMMENT_TO_NOTE).toInt() == 1;
    checkbox->setChecked(autoInsert);

    QString text = m_app.userSettings().get(Signature_TEXT);
    textEdit->setText(text);

    //
    connect(textEdit, SIGNAL(textChanged()), SLOT(signature_text_edit_finished()));
    connect(checkbox, SIGNAL(toggled(bool)), SLOT(autoInsert_state_changed(bool)));

    dlg.exec();

    autoInsert = m_app.userSettings().get(AUTO_INSERT_COMMENT_TO_NOTE).toInt() == 1;
    if (autoInsert)
    {
        ui->checkBox_saveNotes->setChecked(true);
    }
    text = m_app.userSettings().get(Signature_TEXT);
    if (!text.isEmpty() && ui->textEdit_notes->toPlainText().isEmpty())
    {
        ui->textEdit_notes->setPlainText("\n\n"+text);
    }
}

void WizEmailShareDialog::signature_text_edit_finished()
{
    if (QTextEdit* textEdit = qobject_cast<QTextEdit*>(sender()))
    {
        QString text = textEdit->toPlainText();
        m_app.userSettings().set(Signature_TEXT, text);
    }
}

void WizEmailShareDialog::autoInsert_state_changed(bool checked)
{
    m_app.userSettings().set(AUTO_INSERT_COMMENT_TO_NOTE, checked ? "1" : "0");
}
