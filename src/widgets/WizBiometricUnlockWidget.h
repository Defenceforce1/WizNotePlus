#ifndef WIZ_BIOMETRIC_UNLOCK_WIDGET_H
#define WIZ_BIOMETRIC_UNLOCK_WIDGET_H
#include "mac/WizBiometricKeychain.h"
#include "share/WizDocumentAsync.h"
#include <QPointer>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QThreadPool;
class QTimer;

// Default-off GUI building block; the host owns note/cache authorization.
// This widget performs no enrollment, database writes or saves. The host may
// opt in to one automatic verification per visible locked context.
class WizBiometricUnlockWidget final : public QWidget {
    Q_OBJECT
public:
    struct Context {
        WizBiometricKeychain::Scope scope;
        QString documentGuid;
        quint64 openSequence = 0; // New nonzero value for each actual open.
    };
    enum class Stage { Unavailable, Ready, Working, Verified, Consumed, Closed };
    class Operation {
    public:
        virtual ~Operation() = default;
        virtual WizBiometricKeychain::Result read(const WizBiometricKeychain::Scope&) = 0;
        virtual void cancel() noexcept = 0;
    };
    struct Services {
        // Isolated test seam. Ordinary construction always uses native reads.
        std::function<std::shared_ptr<Operation>()> makeOperation;
        QThreadPool* pool = nullptr;
        // Noninteractive capability query; synthetic providers default to true.
        std::function<bool()> biometricAvailable;
    };
    explicit WizBiometricUnlockWidget(QWidget* parent = nullptr);
    explicit WizBiometricUnlockWidget(Services services, QWidget* parent = nullptr);
    ~WizBiometricUnlockWidget() override;
    void setContext(const Context& context, bool rememberedAccessEnabled);
    void setAutomaticStart(bool enabled);
    // Host-only presentation change: hiding an unavailable choice must not
    // consume a verification which has not started. Actual context retirement
    // still uses cancel()/setContext(), including hidden parent windows.
    void setPresentationVisible(bool visible);
    // An embedded password editor already provides the alternative action.
    void setInlinePasswordAvailable(bool available);
    void cancelAutomaticStart() { m_automaticPending=false; }
    Stage stage() const { return m_stage; }
    // Only an already-locked host may preserve this request across temporary
    // application inactivity. Hiding/minimizing/hard session boundaries cancel.
    bool preserveNativeAuthenticationOnInactive() const;
    // One delivery only. Receiver must still match its live note/account/cert.
    WizBiometricKeychain::Result takeCredential(const Context& context);
    bool authorizedFor(const Context& context) const;
public Q_SLOTS:
    void cancel();
    void systemLocked(); // Host must wire the actual lock notification later.
    void beginShutdown();
Q_SIGNALS:
    void credentialReady(); // No password in queued signals or UI properties.
    void manualPasswordRequested();
    void stageChanged();
protected:
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    struct Task;
    class Job;
    void start();
    void poll();
    void cancelWork();
    void updateControls();
    void scheduleAutomaticStart();
    bool currentAvailability() const;
    void usePasswordFallback();
    bool matches(const Context& context) const;
    static bool valid(const Context& context);
    Services m_services;
    QPointer<QThreadPool> m_pool;
    Context m_context;
    bool m_enabled = false, m_closed = false, m_nativeProvider = false;
    bool m_eligible=false,m_capabilityHidden=false,m_presentationChange=false;
    bool m_inlinePasswordAvailable=false;
    bool m_automaticStart=false,m_automaticPending=false;
    Stage m_stage = Stage::Unavailable;
    WizBiometricKeychain::Error m_error = WizBiometricKeychain::Error::None;
    WizDocumentCallbackEpoch m_epoch;
    QString m_secret;
    std::shared_ptr<Task> m_task;
    QLabel* m_status;
    QPushButton *m_unlock, *m_manual, *m_cancel;
    QTimer* m_timer;
};
#endif
