#include "share/jsoncpp/json/json.h"
#include "share/WizObject.h"
#include "share/WizRequest.h"
#include "utils/WizLogger.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDebug>
#include <limits>
#include <cstdlib>
#include <vector>
#include <utility>

// The only substitute is logging. No transport call, account, database or notes.
void Utils::WizLogger::writeLog(const QString& text){qWarning().noquote()<<text;}
static int checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){qCritical()<<"FAIL"<<name;std::exit(3);}}
static Json::Value parse(const std::string& text){Json::Reader r;Json::Value v;check(r.parse(text,v),"legacy Reader parses synthetic text");return v;}
// JSON has one integral-number domain. Positive values written as UInt64 may
// parse back as Int64 in both versions; do not mislabel that as a regression.
static bool same(const Json::Value& a,const Json::Value& b){
 if(a.isIntegral()&&b.isIntegral())return a.asString()==b.asString();
 if(a.type()!=b.type())return false;
 if(a.isObject()){if(a.getMemberNames()!=b.getMemberNames())return false;for(const auto& key:a.getMemberNames())if(!same(a[key],b[key]))return false;return true;}
 if(a.isArray()){if(a.size()!=b.size())return false;for(Json::ArrayIndex i=0;i<a.size();++i)if(!same(a[i],b[i]))return false;return true;}
 return a==b;
}
static Json::Value corpus(){
 Json::Value a(Json::objectValue);
 a["title"]="中文公式 笔记 😀";a["path"]="/数学/测试.md";
 a["source"]="\\begin{equation}A+B=C\\end{equation}";
 a["quote"]="\"quoted\"\\slash\nline\ttab";
 a["nul"]=std::string("A\0B",3);a["empty"]="";a["null"]=Json::Value();
 a["negative"]=Json::Int64(-9223372036854775807LL);a["unsigned"]=std::numeric_limits<Json::UInt64>::max();
 a["version"]=Json::Int64(9007199254740993LL);a["time"]=Json::UInt64(1700000000000ULL);
 a["zero"]=0;a["fraction"]=1.25;a["scientific"]=1.0e-12;a["enabled"]=true;
 a["tags"]=Json::Value(Json::arrayValue);a["tags"].append("tag-a");a["tags"].append("tag-b");
 a["nested"]["emptyArray"]=Json::Value(Json::arrayValue);a["nested"]["emptyObject"]=Json::Value(Json::objectValue);
 return a;
}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 const QString mode=QString::fromLocal8Bit(argv[1]),file=QString::fromLocal8Bit(argv[2]);
 QFile marker(QFileInfo(file).dir().filePath(".wiz-synthetic-jsoncpp-upgrade"));
 check(marker.open(QIODevice::ReadOnly)&&marker.readAll()=="synthetic-only\n","explicit isolated marker before file access");
 check(mode=="create"||mode=="check","known mode");
 const auto expected=corpus();Json::FastWriter writer;const std::string own=writer.write(expected);
 check(!own.empty()&&own.back()=='\n',"FastWriter retains newline");
 check(same(parse(own),expected),"legacy Reader/FastWriter semantic roundtrip including UTF8/NUL/64-bit");
 if(mode=="create"){check(!QFile::exists(file),"never overwrite fixture");QFile out(file);check(out.open(QIODevice::WriteOnly|QIODevice::NewOnly),"create isolated fixture");check(out.write(own.data(),own.size())==qint64(own.size()),"write complete synthetic JSON");}
 QFile input(file);check(input.open(QIODevice::ReadOnly),"open cross-version fixture");const QByteArray bytes=input.readAll();input.close();
 check(same(parse(bytes.toStdString()),expected),"cross-version Reader preserves values and JSON types, allowing integral signedness normalization");
 const auto v=parse(bytes.toStdString());check(v["version"].asInt64()==9007199254740993LL&&v["unsigned"].asUInt64()==std::numeric_limits<Json::UInt64>::max(),"integer precision");
 check(v["nul"].asString().size()==3&&v["nul"].asString()[1]=='\0',"embedded NUL");
 check(v.get("missing","fallback").asString()=="fallback"&&!v.isMember("missing"),"existing missing/default behavior");
 check(v["tags"].size()==2&&v["nested"]["emptyArray"].isArray()&&v["null"].isNull(),"array object and null distinctions");
 Json::Value edit=v;edit.removeMember("empty");edit["tags"].append("tag-c");check(!edit.isMember("empty")&&edit["tags"].size()==3&&v["tags"].size()==2,"copy mutation iteration APIs");
 Json::Value copied=v;copied.clear();check(copied.empty()&&v.size()>0,"clear keeps original");
 const std::string ordinary[]={"{\"title\":\"中文\"}","{\"title\":\"\\u4e2d\\u6587\"}","[null,true,false,0,-2,1.25]","{ /* note */ \"a\":1 }","{\"a\":1,\"a\":2}","42","\"text\""};
 for(const auto& s:ordinary){const auto p=parse(s);check(same(parse(writer.write(p)),p),"ordinary legacy input roundtrip");}
 Json::Reader badReader;Json::Value bad;check(!badReader.parse("{\"a\":",bad),"ordinary truncated response rejected");check(!badReader.getFormattedErrorMessages().empty(),"parse error message remains available");
 Json::Value response;auto result=WizRequest::isSucceededStandardJsonRequest(QByteArray("{\"returnCode\":200,\"data\":{\"title\":\"synthetic\"}}"),response);
 check(result.returnCode==200&&response["data"]["title"].asString()=="synthetic","actual standard camel-case success parser");
 result=WizRequest::isSucceededStandardJsonRequest(QByteArray("{\"return_code\":200,\"return_message\":\"OK\"}"));check(result.returnCode==200,"actual snake-case success parser");
 result=WizRequest::isSucceededStandardJsonRequest(QByteArray("{\"return_code\":403,\"return_message\":\"synthetic denied\",\"externCode\":17}"));check(result.returnCode==403&&result.externCode=="17"&&result.returnMessage=="synthetic denied","actual ordinary error parser");
 result=WizRequest::isSucceededStandardJsonRequest(QByteArray("{"));check(result.returnCode==WIZSTANDARDRESULT::json,"actual truncated response returns JSON error");
 auto tagValue=parse("{\"kbGuid\":\"synthetic-kb\",\"tagGuid\":\"tag-a\",\"parentTagGuid\":\"\",\"name\":\"数学\",\"modified\":1700000000000,\"version\":9007199254740993,\"pos\":2}");
 WIZTAGDATA tag;check(tag.fromJson(tagValue)&&tag.strName==QStringLiteral("数学")&&tag.nVersion==9007199254740993LL,"actual tag conversion with UTF8 and version");
 Json::Value tagOut;check(tag.toJson("synthetic-kb",tagOut)&&tagOut["name"].asString()=="数学"&&tagOut["modified"].asUInt64()==1700000000000ULL,"actual tag serialization fields");
 auto docValue=parse("{\"kbGuid\":\"synthetic-kb\",\"docGuid\":\"doc-a\",\"title\":\"合成公式.md\",\"category\":\"/数学/\",\"type\":\"markdown\",\"tags\":\"tag-a*tag-b\",\"version\":17,\"protected\":1,\"attachmentCount\":2,\"created\":1700000000000,\"modified\":1700000001000,\"dataModified\":1700000002000}");
 WIZDOCUMENTDATAEX doc;check(doc.fromJson(docValue)&&doc.strTitle==QStringLiteral("合成公式.md")&&doc.nProtected==1&&doc.arrayTagGUID.size()==2,"actual document conversion without decrypting contents");
 doc.tAccessed=doc.tCreated;doc.nReadCount=3;Json::Value docOut;check(doc.toJson(docOut)&&docOut["protected"].asInt()==1&&docOut["tags"].asString()=="tag-a*tag-b"&&docOut["modified"].asUInt64()==1700000001000ULL,"actual document serialization including protected flag and dates");
 WIZDOCUMENTDATAEX again;check(again.fromJson(parse(writer.write(docOut)))&&again.strTitle==doc.strTitle&&again.nProtected==doc.nProtected&&again.tModified==doc.tModified,"actual document roundtrip");
 Json::StyledWriter styled;check(same(parse(styled.write(expected)),expected),"existing StyledWriter semantic roundtrip");
 Json::StreamWriterBuilder builder;builder["indentation"]="\t";
 check(same(parse(Json::writeString(builder,expected)),expected),"existing tab-indented export StreamWriter roundtrip");
 const auto documents=Json::writeString(builder,docOut);check(same(parse(documents),docOut),"actual document export metadata fields preserved");
 const auto plain=Json::writeString(builder,expected),formatted=styled.write(expected);
 for(const auto& pair:std::vector<std::pair<QString,std::string>>{{".styled",formatted},{".stream",plain}}){
  const auto p=file+pair.first;if(mode=="create"){QFile output(p);check(output.open(QIODevice::WriteOnly|QIODevice::NewOnly),"create additional writer fixture without overwrite");check(output.write(pair.second.data(),pair.second.size())==qint64(pair.second.size()),"write complete additional fixture");}
  QFile extra(p);check(extra.open(QIODevice::ReadOnly),"read cross-version formatted writer fixture");check(same(parse(extra.readAll().toStdString()),expected),"cross-version formatted output preserves full values");
 }
 Json::Value large;large["title"]=std::string(1024*1024,'A')+"中文😀";large["tags"]=Json::Value(Json::arrayValue);for(int i=0;i<1000;i++)large["tags"].append("synthetic-tag-"+std::to_string(i));
 check(same(parse(writer.write(large)),large),"one-MiB ordinary field and 1000-item list roundtrip");
 QFile unchanged(file);check(unchanged.open(QIODevice::ReadOnly)&&unchanged.readAll()==bytes,"cross-version source fixture remains unchanged");
 QJsonObject report{{"accepted",true},{"checks",checks},{"version",JSONCPP_VERSION_STRING},{"mode",mode},{"sourceSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"ownWriterSha256",QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(own),QCryptographicHash::Sha256).toHex())},{"transportInvoked",false},{"actualRequestAndObjectMethods",true}};
 qInfo().noquote()<<"REPORT"<<QJsonDocument(report).toJson(QJsonDocument::Compact);return 0;
}
