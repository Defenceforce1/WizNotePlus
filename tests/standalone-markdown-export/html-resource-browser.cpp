#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <functional>
#include <memory>
#include <atomic>
#include "WizQtCompat.h"
#include "share/WizMarkdownExportTransaction.h"

using CString=QString;
struct WIZDOCUMENTDATA {};
bool WizCopyFolder(const QString&,const QString&,bool);
bool WizLoadUnicodeTextFromFile(const QString&,QString&,const char* = "UTF-8");
bool WizSaveUnicodeTextToUtf8File(const QString&,const QString&);
namespace Utils {
struct WizMisc {
    static QString extractFileName(const QString&);
    static QString extractFileTitle(const QString&);
    static CString extractFilePath(const CString&);
};
struct WizPathResolve {static QString root;static QString tempPath(){return root;}};
QString WizPathResolve::root;
}
QString WizGenGUIDLowerCaseLetterOnly(){return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');}
struct WizDatabase {
    QString fixture;
    bool extractZiwFileToFolder(const WIZDOCUMENTDATA&,const QString& path){return QDir().mkpath(path)&&WizCopyFolder(fixture,path,true);}
    bool exportToHtmlFile(const WIZDOCUMENTDATA&,const QString&);
};
#include "actual-resources.inc"

namespace {
class LocalOnly final:public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& info)override{
        const auto scheme=info.requestUrl().scheme();
        if(scheme!="file"&&scheme!="about"&&scheme!="data"&&scheme!="qrc"){++blocked;info.block(true);}
    }
};
bool put(const QString& path,const QByteArray& bytes){QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size();}
bool waitFor(const std::function<bool()>& predicate,int timeout=15000){
    if(predicate())return true;QEventLoop loop;QTimer tick,end;bool ready=false;
    QObject::connect(&tick,&QTimer::timeout,&loop,[&]{if(predicate()){ready=true;loop.quit();}});
    QObject::connect(&end,&QTimer::timeout,&loop,&QEventLoop::quit);
    tick.start(20);end.setSingleShot(true);end.start(timeout);loop.exec();return ready;
}
QVariant js(QWebEnginePage& page,const QString& code){
    struct State{bool done=false;QVariant value;};const auto s=std::make_shared<State>();
    page.runJavaScript(code,[s](const QVariant& value){s->value=value;s->done=true;});
    waitFor([s]{return s->done;},5000);return s->value;
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QTemporaryDir root;if(!root.isValid())return 2;
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",1));
    Utils::WizPathResolve::root=root.filePath("extract/");QDir().mkpath(Utils::WizPathResolve::root);
    WizDatabase db;db.fixture=root.filePath("archive/");QDir().mkpath(db.fixture+"index_files");
    QImage image(2,3,QImage::Format_ARGB32);image.fill(Qt::red);
    const QByteArray body="<!doctype html><meta charset='utf-8'><link rel='stylesheet' href='index_files/style.css'><img id='sample' src='index_files/image.png'>";
    if(!put(db.fixture+"index.html",body)||!image.save(db.fixture+"index_files/image.png","PNG")
       ||!put(db.fixture+"index_files/style.css","#sample {padding-left:7px}"))return 2;
    const QStringList titles={"plain","A&B","part#fragment?query","100%20literal",QString::fromUtf8("空 格 中文"),"quote\" and 'single'","A&amp;B",QString::fromUtf8("组合 & # ? %20 中文")};
    LocalOnly interceptor;QWebEngineProfile profile;profile.setUrlRequestInterceptor(&interceptor);
    int checks=0,failures=0;
    for(const auto& title:titles)for(bool rendered:{false,true}){
        const auto directory=root.filePath(QString::number(checks));QDir().mkpath(directory);
        const QString target=directory+'/'+title+".html";QString error;
        const bool written=WizMarkdownExportTransaction::writeHtml(target,rendered?QString::fromUtf8(body):QString(),
            [&](const QString& staged){return db.exportToHtmlFile({},staged);},[]{return true;},error);
        QWebEnginePage page(&profile);auto loaded=std::make_shared<bool>(false),ok=std::make_shared<bool>(false);
        QObject::connect(&page,&QWebEnginePage::loadFinished,&page,[loaded,ok](bool success){*ok=success;*loaded=true;});
        if(written)page.load(QUrl::fromLocalFile(target));
        const bool finished=written&&waitFor([loaded]{return *loaded;});
        const auto result=finished?js(page,QStringLiteral("(function(){const i=document.getElementById('sample');return {complete:!!i&&i.complete,width:i&&i.naturalWidth,height:i&&i.naturalHeight,src:i&&i.src,padding:i&&getComputedStyle(i).paddingLeft};})()")).toMap():QVariantMap();
        const QString expected=directory+'/'+title.toHtmlEscaped()+"_files/image.png";
        const bool passed=finished&&*ok&&result.value("complete").toBool()&&result.value("width").toInt()==2
            &&result.value("height").toInt()==3&&result.value("padding").toString()=="7px"
            &&QUrl(result.value("src").toString()).toLocalFile()==expected&&QFileInfo::exists(expected);
        ++checks;failures+=!passed;qInfo().noquote()<<(passed?"PASS":"FAIL")<<(rendered?"rendered":"archive")<<title;
    }
    profile.setUrlRequestInterceptor(nullptr);
    if(interceptor.blocked.load())++failures;
    qInfo().noquote()<<QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"blockedRequests",interceptor.blocked.load()},
        {"actualWebEngine",true},{"actualDatabaseExporter",true},{"archiveExtractionIsSyntheticAdapter",true},{"realAccount",false},{"realNotes",false}}).toJson(QJsonDocument::Compact);
    return failures?1:0;
}
