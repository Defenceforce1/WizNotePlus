"""Compile actual native URL-paste handlers against Qt using synthetic MIME data.
The clipboard, warning UI, resource conversion and attachment DB are test doubles;
no system clipboard, real notes or network are accessed. QUrl/QMimeData/QFileInfo
and image encoding/format discovery are real Qt. Usage: --source FILE --sdk DIR --output NEW_DIR.
"""
from pathlib import Path
import argparse,subprocess,json,os,re
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
s=a.source.read_text(encoding='utf-8-sig')
def method(signature):
 start=s.index(signature);end=s.index('\n}\n',start)+3;return s[start:end]
methods=method('bool WizDocumentWebView::onPasteCommand()')+'\n'+method('bool WizDocumentWebView::acceptUrlAsAttachment(const QUrl &url)')
pre=r'''
#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <memory>
using CString=QString;
static QString root;static int warnings=0,attachments=0,conversions=0,inserts=0,thumbnails=0;static QString lastWarning,lastAttachment;
struct TestClipboard{QMimeData* data=nullptr;const QMimeData* mimeData(){return data;}QImage image(){return qvariant_cast<QImage>(data->imageData());}};static TestClipboard clipboard;
struct ClipboardSource{static TestClipboard* clipboard(){return &::clipboard;}};
struct WizMessageBox{static void warning(void*,const QString&,const QString& value){warnings++;lastWarning=value;}};
struct WIZDOCUMENTDATA{QString strKbGUID="synthetic-kb";};struct WIZDOCUMENTATTACHMENTDATA{QString strKbGUID,strGUID="synthetic-attachment";};
struct TestView{WIZDOCUMENTDATA note(){return {};}};
struct TestDatabase{bool addAttachment(const WIZDOCUMENTDATA&,const QString& name,WIZDOCUMENTATTACHMENTDATA&){attachments++;lastAttachment=name;return true;}};
struct TestManager{TestDatabase db_;TestDatabase& db(const QString&){return db_;}};using WizDatabase=TestDatabase;
static bool wizIsYosemiteFilePath(const QString&){return false;}static QString wizConvertYosemiteFilePathToNormalPath(const QString& s){return s;}
static int WizGetTickCount(){return 359;}static QString WizIntToStr(int x){return QString::number(x);}
static bool WizImage2Html(const QString&,QString& html,const QString&){conversions++;html="<img alt='synthetic'>";return true;}
#define TOLOG(x) ((void)0)
#define TOLOG1(x,y) ((void)0)
struct Permit{std::shared_ptr<bool> allowed;bool current()const{return *allowed;}};
class WizDocumentWebView:public QObject{public:std::shared_ptr<bool> allowed=std::make_shared<bool>(true);bool editing=true;TestManager m_dbMgr;TestView host;
Permit noteReadPermit(){return {allowed};}bool isEditing(){return editing;}TestView* view(){return &host;}QString noteResourcesPath(){return root+"/resources/";}
void editorCommandExecuteInsertHtml(const QString&,bool){inserts++;}void addAttachmentThumbnail(const QString&,const QString&){thumbnails++;}
bool onPasteCommand();bool acceptUrlAsAttachment(const QUrl&);};
#define QApplication ClipboardSource
#define QClipboard TestClipboard
'''
post=r'''
#undef QApplication
#undef QClipboard
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;root=QString::fromLocal8Bit(argv[1]);QDir().mkpath(root+"/resources");int checks=0;QJsonArray failed;auto ck=[&](bool ok,const char* msg){checks++;if(!ok)failed.append(msg);};auto reset=[](){warnings=attachments=conversions=inserts=thumbnails=0;lastWarning.clear();lastAttachment.clear();};WizDocumentWebView web;
for(const QString& text:QStringList{"https://example.invalid/a?q=1#part","http://example.invalid/中文","mailto:person@example.invalid","ftp://example.invalid/file","wiz://open_document?guid=synthetic","relative/path",""}){
 reset();QMimeData mime;mime.setUrls({QUrl(text)});mime.setText(text);mime.setHtml("<a href='https://example.invalid/'>Synthetic URL</a>");clipboard.data=&mime;
 ck(!web.onPasteCommand(),"nonlocal URL falls back to normal paste");ck(warnings==0&&attachments==0&&conversions==0&&inserts==0,"nonlocal URL has no attachment/image side effects");ck(mime.text()==text&&mime.hasHtml(),"text and rich MIME unchanged");
}
reset();ck(!web.acceptUrlAsAttachment(QUrl("file:"))&&warnings==0,"empty local path is quietly rejected");
QString file=root+"/synthetic file.txt";{QFile f(file);ck(f.open(QIODevice::WriteOnly),"create nonempty fixture");f.write("synthetic attachment");}QUrl local=QUrl::fromLocalFile(file);
{reset();QMimeData mime;mime.setUrls({local});mime.setText(file);QImage preview(4,3,QImage::Format_RGB32);preview.fill(Qt::blue);mime.setImageData(preview);clipboard.data=&mime;ck(web.onPasteCommand(),"local file consumed as attachment");ck(attachments==1&&lastAttachment==file&&thumbnails==1&&warnings==0,"decoded local file delivered once");ck(conversions==0&&inserts==0,"Finder file preview not pasted as image");}
{reset();QMimeData mime;mime.setUrls({QUrl("https://example.invalid/"),local});clipboard.data=&mime;ck(web.onPasteCommand()&&attachments==1&&warnings==0,"mixed URL list preserves local-file route without remote warning");}
QString empty=root+"/empty.txt";{QFile f(empty);ck(f.open(QIODevice::WriteOnly),"create empty fixture");}reset();ck(!web.acceptUrlAsAttachment(QUrl::fromLocalFile(empty))&&warnings==1&&lastWarning.contains(empty)&&attachments==0,"real zero-byte attachment keeps named warning");
QString image=root+"/synthetic.png";QImage img(4,3,QImage::Format_RGB32);img.fill(Qt::red);ck(img.save(image),"create image fixture");
{reset();QMimeData mime;mime.setUrls({QUrl::fromLocalFile(image)});clipboard.data=&mime;ck(web.onPasteCommand()&&conversions==1&&inserts==1&&attachments==0&&warnings==0,"local image keeps image insertion");}
{reset();QMimeData mime;mime.setImageData(img);clipboard.data=&mime;ck(web.onPasteCommand()&&conversions==1&&inserts==1&&attachments==0&&warnings==0,"image-only clipboard keeps image insertion");}
{reset();QMimeData mime;mime.setText("Plain text with https://example.invalid");clipboard.data=&mime;ck(!web.onPasteCommand()&&warnings==0&&attachments==0,"plain text falls through");}
{reset();QMimeData mime;mime.setUrls({local});clipboard.data=&mime;*web.allowed=false;ck(web.onPasteCommand(),"revoked paste remains consumed");ck(!web.acceptUrlAsAttachment(local)&&warnings==0&&attachments==0&&inserts==0,"revoked attachment remains blocked");*web.allowed=true;web.editing=false;ck(!web.onPasteCommand()&&attachments==0,"read-mode route unchanged");}
QJsonObject result{{"passed",failed.isEmpty()},{"checks",checks},{"failures",failed},{"actualMethodsExtracted",true},{"systemClipboardAccessed",false},{"attachmentDatabaseStubbed",true},{"physicalPasteOrBrowserInsertionQualified",false}};QFile out(root+"/result.json");if(!out.open(QIODevice::WriteOnly))return 3;out.write(QJsonDocument(result).toJson());return failed.isEmpty()?0:1;}
'''
(a.output/'main.cpp').write_text(pre+methods+post)
cmd=['clang++','-std=c++17','-fPIC',str(a.output/'main.cpp'),'-o',str(a.output/'test'),'-F'+str(a.sdk/'lib'),'-Wl,-rpath,'+str(a.sdk/'lib')]
for name in ['QtCore','QtGui']:cmd+=['-I'+str(a.sdk/'lib'/f'{name}.framework/Headers'),'-framework',name]
r=subprocess.run(cmd,capture_output=True,text=True);(a.output/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.returncode)
env={k:v for k,v in os.environ.items() if not re.match(r'^(Malloc|DYLD_|QT_|QSG_|WIZNOTE_)',k)}
for n in ['home','temp','config','cache']:(a.output/n).mkdir()
env.update(HOME=str(a.output/'home'),TMPDIR=str(a.output/'temp')+'/',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str(a.output/'cache'),WIZNOTE_DATA_STORE=str(a.output/'store'))
r=subprocess.run([str(a.output/'test'),str(a.output)],capture_output=True,text=True,env=env,timeout=30);(a.output/'runtime.log').write_text(r.stdout+r.stderr);print((a.output/'result.json').read_text() if (a.output/'result.json').exists() else 'No result');raise SystemExit(r.returncode)
