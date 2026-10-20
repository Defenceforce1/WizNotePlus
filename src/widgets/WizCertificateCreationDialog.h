#ifndef WIZ_CERTIFICATE_CREATION_DIALOG_H
#define WIZ_CERTIFICATE_CREATION_DIALOG_H

#include "share/WizCertificateCreation.h"
#include <QDialog>
#include <QPointer>
#include <functional>
#include <memory>

class QCheckBox;
class QFileDialog;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

// Offline, unregistered backup UI only. Not connected to the account menu or
// database; it cannot install, replace, register or activate a certificate.
class WizCertificateCreationDialog : public QDialog {
    Q_OBJECT
public:
    enum class Stage { Input, Generating, Ready, Saving, Saved, Unconfirmed, Closed };
    using Selection = std::function<void(const QString&)>;
    struct Services {
        std::function<WizCertificateCreation::Result(const QString&, const QString&, const QString&)> generate;
        std::function<WizCertificateCreation::BackupResult(const QString&,
            const WizCertificateCreation::Certificate&, const QString&)> save;
        // GUI-thread, one-shot completion. Empty path means cancel. Tests supply
        // this seam instead of showing a real user's save panel.
        std::function<void(QWidget*, const Selection&)> choosePath;
        QString initialDirectory;
    };
    explicit WizCertificateCreationDialog(QWidget* parent = nullptr);
    explicit WizCertificateCreationDialog(Services services, QWidget* parent = nullptr);
    ~WizCertificateCreationDialog() override;
    Stage stage() const { return m_stage; }
    QString backupPath() const { return m_backupPath; }
    void accept() override;
    void reject() override;
    void done(int code) override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    struct Task;
    void generate();
    void chooseBackup();
    void saveBackup(const QString& path);
    void poll();
    void updateControls();
    void clearVisiblePasswords();
    void clearSecrets();
    void requestClose(int code);
    void finishClose(int code);
    Services m_services;
    Stage m_stage = Stage::Input;
    QLineEdit *m_password, *m_confirmation, *m_hint;
    QCheckBox* m_ack;
    QLabel *m_status, *m_fingerprint;
    QPushButton *m_generate, *m_save, *m_close;
    QTimer* m_timer;
    QPointer<QFileDialog> m_picker;
    bool m_selecting = false;
    quint64 m_selection = 0;
    bool m_closePending = false;
    int m_closeCode = QDialog::Rejected;
    QString m_secret, m_backupPath;
    WizCertificateCreation::Certificate m_certificate;
    std::shared_ptr<Task> m_task;
};
#endif
