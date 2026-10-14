#include "WizAboutDialog.h"
#include "WizDef.h"
#include "WizBuildIdentity.h"

#include <QApplication>
#include <QLabel>
#include <QFileInfo>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QDateTime>
#include <QDate>
#include <QTime>

#include <QTextStream>

WizAboutDialog::WizAboutDialog(QWidget *parent)
    : QDialog(parent)
{
    QLabel* labelIcon = new QLabel(this);
    labelIcon->setPixmap(qApp->windowIcon().pixmap(QSize(58, 58)));

#if defined Q_OS_MAC
    QString strProduct("<span style='font-weight:bold;font-size:14px'>WizNotePlus for Mac</span>");
#elif defined Q_OS_LINUX
    QString strProduct("<span style='font-weight:bold;font-size:14px'>WizNotePlus for Linux</span>");
#else
    QString strProduct("<span style='font-weight:bold;font-size:14px'>WizNotePlus for Windows</span>");
#endif

    QLabel* labelProduct = new QLabel(this);
    labelProduct->setObjectName("wiz-about-product");
    if (WizBuildIdentity::appleSiliconTrial)
        strProduct = QStringLiteral("<span style='font-weight:bold;font-size:14px'>%1</span>")
            .arg(WizBuildIdentity::displayName().toHtmlEscaped());
    labelProduct->setText(strProduct);
    labelProduct->setTextInteractionFlags(Qt::TextBrowserInteraction);

    // Construct build information
    QString versionInfo = QString(
        tr("<span style='font-size:11px'>Version: %1</span><br/>")).arg(WIZ_CLIENT_VERSION);
    QString devInfo = QString(
        tr("<span style='font-size:11px'>Development Stage: %1</span><br/>").arg(WIZ_DEV_STAGE));
    if (WizBuildIdentity::appleSiliconTrial) {
        versionInfo = QStringLiteral("<span style='font-size:11px'>基础版本：WizNotePlus %1</span><br/>")
            .arg(QString::fromLatin1(WIZ_CLIENT_VERSION));
        devInfo = QStringLiteral("<span style='font-size:11px'>候选构建：%1（临时签名，未公证）</span><br/>")
            .arg(WizBuildIdentity::trialBuild());
    }
    QString qtInfo = QString(
        tr("<span style='font-size:11px'>Build against: Qt %1</span><br/>").arg(QT_VERSION_STR));
    QString buildTime = QString(
        tr("<span style='font-size:11px'>Build Time: %1 %2</span>")).arg(__DATE__, __TIME__);

    QString buildInfo = "<div style='text-align:center'>" + versionInfo + devInfo + qtInfo + buildTime +"</div>";
    QLabel* labelBuildInfo = new QLabel(this);
    labelBuildInfo->setObjectName("wiz-about-build-info");
    labelBuildInfo->setText(buildInfo);
    labelBuildInfo->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);

    // Construct credits information
    QTextBrowser* textCredits = new QTextBrowser(this);
    textCredits->setOpenExternalLinks(true);
    textCredits->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QString strHtml;
    QFile file(":/credits.html");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        strHtml = stream.readAll();
        file.close();
    }
    textCredits->setHtml(strHtml);

    QLabel* labelCopyright = new QLabel(this);
    labelCopyright->setText(
        tr("<span style=\"font-size:10px\">Copyright 2011-2017 Beijing Wozhi Technology Co., Ltd. All rights reserved.</span>"));

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    QVBoxLayout* layout = new QVBoxLayout(this);

    mainLayout->addLayout(layout);
    layout->setContentsMargins(0, 10, 0, 10);

    layout->addWidget(labelIcon);
    layout->setAlignment(labelIcon, Qt::AlignCenter);

    layout->addWidget(labelProduct);
    layout->setAlignment(labelProduct, Qt::AlignCenter);

    layout->addWidget(labelBuildInfo);
    layout->setAlignment(labelBuildInfo, Qt::AlignCenter);

    layout->addSpacing(50);
    
    layout->addWidget(labelCopyright);
    layout->setAlignment(labelCopyright, Qt::AlignCenter);

    mainLayout->addWidget(textCredits);

    setWindowTitle(WizBuildIdentity::appleSiliconTrial ? QStringLiteral("关于 WizNotePlus") : tr("About WizNote"));
}
