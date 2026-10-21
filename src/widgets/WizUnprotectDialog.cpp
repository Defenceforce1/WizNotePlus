#include "WizUnprotectDialog.h"
#include "database/WizDatabase.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include <QEventLoop>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
namespace WizUnprotectDialog {
WizDocumentUnprotect::Result run(QWidget* owner,WizDatabase& db,const WIZDOCUMENTDATA& requested,WizDocumentSaverThread* source,Services services){
    using namespace WizDocumentUnprotect;Result result;const QPointer<QWidget> guard(owner);const QPointer<WizDatabase> database(&db);const QPointer<WizDocumentSaverThread> saver(source);
    if(!guard||!saver||(services.contextCurrent&&!services.contextCurrent()))return result;
    QEventLoop loop;quint64 fence=0;bool reached=false,ok=false;
    QObject::connect(saver,&WizDocumentSaverThread::saveFenceReached,&loop,[&](quint64 id,bool saved){if(id==fence){reached=true;ok=saved;loop.quit();}},Qt::QueuedConnection);
    QObject::connect(owner,&QObject::destroyed,&loop,&QEventLoop::quit);QObject::connect(saver,&QObject::destroyed,&loop,&QEventLoop::quit);
    fence=saver->saveFence(requested);if(!fence)return result;loop.exec(QEventLoop::ExcludeUserInputEvents);
    if(!guard||!database||!saver||!reached||!ok||!saver->isIdleForTransfer()||(services.contextCurrent&&!services.contextCurrent()))return result;
    const auto prepared=prepare(*database,requested);if(!prepared.valid){result.error=Error::Unsupported;}
    else {
        if(!services.password)services.password=[](QWidget* parent,bool* accepted){return QInputDialog::getText(parent,QObject::tr("永久解除笔记加密"),QObject::tr("请重新输入证书密码（不是账户登录密码）："),QLineEdit::Password,{},accepted);};
        if(!services.confirm)services.confirm=[](QWidget* parent,const QString& folder){return QMessageBox::warning(parent,QObject::tr("确认永久解除加密"),QObject::tr("笔记正文和其内嵌资源将以未加密归档保存，并可在后续同步中成为明文。\n先保留完整的原加密归档至：\n%1\n\n不会删除证书、备份或找回密码。确认继续？").arg(folder),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;};
        bool accepted=false;QString password=services.password(guard,&accepted);
        const auto current=[&]{return guard&&database&&saver&&saver->isIdleForTransfer()&&(!services.contextCurrent||services.contextCurrent())&&guard&&database&&WizDocumentUnprotect::current(*database,prepared);};
        if(!guard||!database){password.clear();return result;}
        if(!accepted){password.clear();result.error=Error::ConsentRequired;return result;}
        if(!current())result.error=Error::Changed;
        else if(!services.confirm(guard,QFileInfo(prepared.archive).absolutePath()+"/encrypted-backups"))result.error=Error::ConsentRequired;
        else if(current())result=execute(*database,prepared,password,true,current);
        else result.error=Error::Changed;
        password.clear();
    }
    if(guard&&result.error!=Error::ConsentRequired){
        if(services.report)services.report(guard,result);
        else if(result)QMessageBox::information(guard,QObject::tr("已解除加密"),QObject::tr("已完成。原加密归档保留在：\n%1\n请保留证书和密码；此备份不会找回密码。").arg(result.backup));
        else QMessageBox::warning(guard,QObject::tr("未完成解除加密"),result.error==Error::RecoveryRequired?QObject::tr("自动回退未全部完成，请不要继续编辑或同步此笔记。原加密备份保留在：\n%1").arg(result.backup):QObject::tr("笔记、密码或保存状态未通过检查，或写入失败。没有自动重试；已生成的备份会保留。\n%1").arg(result.backup));
    }
    return result;
}
}
