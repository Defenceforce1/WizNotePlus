#pragma once
#include "share/WizDocumentUnprotect.h"
class QWidget;
class WizDocumentSaverThread;
namespace WizUnprotectDialog {
QString resultMessage(const WizDocumentUnprotect::Result&);
struct Services {
    std::function<QString(QWidget*,bool*)> password;
    std::function<bool(QWidget*,const QString&)> confirm;
    std::function<void(QWidget*,const WizDocumentUnprotect::Result&)> report;
    std::function<bool()> contextCurrent;
};
WizDocumentUnprotect::Result run(QWidget*,WizDatabase&,const WIZDOCUMENTDATA&,WizDocumentSaverThread*,Services={});
}
