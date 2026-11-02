// Build with prepare-html-attribute-lifetime.py instrumentation in an isolated copy.
// Actual parsing and ownership code are unchanged by the test generator.
#include "html/WizHtmlReader.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QFile>
#include <malloc/malloc.h>
long wizHtmlAttributesCreated=0,wizHtmlAttributesDestroyed=0;
int main(int argc,char**argv){if(argc!=2)return 2;QCoreApplication app(argc,argv);int checks=0;bool pass=true;QJsonArray failures,values;
auto ck=[&](bool ok,const char* id){checks++;if(!ok){pass=false;failures.append(id);}};auto live=[](){return wizHtmlAttributesCreated-wizHtmlAttributesDestroyed;};
{
 WizHtmlAttributes a;CString src("DATA-ID='one &amp; two' width=42 disabled");ck(a.parseFromStr(src)==src.length(),"parse-length");ck(a.getCount()==3,"three-attributes");ck(a.getValueFromName("data-id")=="one & two","entity-case");ck(a.getValueFromName("width")=="42","unquoted-value");ck(a.getValueFromName("disabled").isEmpty(),"boolean-value");
 auto retained=a.getAttribute("data-id");long old=live();ck(a.removeAttribute("DATA-ID"),"remove-name");ck(live()==old-1,"remove-releases-item");ck(a.getCount()==2&&retained.getValue()=="one & two","copy-survives-remove");ck(!a.removeAttribute(-1)&&!a.removeAttribute(9)&&!a.removeAttribute("missing"),"invalid-removal");
 a.setValueToName("width","43");ck(a.getCount()==2&&a.getValueFromName("WIDTH")=="43","replace-value");WizHtmlAttributes copy(a,true);a.removeAll();ck(copy.getCount()==2&&copy.getValueFromName("width")=="43","deep-copy-survives-clear");WizHtmlAttributes moved(copy);ck(copy.getCount()==0&&moved.getCount()==2,"transfer-ownership");CString next("src='data:image/png;base64,AAAA' title='中文 &lt;math&gt;'");moved.parseFromStr(next);ck(moved.getCount()==2&&moved.getValueFromName("title")==QString::fromUtf8("中文 <math>"),"replace-parsed-collection");CString invalid("=invalid");ck(moved.parseFromStr(invalid)==0&&moved.getCount()==2,"invalid-preserves-collection");values.append(QString(moved.getValueFromName("title")));moved.removeAll();ck(moved.removeAll()&&moved.getCount()==0,"repeat-clear");
}
ck(live()==0,"scope-releases-all");
{
 CString original("<img src='data:image/png;base64,AAAA' title='a &amp; b' width='42'>");WizHtmlTag t;bool opening=false,closing=false;ck(t.parseFromStr(original,opening,closing)>0&&opening&&!closing,"tag-parse");ck(t.getTag()==original,"original-tag-preserved");WizHtmlTag copy(t,true);t.removeAttribute("width");ck(t.getAttributes()->getCount()==2&&copy.getAttributes()->getCount()==3,"tag-copy-independent");ck(copy.getValueFromName("title")=="a & b","tag-decoded-value");t.setValueToName("title","updated");values.append(QString(t.getTag()));WizHtmlTag moved(copy);ck(moved.getAttributes()&&moved.getAttributes()->getCount()==3&&!copy.getAttributes(),"tag-transfer");
}
ck(live()==0,"tag-scope-releases-all");
const long beforeLive=live();malloc_statistics_t before{};malloc_zone_statistics(nullptr,&before);
for(int i=0;i<100;i++){WizHtmlAttributes a;CString src(QString("src='data:image/png;base64,")+QString(32000,QChar('A'))+"' title='value &amp; text'");a.parseFromStr(src);ck(a.getCount()==2&&a.getValueFromName("src").length()==32022,"large-value-preserved");a.setValueToName("title","replacement");ck(a.getValueFromName("title")=="replacement","replace-loop");a.removeAttribute("title");ck(a.getCount()==1,"remove-loop");}
malloc_statistics_t after{};malloc_zone_statistics(nullptr,&after);ck(live()==beforeLive,"stress-no-retained-items");
QJsonObject r{{"passed",pass},{"checks",checks},{"failures",failures},{"created",qint64(wizHtmlAttributesCreated)},{"destroyed",qint64(wizHtmlAttributesDestroyed)},{"remaining",qint64(live())},{"activeGrowthBytes",qint64(after.size_in_use)-qint64(before.size_in_use)},{"behaviorDigest",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(values).toJson(),QCryptographicHash::Sha256).toHex())}};QFile f(QString::fromLocal8Bit(argv[1])+"/result.json");if(!f.open(QIODevice::WriteOnly))return 3;f.write(QJsonDocument(r).toJson());return pass?0:1;}
