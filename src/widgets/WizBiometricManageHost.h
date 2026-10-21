#pragma once
#include "WizBiometricManageWidget.h"
class WizExplorerApp;

// Existing personal-certificate management only; no service registration, group
// certificate creation or automatic policy enablement. Normal settings exposes
// management only with valid self-signing prerequisites and an existing cert.
class WizBiometricManageHost final : public QWidget {
public:
    struct Services {
        bool explicitlyAvailable=false;
        std::function<std::shared_ptr<WizBiometricManageWidget::Operation>()> makeOperation;
        QThreadPool* pool=nullptr;
    };
    explicit WizBiometricManageHost(WizExplorerApp&,QWidget* parent=nullptr);
    WizBiometricManageHost(WizExplorerApp&,Services,QWidget* parent=nullptr);
    WizBiometricManageWidget* management()const{return m_management;}
private:
    WizBiometricManageWidget* m_management;
};
