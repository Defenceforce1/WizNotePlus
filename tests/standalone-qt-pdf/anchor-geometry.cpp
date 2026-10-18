// Synthetic read-only diagnosis of native printToPdf named destinations.
// This executable intentionally includes/links no Wiz code or resources.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <atomic>
#include <memory>
#ifdef Q_OS_MAC
#include <mach-o/dyld.h>
#endif

class Offline final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto s=info.requestUrl().scheme();
        if(s!="data" && s!="about" && s!="blob") {++blocked;info.block(true);}
    }
};
static QVariant evaluate(QWebEnginePage* page,const QString& script) {
    struct State {QVariant value;bool done=false;};auto state=std::make_shared<State>();
    QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;timer.setSingleShot(true);
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->runJavaScript(script,[state,guard](const QVariant& value){state->value=value;state->done=true;if(guard)guard->quit();});
    timer.start(10000);if(!state->done)loop.exec();return state->value;
}
struct Case {
    const char* name;int width;QPageSize::PageSizeId size;QPageLayout::Orientation orientation;
    QMarginsF margins;QString css;
};
static QString html(const Case& sample) {
    QString body;
    for(int i=1;i<=3;++i) {
        body+=QString("<article class='sheet s%1'><nav><a href='#target-%1'>Jump to target %1</a></nav>"
                      "<div class='gap' style='height:%2px'></div><div class='target t%1' id='target-%1'>"
                      "<span>TARGET-%1</span></div></article>").arg(i).arg(i==1?32:i==2?200:480);
    }
    return QString("<!doctype html><meta charset='utf-8'><title>Isolated anchor geometry</title><style>"
        "html,body{margin:0;padding:0}body{width:%1px;font:16px sans-serif}"
        "article{padding:0 40px;break-before:page}article:first-child{break-before:auto}"
        "nav{height:24px;line-height:24px;background:rgb(230,230,230)}"
        ".target{width:320px;height:48px;padding:0;border:0;margin:0;break-inside:avoid}"
        ".t1{background:rgb(204,51,51)}.t2{background:rgb(51,153,51)}.t3{background:rgb(51,51,204)}"
        "%2</style><body>%3</body>").arg(sample.width).arg(sample.css,body);
}
int main(int argc,char** argv) {
    if(argc!=3){qCritical()<<"Usage: anchor-geometry MATCHING_QT_SDK NEW_EMPTY_OUTPUT_DIRECTORY";return 2;}
    const QDir sdk(QDir(QString::fromLocal8Bit(argv[1])).absolutePath());
    const QDir output(QDir(QString::fromLocal8Bit(argv[2])).absolutePath());
    if(!output.exists() || !output.entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot).isEmpty())return 2;
    const auto core=sdk.filePath("lib/QtWebEngineCore.framework/Versions/A");
    QJsonObject runtimeLibraries;
#ifdef Q_OS_MAC
    // A same-version framework from another rpath is not the requested runtime.
    // Verify before QApplication/profile creation, without launching WebEngine.
    for (const auto& name:QStringList{"QtCore", "QtGui", "QtWidgets", "QtQuick",
                                     "QtQuickWidgets", "QtWebEngineCore", "QtWebEngineWidgets"}) {
        const QString suffix="/"+name+".framework/Versions/A/"+name;
        const QString expected=QFileInfo(sdk.filePath("lib"+suffix)).canonicalFilePath();
        QString loaded;
        for (uint32_t i=0;i<_dyld_image_count();++i) {
            const QString image=QString::fromUtf8(_dyld_get_image_name(i));
            if (image.endsWith(suffix)) {loaded=QFileInfo(image).canonicalFilePath();break;}
        }
        if (expected.isEmpty() || loaded!=expected) {
            qCritical()<<"Matching runtime required before WebEngine startup:"<<name
                       <<"expected"<<expected<<"loaded"<<loaded;return 2;
        }
        runtimeLibraries.insert(name,loaded);
    }
#endif
    for (const auto& resource:QStringList{
            core+"/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess",
            core+"/Resources/icudtl.dat",core+"/Resources/qtwebengine_resources.pak",
            core+"/Resources/qtwebengine_resources_100p.pak",core+"/Resources/qtwebengine_resources_200p.pak",
            sdk.filePath("plugins/platforms/libqcocoa.dylib")}) {
        const QFileInfo info(resource);
        if(!info.isFile() || !info.isReadable() || !info.isAbsolute()) {
            qCritical()<<"Matching complete SDK required; missing/unreadable absolute resource:"<<resource;return 2;
        }
    }
    qputenv("QTWEBENGINEPROCESS_PATH",QFile::encodeName(core+"/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"));
    qputenv("QTWEBENGINE_RESOURCES_PATH",QFile::encodeName(core+"/Resources"));
    QTemporaryDir temporary;if(!temporary.isValid())return 2;
    qputenv("XDG_CACHE_HOME",QFile::encodeName(temporary.filePath("cache")));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setLibraryPaths({sdk.filePath("plugins")});
    QApplication app(argc,argv);app.setApplicationName("WizNoteQtAnchorGeometryProbe");
    Offline offline;QWebEngineProfile profile;profile.setUrlRequestInterceptor(&offline);
    QWebEngineView view;view.setPage(new QWebEnginePage(&profile,&view));view.resize(850,660);view.show();
    const Case cases[]={
        {"a4-zero-narrow",600,QPageSize::A4,QPageLayout::Portrait,{0,0,0,0},{}},
        {"a4-5mm-narrow",600,QPageSize::A4,QPageLayout::Portrait,{5,5,5,5},{}},
        {"a4-20mm-narrow",600,QPageSize::A4,QPageLayout::Portrait,{20,20,20,20},{}},
        {"a4-5mm-wide",1100,QPageSize::A4,QPageLayout::Portrait,{5,5,5,5},{}},
        {"a4-asymmetric-wide",1100,QPageSize::A4,QPageLayout::Portrait,{7,11,19,23},{}},
        {"letter-5mm-wide",1100,QPageSize::Letter,QPageLayout::Portrait,{5,5,5,5},{}},
        {"a4-landscape-5mm",1100,QPageSize::A4,QPageLayout::Landscape,{5,5,5,5},{}},
        {"a4-css20-api0",600,QPageSize::A4,QPageLayout::Portrait,{0,0,0,0},"@page{size:A4;margin:20mm}"},
        {"css-mixed-api5",600,QPageSize::A4,QPageLayout::Portrait,{5,5,5,5},
            "@page first{size:A4;margin:5mm}@page second{size:A5 landscape;margin:11mm}"
            "@page third{size:letter;margin:20mm}.s1{page:first}.s2{page:second}.s3{page:third}"}
    };
    QJsonArray records;int failures=0;
    for(const auto& sample:cases) {
        bool loaded=false;QEventLoop loadLoop;QTimer loadTimer;loadTimer.setSingleShot(true);
        QObject::connect(&loadTimer,&QTimer::timeout,&loadLoop,&QEventLoop::quit);
        QObject::connect(view.page(),&QWebEnginePage::loadFinished,&loadLoop,[&](bool ok){loaded=ok;loadLoop.quit();});
        view.setHtml(html(sample));loadTimer.start(15000);loadLoop.exec();
        if(!loaded){++failures;continue;}
        const auto before=evaluate(view.page(),"document.documentElement.outerHTML").toString();
        const auto screen=evaluate(view.page(),R"JS(Array.from(document.querySelectorAll('.target')).map(x=>{
            const r=x.getBoundingClientRect();return {id:x.id,x:r.x,y:r.y,width:r.width,height:r.height};}))JS");
        struct State {QByteArray pdf;bool done=false;};auto state=std::make_shared<State>();
        QEventLoop printLoop;QPointer<QEventLoop> guard(&printLoop);QTimer timer;timer.setSingleShot(true);
        QObject::connect(&timer,&QTimer::timeout,&printLoop,&QEventLoop::quit);
        const QPageLayout layout(QPageSize(sample.size),sample.orientation,sample.margins,QPageLayout::Millimeter);
        view.page()->printToPdf([state,guard](const QByteArray& bytes){state->pdf=bytes;state->done=true;if(guard)guard->quit();},layout);
        timer.start(30000);if(!state->done)printLoop.exec();
        const bool same=!before.isEmpty() && evaluate(view.page(),"document.documentElement.outerHTML").toString()==before;
        const auto file=QString::fromLatin1(sample.name)+".pdf";QFile pdf(output.filePath(file));
        const bool valid=state->done && state->pdf.startsWith("%PDF-") && state->pdf.right(1024).trimmed().endsWith("%%EOF");
        const bool saved=valid && pdf.open(QIODevice::WriteOnly|QIODevice::NewOnly) && pdf.write(state->pdf)==state->pdf.size();
        pdf.close();failures+=!same||!saved;
        records.append(QJsonObject{{"name",sample.name},{"file",file},{"layoutValid",layout.isValid()},
            {"htmlSame",same},{"saved",saved},{"widthCssPx",sample.width},
            {"apiMarginsMm",QJsonArray{sample.margins.left(),sample.margins.top(),sample.margins.right(),sample.margins.bottom()}},
            {"screenTargets",QJsonValue::fromVariant(screen)}});
        qInfo().noquote()<<(same&&saved?"PASS":"FAIL")<<sample.name;
    }
    failures+=offline.blocked.load()!=0;
    const QJsonDocument result(QJsonObject{{"kind","standalone Qt anchor geometry; no Wiz/editor/math code"},
        {"qt",qVersion()},{"webEngine",qWebEngineVersion()},{"chromium",qWebEngineChromiumVersion()},
        {"securityPatch",qWebEngineChromiumSecurityPatchVersion()},{"blockedRequests",offline.blocked.load()},
        {"offTheRecord",profile.isOffTheRecord()},{"registeredScripts",view.page()->scripts().count()},
        {"runtimeLibraries",runtimeLibraries},{"records",records},{"failures",failures}});
    QFile report(output.filePath("anchor-geometry-results.json"));
    if(!report.open(QIODevice::WriteOnly|QIODevice::NewOnly)||report.write(result.toJson())<0)return 2;
    qInfo()<<"Geometry generation failures:"<<failures;return failures?1:0;
}
