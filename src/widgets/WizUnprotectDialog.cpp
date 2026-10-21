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
QString resultMessage(const WizDocumentUnprotect::Result& result){
    using WizDocumentUnprotect::Error;
    if(result)return QObject::tr("已完成。原加密归档保留在：\n%1\n请保留证书和密码；此备份不会找回密码。").arg(result.backup);
    QString message;
    switch(result.error){
    case Error::ConsentRequired:return QObject::tr("已取消解除加密，未转换笔记。");
    case Error::CertificateUnavailable:return QObject::tr("本机未能读取可用的已有加密证书。请先完成账户同步并确认原证书可用，再重试。\n不会自动创建或替换证书，笔记仍保持加密。");
    case Error::SaveFailed:return QObject::tr("笔记尚未成功保存，已停止解除加密。请先解决保存问题，再重试。\n未开始转换，不会取消待保存任务或清除未保存内容。");
    case Error::PasswordInvalid:message=QObject::tr("证书密码不正确，或证书无法用此密码解锁。请输入证书密码，而不是账户登录密码。\n未开始转换，原加密笔记未改动。");break;
    case Error::Unsupported:message=QObject::tr("当前笔记不支持永久解除加密。仅支持已下载、可编辑且使用已有兼容证书加密的个人笔记。\n未开始转换。");break;
    case Error::Changed:message=QObject::tr("笔记、账户、证书或保存状态已变化，已停止解除加密。请返回原笔记确认后重试。\n没有自动重试，已生成的备份会保留。");break;
    case Error::ReadFailed:message=QObject::tr("无法完整读取或解密原加密归档，已停止解除加密。\n未发布转换结果，请保留原笔记和已有备份。");break;
    case Error::BackupFailed:message=QObject::tr("无法保存并核对完整的原加密备份，已停止解除加密。\n原加密笔记未转换，请检查磁盘空间和备份目录权限。");break;
    case Error::WriteFailed:message=QObject::tr("明文归档未能写入，已停止解除加密。\n请检查磁盘空间和目录权限后重试，保留原笔记及加密备份。");break;
    case Error::MetadataFailed:message=QObject::tr("无法完成笔记状态更新，解除加密未完成。\n请保留原笔记和加密备份，不要将本次操作视为成功。");break;
    case Error::RecoveryRequired:message=QObject::tr("自动回退未全部完成，请不要继续编辑或同步此笔记。\n请保留原加密备份，不要删除或覆盖它。");break;
    default:message=QObject::tr("解除加密未完成，没有自动重试。请保留原笔记和已有备份。");break;
    }
    if(!result.backup.isEmpty())message+=QObject::tr("\n原加密备份保留在：\n%1").arg(result.backup);
    return message;
}
WizDocumentUnprotect::Result run(QWidget* owner,WizDatabase& db,const WIZDOCUMENTDATA& requested,WizDocumentSaverThread* source,Services services){
    using namespace WizDocumentUnprotect;Result result;const QPointer<QWidget> guard(owner);const QPointer<WizDatabase> database(&db);const QPointer<WizDocumentSaverThread> saver(source);
    if(!guard||!saver||(services.contextCurrent&&!services.contextCurrent()))return result;
    const auto report=[&]{
        if(!guard||result.error==Error::ConsentRequired)return;
        if(services.report)services.report(guard,result);
        else if(result)QMessageBox::information(guard,QObject::tr("已解除加密"),resultMessage(result));
        else QMessageBox::warning(guard,QObject::tr("未完成解除加密"),resultMessage(result));
    };
    QEventLoop loop;quint64 fence=0;bool reached=false,ok=false;
    QObject::connect(saver,&WizDocumentSaverThread::saveFenceReached,&loop,[&](quint64 id,bool saved){if(id==fence){reached=true;ok=saved;loop.quit();}},Qt::QueuedConnection);
    QObject::connect(owner,&QObject::destroyed,&loop,&QEventLoop::quit);QObject::connect(saver,&QObject::destroyed,&loop,&QEventLoop::quit);
    fence=saver->saveFence(requested);if(!fence){result.error=Error::SaveFailed;report();return result;}loop.exec(QEventLoop::ExcludeUserInputEvents);
    if(!guard||!database||!saver||(services.contextCurrent&&!services.contextCurrent()))return result;
    if(!reached||!ok||!saver->isIdleForTransfer()){result.error=Error::SaveFailed;report();return result;}
    const auto prepared=prepare(*database,requested);if(!prepared.valid){result.error=prepared.error;}
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
    report();
    return result;
}
}
