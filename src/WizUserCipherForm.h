#ifndef WIZUSERCIPHERFORM_H
#define WIZUSERCIPHERFORM_H

#include <QWidget>
#include "WizDef.h"
#include "widgets/WizBiometricUnlockWidget.h"

namespace Ui {
class WizUserCipherForm;
}

class QPropertyAnimation;
class WizUserCipherForm : public QWidget
{
    Q_OBJECT
    
public:
    explicit WizUserCipherForm(WizExplorerApp& app, QWidget *parent);
    ~WizUserCipherForm() override;
    WizUserCipherForm(WizExplorerApp& app, QWidget* parent, WizBiometricUnlockWidget::Services services);
    void prepareBiometric(const WizBiometricUnlockWidget::Context& context, bool enabled);
    void cancelBiometric();
    bool preserveNativeAuthenticationOnInactive() const;
    WizBiometricKeychain::Result takeBiometricCredential(const WizBiometricUnlockWidget::Context& context);

    void setPasswordText(const QString& strPasswordText);
    void setHint(const QString& strHint);

    const QString& userCipher() const { return m_userCipher; }
    bool isSaveForSession() const { return m_bSaveForSession; }

//    void sheetShow();
    void setCipherEditorFocus();

    void cipherError();
    void cipherCorrect();

protected:
    virtual QSize sizeHint();
    virtual void showEvent(QShowEvent* event);
    
private:
    Ui::WizUserCipherForm *ui;
    WizExplorerApp& m_app;

    bool m_bSaveForSession;
    QString m_userCipher;
    QPropertyAnimation* m_animation;
    WizBiometricUnlockWidget* m_biometric;
    WizDocumentCallbackEpoch m_manualRequestEpoch;

Q_SIGNALS:
    void cipherCheckRequest();
    void biometricCredentialReady();

public Q_SLOTS:
    void onButtonOK_clicked();
    void onCheckSave_stateChanged(int state);
    void onCipher_changed(const QString& text);

};

#endif // WIZUSERCIPHERFORM_H
