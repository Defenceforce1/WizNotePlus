#include "database/WizIndexBase.h"
#include "database/WizThumbIndex.h"
#include "WizDef.h"
#include "utils/WizPathResolve.h"
#include "utils/WizLogger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QTemporaryDir>
#include <QFileInfo>

// Only resource lookup and logging are isolated. Real CppSQLite3, index methods,
// SQL files and QString/object conversion helpers are linked unchanged.
static QString resources;
static QString scratch;
QString Utils::WizPathResolve::resourcesPath(){return resources+"/";}
QString Utils::WizPathResolve::tempPath(){return scratch+"/";}
void Utils::WizLogger::writeLog(const QString& text){qWarning().noquote()<<text;}
static QByteArray logicalSnapshot(CppSQLite3DB& db,int* count=nullptr){
 QJsonArray snapshot;QStringList tables;{auto q=db.execQuery("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name");while(!q.eof()){tables<<q.getStringField(0);q.nextRow();}}
 for(const auto& table:tables){auto q=db.execQuery("SELECT * FROM \""+table+"\" ORDER BY rowid");QJsonArray rows;while(!q.eof()){QJsonArray row;for(int col=0;col<q.numFields();++col){if(q.fieldIsNull(col))row.append(QJsonValue(QJsonValue::Null));else if(q.fieldDataType(col)==SQLITE_BLOB){int n=0;const auto* b=q.getBlobField(col,n);row.append("BLOB:"+QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b),n).toHex()));}else row.append(q.getStringField(col));}rows.append(row);q.nextRow();}snapshot.append(QJsonObject{{"table",table},{"rows",rows}});}
 if(count)*count=tables.size();
 return QCryptographicHash::hash(QJsonDocument(snapshot).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
}
class SyntheticIndex final:public WizIndexBase {
public:
 bool setTableStructureVersion(const QString& version)override{return execSQL("INSERT OR REPLACE INTO WIZ_META(META_NAME,META_KEY,META_VALUE) VALUES('TableStructure','Version',"+WizStringToSQL(version)+")");}
 QString getTableStructureVersion()override{CString version;getFirstRowFieldValue("SELECT META_VALUE FROM WIZ_META WHERE META_NAME='TableStructure' AND META_KEY='Version'",0,version);return version;}
 // Only metadata plumbing is synthetic. Actual updateTableStructure(0..5),
 // checkTable(), SQL resources and indexing queries execute unchanged.
};
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=4)return 2;resources=QString::fromLocal8Bit(argv[1]);
 const QString mode=QString::fromLocal8Bit(argv[2]),file=QString::fromLocal8Bit(argv[3]);int checks=0;
 const auto check=[&](bool ok,const char* label){++checks;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;if(!ok)std::exit(3);};
 check(mode=="create"||mode=="check","known synthetic mode");check(mode!="create"||!QFile::exists(file),"never overwrite a database during create");
 QFile guard(QFileInfo(file).dir().filePath(".wiz-synthetic-sqlite-upgrade"));check(guard.open(QIODevice::ReadOnly)&&guard.readAll()=="synthetic-only\n","isolated test directory marker required before opening databases");
 QTemporaryDir temporary(QFileInfo(file).dir().filePath("roundtrip-XXXXXX"));check(temporary.isValid(),"dedicated temporary roundtrip directory");scratch=temporary.path();
 try{
  SyntheticIndex index;check(index.open(file),"actual index opens and creates current SQL schema");
  check(index.getTableStructureVersion()=="5","existing version-five schema bookkeeping retained");
  if(mode=="create"){
   check(index.execSQL("BEGIN IMMEDIATE"),"begin synthetic transaction");
   for(int i=0;i<100;++i){const auto sql=QString("INSERT INTO WIZ_DOCUMENT(DOCUMENT_GUID,DOCUMENT_TITLE,DOCUMENT_LOCATION,DOCUMENT_TYPE,DOCUMENT_PROTECT,DT_CREATED,DT_MODIFIED,DT_DATA_MODIFIED,WIZ_VERSION,INFO_CHANGED,DATA_CHANGED) VALUES('doc-%1',%2,%3,'markdown',%4,'2020-01-01 12:00:00','2020-02-01 12:00:00','2020-02-01 12:00:00',%5,1,1)")
      .arg(i).arg(WizStringToSQL(QString("中文 ' 引号 %% _ 笔记-%1").arg(i))).arg(WizStringToSQL(i<90?"/学术/":"/Deleted Items/")).arg(i%2).arg(i==0?"9223372036854775000":"-1");check(index.execSQL(sql),"insert synthetic document via actual index SQL path");}
   check(index.execSQL("INSERT INTO WIZ_TAG(TAG_GUID,TAG_NAME,TAG_GROUP_GUID,TAG_POS) VALUES('tag-1','数学','',1);INSERT INTO WIZ_DOCUMENT_TAG(DOCUMENT_GUID,TAG_GUID) VALUES('doc-0','tag-1');INSERT INTO WIZ_DOCUMENT_ATTACHMENT(ATTACHMENT_GUID,DOCUMENT_GUID,ATTACHMENT_NAME,WIZ_VERSION) VALUES('attachment-1','doc-0','中文附件.pdf',-1);INSERT INTO WIZ_META(META_NAME,META_KEY,META_VALUE) VALUES('SYNC','Version','-1');"),"tag attachment and sync metadata retain existing schema");
   check(index.execSQL("COMMIT"),"commit synthetic database");
  }
  int count=0;check(index.getAllDocumentsSize(count,true)&&count==100,"actual document count includes trash");check(index.getAllDocumentsSize(count,false)&&count==90,"actual document count excludes trash");
  WIZDOCUMENTDATA doc;check(index.documentFromGuid("doc-0",doc),"actual document GUID lookup");check(doc.strTitle=="中文 ' 引号 %% _ 笔记-0"&&doc.strLocation=="/学术/","actual object conversion keeps title and folder");
  WIZTAGDATA tag;check(index.tagFromGuid("tag-1",tag)&&tag.strName=="数学","actual tag lookup and UTF8 object conversion");
  CWizDocumentDataArray docs;check(index.getDocumentsBySQLWhere("DOCUMENT_GUID IN (SELECT DOCUMENT_GUID FROM WIZ_DOCUMENT_TAG WHERE TAG_GUID='tag-1')",docs)&&docs.size()==1,"actual tag-associated document query");
  docs.clear();check(index.getDocumentsBySQLWhere("DOCUMENT_GUID IN (SELECT DOCUMENT_GUID FROM WIZ_DOCUMENT_ATTACHMENT WHERE ATTACHMENT_NAME LIKE '%中文%')",docs)&&docs.size()==1,"actual attachment subquery");
  docs.clear();check(index.getDocumentsBySQLWhere("WIZ_VERSION=-1 AND DOCUMENT_LOCATION NOT LIKE '/Deleted Items/%'",docs)&&docs.size()==89,"pending sync and trash filter remain unchanged");
  index.close();CppSQLite3DB db;db.open(file);check(db.isOpened(),"actual CppSQLite3 opens existing database");
  check(db.tableExists("WIZ_DOCUMENT")&&db.columnExists("WIZ_DOCUMENT","DOCUMENT_TITLE")&&!db.columnExists("WIZ_DOCUMENT","missing_column"),"wrapper metadata queries");
  check(db.columnExists("WIZ_DOCUMENT_PARAM","WIZ_VERSION"),"existing index schema upgrade path still runs");
  {auto q=db.execQuery("SELECT WIZ_VERSION,NULL AS N,DT_CREATED FROM WIZ_DOCUMENT WHERE DOCUMENT_GUID='doc-0'");check(q.getInt64Field(0)==9223372036854775000LL&&q.fieldIsNull(1)&&q.getStringField(1,"fallback")=="fallback","64-bit and NULL behavior unchanged");check(q.getTimeField(2).getYear()==2020,"actual date conversion");}
  {auto stmt=db.compileStatement("UPDATE WIZ_META SET META_VALUE=? WHERE META_NAME='SYNC' AND META_KEY='Version'");stmt.bind(1,"保留'引号");check(stmt.execDML()==1,"actual prepared UTF8 bind update");stmt.reset();stmt.bind(1,"-1");check(stmt.execDML()==1,"prepared reset and reuse");}
  db.execDML("BEGIN IMMEDIATE;UPDATE WIZ_DOCUMENT SET DOCUMENT_TITLE='discard' WHERE DOCUMENT_GUID='doc-0';ROLLBACK");{auto q=db.execQuery("SELECT DOCUMENT_TITLE FROM WIZ_DOCUMENT WHERE DOCUMENT_GUID='doc-0'");check(q.getStringField(0)=="中文 ' 引号 %% _ 笔记-0","rollback protects existing text");}
  bool rejected=false;try{db.execDML("INSERT INTO WIZ_DOCUMENT(DOCUMENT_GUID,DOCUMENT_TITLE) VALUES('doc-0','duplicate')");}catch(const CppSQLite3Exception& e){rejected=e.errorCode()==SQLITE_CONSTRAINT;}check(rejected&&db.execScalar("SELECT count(*) FROM WIZ_DOCUMENT")==100,"duplicate failure preserves original data and wrapper remains usable");
  const unsigned char binary[]={0,255,0,128,65};
  if(mode=="create")check(db.insertBlob("INSERT INTO WIZ_META(META_NAME,META_KEY,META_VALUE) VALUES('SYNTHETIC','Binary',?)",binary,sizeof(binary))==1,"actual wrapper inserts binary metadata without new schema");
  check(db.updateBlob("WIZ_META","META_VALUE",binary,sizeof(binary),"META_NAME='SYNTHETIC' AND META_KEY='Binary'")==1,"actual wrapper binary update");
  {auto q=db.execQuery("SELECT META_VALUE FROM WIZ_META WHERE META_NAME='SYNTHETIC' AND META_KEY='Binary'");int length=0;const auto* bytes=q.getBlobField(0,length);check(length==sizeof(binary)&&memcmp(bytes,binary,sizeof(binary))==0,"wrapper preserves all binary bytes");}
  {CppSQLite3DB other;other.open(file);other.setBusyTimeout(10);db.execDML("BEGIN IMMEDIATE");bool busy=false;try{other.execDML("UPDATE WIZ_META SET META_VALUE='unexpected' WHERE META_NAME='SYNC'");}catch(const CppSQLite3Exception& e){busy=e.errorCode()==SQLITE_BUSY;}db.execDML("ROLLBACK");check(busy&&other.execScalar("SELECT count(*) FROM WIZ_META WHERE META_NAME='SYNC' AND META_VALUE='-1'")==1,"busy failure leaves data and connections usable");other.close();}
  {auto q=db.execQuery("PRAGMA integrity_check");check(q.getStringField(0)=="ok","synthetic index integrity");}
  {auto q=db.execQuery("PRAGMA journal_mode");check(q.getStringField(0)=="delete","existing rollback journal mode retained");}
  check(db.execScalar("PRAGMA page_size")==1024&&db.execScalar("PRAGMA synchronous")==2&&db.execScalar("PRAGMA foreign_keys")==0,"original page and transaction defaults preserved");
  int tables=0;const auto snapshotSha=QString::fromLatin1(logicalSnapshot(db,&tables));
  db.close();check(!db.isOpened(),"wrapper closes cleanly");
  const QString rebuilt=temporary.filePath("rebuilt.db");check(!QFile::exists(rebuilt),"dump/read destination is a new synthetic file");check(CppSQLite3DB::repair(file,rebuilt),"actual legacy dump/read roundtrip on healthy synthetic data");
  {CppSQLite3DB copy;copy.open(rebuilt);check(QString::fromLatin1(logicalSnapshot(copy))==snapshotSha,"dump/read retains every synthetic logical row");auto q=copy.execQuery("PRAGMA integrity_check");check(q.getStringField(0)=="ok","rebuilt synthetic integrity");q.finalize();copy.close();}
  const QString thumbnail=file+".thumb";check(mode!="create"||!QFile::exists(thumbnail),"never overwrite thumbnail database during create");
  WizThumbIndex thumb;check(thumb.openThumb(thumbnail,WIZNOTE_THUMB_VERSION),"actual thumbnail database opens without version-policy change");
  WIZABSTRACT abstract;abstract.guid="synthetic-thumb";abstract.text="中文 ' 摘要";abstract.image=QImage(2,2,QImage::Format_ARGB32);abstract.image.fill(qRgba(25,64,128,255));
  if(mode=="create")check(thumb.updatePadAbstract(abstract)&&thumb.updateIphoneAbstract(abstract),"actual thumbnail text and PNG insert");
  WIZABSTRACT read;check(thumb.padAbstractFromGuid(abstract.guid,read)&&read.text==abstract.text&&read.image==abstract.image,"thumbnail text and pixels survive cross-engine read");
  check(thumb.updatePadAbstract(abstract)&&thumb.updateIphoneAbstract(abstract),"actual thumbnail PNG and text update");
  {WIZABSTRACT phone;check(thumb.phoneAbstractFromGuid(abstract.guid,phone)&&phone.text==abstract.text&&phone.image==abstract.image,"phone thumbnail remains unchanged");}
  thumb.closeThumb();check(!thumb.isThumbOpened(),"thumbnail database closes cleanly");
  qInfo().noquote()<<"SQLITE_WRAPPER_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"version",sqlite3_libversion()},{"mode",mode},{"checks",checks},{"failures",0},{"actualWrapper",true},{"actualIndexMethods",true},{"actualThumbnailMethods",true},{"healthyDumpReadRoundtrip",true},{"existingSchemaUpgradeTested",true},{"businessSchemaVersion",5},{"snapshotSha256",snapshotSha},{"tables",tables},{"syntheticOnly",true}}).toJson(QJsonDocument::Compact));return 0;
 }catch(const CppSQLite3Exception& e){qCritical().noquote()<<"FAIL wrapper exception"<<e.errorMessage();return 4;}
}
