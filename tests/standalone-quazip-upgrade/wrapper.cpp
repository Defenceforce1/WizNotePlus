#include "share/WizZip.h"
#include "quazip/quazip.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDebug>

static QByteArray readFile(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;const QString mode=QString::fromLocal8Bit(argv[1]),archive=QString::fromLocal8Bit(argv[2]);int checks=0;
 const auto check=[&](bool ok,const char* label){++checks;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;if(!ok)std::exit(3);};
 check(mode=="create"||mode=="check","known isolated mode");QFile guard(QFileInfo(archive).dir().filePath(".wiz-synthetic-quazip-upgrade"));check(guard.open(QIODevice::ReadOnly)&&guard.readAll()=="synthetic-only\n","synthetic directory marker required before archive access");
 QTemporaryDir scratch(QFileInfo(archive).dir().filePath("roundtrip-XXXXXX"));check(scratch.isValid(),"dedicated temporary files");
 QByteArray binary(1024*1024,Qt::Uninitialized);for(int i=0;i<binary.size();++i)binary[i]=char(i%256);
 const QByteArray html=QString("<!doctype html><meta charset=\"UTF-8\"><p>合成数学笔记 $A+B=C$</p>").toUtf8();
 const QList<QPair<QString,QByteArray>> payload={{"index.html",html},{"index_files/中文 图片.bin",binary},{"attachments/é-文档.txt",QByteArray("\0\1\xff\0\x80",5)},{"index_files/empty.dat",{}},{"note.md",QString("# 合成笔记\n\\begin{equation}A+B\\end{equation}\n").toUtf8()}};
 if(mode=="create"){
  check(!QFile::exists(archive),"never overwrite an archive during create");WizZipFile writer;check(writer.open(archive),"actual WizZipFile opens new archive");
  check(!writer.compressFile(scratch.filePath("missing-input"),"missing.bin"),"ordinary missing source failure reported");
  for(int i=0;i<payload.size();++i){const auto& p=payload[i];if(i==2){const QString file=scratch.filePath("attachment-source");QFile f(file);check(f.open(QIODevice::WriteOnly)&&f.write(p.second)==p.second.size(),"synthetic disk attachment source");f.close();check(writer.compressFile(file,p.first),"actual disk file compression");}else check(writer.compressFile(p.second,p.first),"actual memory compression preserves existing flow");}
  check(writer.close(),"writer closes successfully after handled missing-source failure");
 }
 const auto sourceBefore=QCryptographicHash::hash(readFile(archive),QCryptographicHash::Sha256);
 WizUnzipFile reader;check(reader.open(archive),"actual file reader opens archive");check(reader.count()==payload.size(),"entry count unchanged");
 for(const auto& p:payload){QByteArray data;check(reader.fileNameToIndex(p.first)>=0,"Unicode and nested file name retained");check(reader.extractFile(p.first,data)&&data==p.second,"actual memory extraction exact bytes");}
 QByteArray missing;check(!reader.extractFile("absent-entry",missing),"ordinary missing entry reported");
 QString document;CWizStdStringArray resources;check(reader.readMainHtmlAndResources(document,resources)&&document==QString::fromUtf8(html)&&resources.size()==4,"existing main HTML and resource enumeration");
 const auto destination=scratch.filePath("single.bin");check(reader.extractFile(payload[1].first,destination)&&readFile(destination)==binary,"actual disk extraction exact bytes");
 const auto directory=scratch.filePath("directory-target");check(QDir().mkpath(directory),"synthetic failure destination");check(!reader.extractFile(payload[1].first,directory)&&QFileInfo(directory).isDir(),"ordinary write failure preserves destination directory");
 const auto expanded=scratch.filePath("expanded");check(QDir().mkpath(expanded)&&reader.extractAll(expanded),"actual existing whole archive extraction");
 for(const auto& p:payload)check(QFile::exists(expanded+"/"+p.first)&&readFile(expanded+"/"+p.first)==p.second,"whole archive extraction exact bytes");
 check(reader.close(),"file reader closes");WizUnzipFile memory;check(memory.open(readFile(archive)),"actual QBuffer archive open");
 for(const auto& p:payload){QByteArray data;check(memory.extractFile(p.first,data)&&data==p.second,"QBuffer extraction exact bytes");}check(memory.close(),"memory reader closes");
 QuaZip info(archive);check(info.open(QuaZip::mdUnzip),"entry metadata available");QJsonArray entries;for(const auto& p:info.getFileInfoList())entries.append(QJsonObject{{"name",p.name},{"crc",QString::number(p.crc)},{"size",int(p.uncompressedSize)},{"method",p.method},{"flags",p.flags},{"versionNeeded",p.versionNeeded}});info.close();
 check(QCryptographicHash::hash(readFile(archive),QCryptographicHash::Sha256)==sourceBefore,"all reads and failures preserve source archive bytes");
 qInfo().noquote()<<"QUAZIP_WRAPPER_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"version",WIZ_QUAZIP_VERSION},{"mode",mode},{"checks",checks},{"failures",0},{"actualWizZip",true},{"syntheticOnly",true},{"entries",entries}}).toJson(QJsonDocument::Compact));return 0;
}
