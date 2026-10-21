#pragma once

#include <QSaveFile>

#ifdef WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING
namespace WizMarkdownExportTransaction {
static bool cancelHtmlCommit = false;
void beforeHtmlCommitForTest(QSaveFile& output)
{
    if (cancelHtmlCommit) output.cancelWriting();
}
}
#endif

namespace HtmlTransactionCases {
template<class Check>
void run(const QString& root, WizDatabase& database, const WIZDOCUMENTDATA& document, Check check)
{
    QTemporaryDir isolated(root + "/html-corpus-XXXXXX");
    if (!check(isolated.isValid(), "HTML transaction corpus has its own disposable directory")) return;
    const QString target=isolated.filePath(QString::fromUtf8("页面.html"));
    const QString resources=isolated.filePath(QString::fromUtf8("页面_files"));
    const QByteArray old=QByteArray::fromHex("efbbbf")+"<html>previous complete body</html>";
    const auto cleanStage=[&] {
        return QDir(isolated.path()).entryList({".wiz-export-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty();
    };
    const auto reset=[&] {
        database.failBeforeExtraction=false;database.makeLastExtractedResourceUnreadable=false;
        return (!QFileInfo::exists(resources)||QDir(resources).removeRecursively())
            &&QDir().mkpath(resources+"/.hidden-directory")&&writeFile(target,old)
            &&writeFile(resources+"/a-first.png","OLD A")&&writeFile(resources+"/z-fails.png","OLD Z")
            &&writeFile(resources+"/unrelated","KEEP")&&writeFile(resources+"/.hidden-directory/nested","HIDDEN");
    };
    const auto unchanged=[&] {
        return readFile(target)==old&&readFile(resources+"/a-first.png")=="OLD A"
            &&readFile(resources+"/z-fails.png")=="OLD Z"&&readFile(resources+"/unrelated")=="KEEP"
            &&readFile(resources+"/.hidden-directory/nested")=="HIDDEN";
    };
    const auto exporter=[&](const QString& staged) {return database.exportToHtmlFile(document,staged);};
    const auto current=[] {return true;};
    QString error;
    if (!check(reset(),"HTML old body and resource fixture seeded")) return;
    database.failBeforeExtraction=true;
    check(!WizMarkdownExportTransaction::writeHtml(target,"rendered",exporter,current,error)
        &&error=="staged export failed"&&unchanged()&&cleanStage(),"HTML early extraction failure preserves old body/resources and cleans staging");
    if (!reset()) return;
    database.makeLastExtractedResourceUnreadable=true;
    const bool late=WizMarkdownExportTransaction::writeHtml(target,"rendered",exporter,current,error);
    check(QFile::setPermissions(database.unreadableResource,QFileDevice::ReadOwner|QFileDevice::WriteOwner),
        "HTML synthetic resource permissions restored after actual copy failure");
    check(!late&&error=="staged export failed"&&unchanged()&&cleanStage(),
        "HTML actual late copy failure preserves every previous body/resource byte");
    if (!reset()) return;
    int exports=0;
    check(!WizMarkdownExportTransaction::writeHtml(target,"rendered",[&](const QString&) {++exports;return true;},
        [] {return false;},error)&&exports==0&&unchanged()&&cleanStage(),"expired HTML permission refuses before staging or archive work");
    int validations=0;
    check(!WizMarkdownExportTransaction::writeHtml(target,"rendered",exporter,[&] {return ++validations<2;},error)
        &&validations==2&&unchanged()&&cleanStage(),"HTML permission lost after export discards private body/resources");
    validations=0;bool sawInstalled=false;
    check(!WizMarkdownExportTransaction::writeHtml(target,"rendered",exporter,[&] {
        if(++validations==3){sawInstalled=readFile(resources+"/a-first.png")=="NEW A";return false;}return true;
    },error)&&validations==3&&sawInstalled&&unchanged()&&cleanStage(),
        "HTML permission lost after resource installation rolls resources back without committing body");
#ifdef WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING
    WizMarkdownExportTransaction::cancelHtmlCommit=true;
    const bool committed=WizMarkdownExportTransaction::writeHtml(target,"rendered",exporter,current,error);
    WizMarkdownExportTransaction::cancelHtmlCommit=false;
    check(!committed&&error=="final HTML commit failed"&&unchanged()&&cleanStage(),
        "actual canceled QSaveFile HTML commit rolls installed resources back and preserves old body");
#endif
    check(!WizMarkdownExportTransaction::writeHtml(target,QString(),[](const QString&) {return true;},current,error)
        &&error=="staged HTML read failed"&&unchanged()&&cleanStage(),"missing staged saved HTML refuses without truncating old body");
    check(WizMarkdownExportTransaction::writeHtml(target,QString(),exporter,current,error)
        &&readFile(target)==QByteArray::fromHex("efbbbf")+QString::fromUtf8("<html><img src='%E9%A1%B5%E9%9D%A2_files/a-first.png'><img src='%E9%A1%B5%E9%9D%A2_files/z-fails.png'></html>").toUtf8()
        &&readFile(resources+"/a-first.png")=="NEW A"&&readFile(resources+"/z-fails.png")=="NEW Z",
        "empty HTML render keeps exact saved archive body/BOM and complete resource naming");
    const QString rendered=QString::fromUtf8("<html>定理<img src='index_files/a-first.png'></html>");
    check(WizMarkdownExportTransaction::writeHtml(target,rendered,exporter,current,error)
        &&readFile(target)==QByteArray::fromHex("efbbbf")+QString::fromUtf8("<html>定理<img src='%E9%A1%B5%E9%9D%A2_files/a-first.png'></html>").toUtf8()
        &&readFile(resources+"/unrelated")=="KEEP"&&readFile(resources+"/.hidden-directory/nested")=="HIDDEN"&&cleanStage(),
        "rendered Unicode HTML retains BOM, basename rewrite, unrelated and hidden resources");
    const QString first=isolated.filePath("first.html");
    check(WizMarkdownExportTransaction::writeHtml(first,rendered,exporter,current,error)
        &&readFile(first).contains("first_files/a-first.png")&&readFile(isolated.filePath("first_files/a-first.png"))=="NEW A"
        &&cleanStage(),"first HTML export creates complete body and resources");
    const QString noResources=isolated.filePath("no-resources.html");
    check(WizMarkdownExportTransaction::writeHtml(noResources,QString(),[](const QString& staged) {
        return writeFile(staged,"archive bytes without BOM");},current,error)
        &&readFile(noResources)=="archive bytes without BOM"&&cleanStage(),"HTML archive fallback preserves exact staged bytes even without BOM or resources");
    const QString missing=isolated.filePath("missing/note.html");
    check(!WizMarkdownExportTransaction::writeHtml(missing,rendered,exporter,current,error)
        &&!QFileInfo::exists(missing),"HTML missing output parent fails without creating it");
    const QString directory=isolated.filePath("directory.html");QDir().mkpath(directory);
    check(!WizMarkdownExportTransaction::writeHtml(directory,rendered,exporter,current,error)
        &&QFileInfo(directory).isDir()&&cleanStage(),"HTML directory destination fails without replacing it");
    check(!WizMarkdownExportTransaction::writeHtml(target,rendered,{},current,error)
        &&!WizMarkdownExportTransaction::writeHtml(target,rendered,exporter,{},error),"HTML helper requires both exporter and explicit current permission");
    const QString amp=isolated.filePath("A&B.html");
    check(WizMarkdownExportTransaction::writeHtml(amp,QString(),exporter,current,error)
        &&readFile(amp).contains("A%26amp%3BB_files/")&&readFile(isolated.filePath("A&amp;B_files/a-first.png"))=="NEW A",
        "saved HTML encodes the literal legacy resource directory name as a URL");
    check(WizMarkdownExportTransaction::writeHtml(amp,rendered,exporter,current,error)
        &&readFile(amp).contains("A%26amp%3BB_files/")&&readFile(isolated.filePath("A&amp;B_files/a-first.png"))=="NEW A",
        "rendered HTML resolves to the same actual resource directory as archive HTML");
#ifdef Q_OS_MACOS
    const QByteArray metadata=QFile::encodeName(resources+"/unrelated"),dirMetadata=QFile::encodeName(resources+"/.hidden-directory");
    const bool seeded=QFile::setPermissions(resources+"/unrelated",QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        &&::setxattr(metadata.constData(),"user.wiznote.html","FILE",4,0,0)==0
        &&::setxattr(dirMetadata.constData(),"user.wiznote.html","DIR",3,0,0)==0;
    check(seeded,"HTML synthetic file/directory metadata seeded");
    const bool metadataExport=WizMarkdownExportTransaction::writeHtml(target,rendered,exporter,current,error);
    char fileValue[8]={},dirValue[8]={};struct stat info{};
    check(metadataExport&&::getxattr(metadata.constData(),"user.wiznote.html",fileValue,sizeof(fileValue),0,0)==4
        &&QByteArray(fileValue,4)=="FILE"&&::getxattr(dirMetadata.constData(),"user.wiznote.html",dirValue,sizeof(dirValue),0,0)==3
        &&QByteArray(dirValue,3)=="DIR"&&::stat(metadata.constData(),&info)==0&&(info.st_mode&0777)==0600&&cleanStage(),
        "HTML unrelated file mode and file/directory extended attributes survive publication");
#endif
    check(QDir(isolated.path()).entryList({".wiz-export-recovery-*",".wiz-export-lock-*"},
        QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty(),
        "HTML never enters persistent Markdown recovery even when its compile gate is ON");
    if (!reset()) return;
    validations=0;QString retained;
    const bool failedRollback=WizMarkdownExportTransaction::writeHtml(target,rendered,exporter,[&] {
        if(++validations!=3)return true;
        const auto dirs=QDir(isolated.path()).entryList({".wiz-export-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot);
        if(dirs.size()==1){retained=isolated.filePath(dirs.first());QDir().mkpath(retained+"/returned-resources/blocker");}
        return false;
    },error);
    check(!failedRollback&&readFile(target)==old&&!retained.isEmpty()&&error.contains(retained)
        &&readFile(retained+"/previous-resources/a-first.png")=="OLD A"
        &&readFile(retained+"/previous-resources/.hidden-directory/nested")=="HIDDEN",
        "failed HTML rollback reports and retains the only complete old resource directory instead of deleting it");
}
}
