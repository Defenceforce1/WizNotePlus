// Standalone actual helper regression. Synthetic temporary files only.
#include "share/WizPdfExport.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <limits>
#include <utility>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    int failures=0;
    const auto check=[&](bool ok,const char* name){qInfo().noquote()<<(ok?"PASS":"FAIL")<<name;failures+=!ok;};
    const auto read=[](const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
    const QString path=temporary.filePath(QString::fromUtf8("输出 space#%.pdf"));
    const QByteArray oldPdf("%PDF-1.7\n% synthetic original\n%%EOF\n"),newPdf("%PDF-1.7\n% synthetic replacement\n%%EOF\n");
    QString error;
    check(WizPdfExport::writeFile(path,oldPdf,error)&&error.isEmpty()&&read(path)==oldPdf,"write new synthetic bytes to Unicode/space/hash/percent path");
    for(const QByteArray& invalid:{QByteArray(),QByteArray("not a PDF"),QByteArray("%PDF-1.7\npartial")}) {
        check(!WizPdfExport::writeFile(path,invalid,error)&&!error.isEmpty()&&read(path)==oldPdf,"invalid/cancelled/partial render preserves old file");
    }
    check(WizPdfExport::writeFile(path,newPdf,error)&&error.isEmpty()&&read(path)==newPdf,"complete render atomically replaces old bytes");
    const auto directory=temporary.filePath("directory.pdf");QDir().mkdir(directory);
    check(!WizPdfExport::writeFile(directory,newPdf,error)&&QFileInfo(directory).isDir(),"directory target is not replaced");
    check(!WizPdfExport::writeFile(temporary.filePath("missing/output.pdf"),newPdf,error)&&!error.isEmpty(),"missing output parent reports failure");
    const QString lockedDir=temporary.filePath("locked");QDir().mkdir(lockedDir);
    const QString lockedFile=QDir(lockedDir).filePath("existing.pdf");
    if(!WizPdfExport::writeFile(lockedFile,oldPdf,error))return 3;
    const auto permissions=QFileInfo(lockedDir).permissions();
    QFile::setPermissions(lockedDir,QFileDevice::ReadOwner|QFileDevice::ExeOwner);
    const bool preserved=!WizPdfExport::writeFile(lockedFile,newPdf,error)&&read(lockedFile)==oldPdf;
    QFile::setPermissions(lockedDir,permissions);
    check(preserved,"nonwritable directory refuses direct-write fallback and preserves old PDF");
    const auto files=QDir(temporary.path()).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot);
    check(files.size()==1,"failed writes leave no partial file alongside the destination");
    for(const auto sample:{std::pair<int,double>{0,5},{1,72},{2,1},{3,6},{4,72},{5,6}}) {
        const auto layout=WizPdfExport::pageLayout(sample.first,QMarginsF(sample.second,sample.second,sample.second,sample.second));
        const auto mm=layout.margins(QPageLayout::Millimeter).top();
        const bool expected=sample.first==0?std::abs(mm-5)<.01:sample.first<=3?std::abs(mm-25.4)<.01:std::abs(mm-27.07)<.02;
        check(layout.isValid()&&expected,"stored margin index uses the selected physical unit");
    }
    check(!WizPdfExport::pageLayout(-1,QMarginsF()).isValid()&&!WizPdfExport::pageLayout(6,QMarginsF()).isValid(),"invalid unit indexes are rejected");
    check(!WizPdfExport::pageLayout(0,QMarginsF(std::numeric_limits<double>::quiet_NaN(),5,5,5)).isValid(),"nonfinite margins are rejected");
    check(!WizPdfExport::pageLayout(0,QMarginsF(200,5,200,5)).isValid(),"empty printable areas are rejected");
    check(WizPdfExport::pageLayout(0,QMarginsF(-1,-1,-1,-1)).margins()==QMarginsF(),"negative margins retain Qt zero-clamping behavior");
    qInfo()<<"PDF helper failures:"<<failures;return failures?1:0;
}
