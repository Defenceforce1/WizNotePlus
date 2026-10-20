#include "WizCertificateCreationDialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>

namespace {
using namespace WizCertificateCreation;
enum WorkState { Queued, Running, Canceled, Complete };
class Job final : public QRunnable {
public:
    explicit Job(std::function<void()> work) : work(std::move(work)) {}
    void run() override { work(); }
private:
    std::function<void()> work;
};
QString inputError(Error error)
{
    switch (error) {
    case Error::PasswordRequired: return QObject::tr("请输入加密密码，不能仅为空格。");
    case Error::PasswordTooShort: return QObject::tr("密码至少需要 8 个字符。");
    case Error::PasswordMismatch: return QObject::tr("两次输入的密码不一致。");
    case Error::UnsupportedPassword: return QObject::tr("此密码含有旧格式无法完整保存的字符，或长度过长。");
    case Error::InvalidHint: return QObject::tr("提示不能包含密码，且最多为 256 个字符。");
    default: return QObject::tr("本地生成失败，未登记或修改任何账号证书。请重试。");
    }
}
bool same(const Certificate& a, const Certificate& b)
{
    return a.n == b.n && a.e == b.e && a.encryptedD == b.encryptedD && a.hint == b.hint;
}
void clearEdit(QLineEdit* edit)
{
    // A textChanged listener must not see a password or destroy the owner while
    // its fields are cleared. Do not leave a blocker referring to a dead edit.
    QPointer<QLineEdit> guard(edit);
    const bool blocked = edit->blockSignals(true);
    edit->clear();
    if (guard) guard->blockSignals(blocked);
}
}

struct WizCertificateCreationDialog::Task {
    std::atomic<int> state{Queued};
    Result generated;
    BackupResult saved;
};

WizCertificateCreationDialog::WizCertificateCreationDialog(QWidget* parent)
    : WizCertificateCreationDialog(Services{}, parent) {}

WizCertificateCreationDialog::WizCertificateCreationDialog(Services services, QWidget* parent)
    : QDialog(parent), m_services(std::move(services))
{
    if (!m_services.generate) m_services.generate = WizCertificateCreation::generate;
    if (!m_services.save) m_services.save = WizCertificateCreation::saveNewBackup;
    setWindowTitle(tr("本地加密证书备份（未登记）"));
    setModal(true);
    resize(580, 500);
    auto* layout = new QVBoxLayout(this);
    auto* warning = new QLabel(tr("此步骤只创建本地加密备份，不会登记、启用或替换账号证书，也不是 iPhone 可直接导入的文件。"
        "为兼容既有笔记，沿用旧证书格式，并非现代加密升级。密码不会保存，丢失后无法找回；提示属于公开信息。"), this);
    warning->setObjectName("certificateWarning");
    warning->setTextFormat(Qt::PlainText);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    auto* form = new QFormLayout;
    m_password = new QLineEdit(this);
    m_confirmation = new QLineEdit(this);
    m_hint = new QLineEdit(this);
    m_password->setObjectName("certificatePassword");
    m_confirmation->setObjectName("certificateConfirmation");
    m_hint->setObjectName("certificateHint");
    m_password->setEchoMode(QLineEdit::Password);
    m_confirmation->setEchoMode(QLineEdit::Password);
    m_password->setMaxLength(1024);
    m_confirmation->setMaxLength(1024);
    m_hint->setMaxLength(256);
    form->addRow(tr("加密密码（至少 8 字符）"), m_password);
    form->addRow(tr("再次输入密码"), m_confirmation);
    form->addRow(tr("公开提示（可不填）"), m_hint);
    layout->addLayout(form);
    m_ack = new QCheckBox(tr("我已了解：密码无法找回，备份尚未登记或启用。"), this);
    m_ack->setObjectName("certificateAcknowledgement");
    layout->addWidget(m_ack);
    m_fingerprint = new QLabel(this);
    m_fingerprint->setObjectName("certificateFingerprint");
    m_fingerprint->setTextFormat(Qt::PlainText);
    m_fingerprint->setWordWrap(true);
    m_fingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(m_fingerprint);
    m_status = new QLabel(tr("确认后先生成一份证书，再选择备份位置；不会覆盖已有文件。"), this);
    m_status->setObjectName("certificateStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();
    auto* buttons = new QHBoxLayout;
    m_generate = new QPushButton(tr("生成本地证书"), this);
    m_save = new QPushButton(tr("保存加密备份…"), this);
    m_close = new QPushButton(tr("关闭"), this);
    m_generate->setObjectName("certificateGenerate");
    m_save->setObjectName("certificateSave");
    m_close->setObjectName("certificateClose");
    for (auto* button : {m_generate, m_save, m_close}) button->setAutoDefault(false);
    m_close->setDefault(true);
    buttons->addWidget(m_generate);
    buttons->addWidget(m_save);
    buttons->addStretch();
    buttons->addWidget(m_close);
    layout->addLayout(buttons);
    m_timer = new QTimer(this);
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout, this, &WizCertificateCreationDialog::poll);
    connect(m_ack, &QCheckBox::toggled, this, &WizCertificateCreationDialog::updateControls);
    connect(m_generate, &QPushButton::clicked, this, &WizCertificateCreationDialog::generate);
    connect(m_save, &QPushButton::clicked, this, &WizCertificateCreationDialog::chooseBackup);
    connect(m_close, &QPushButton::clicked, this, &WizCertificateCreationDialog::reject);
    updateControls();
}

WizCertificateCreationDialog::~WizCertificateCreationDialog()
{
    if (m_task) {
        int expected = Queued;
        m_task->state.compare_exchange_strong(expected, Canceled);
    }
    // Running pure work owns only value copies. It never touches a QObject.
    m_secret.clear();
    m_certificate = {};
}

void WizCertificateCreationDialog::updateControls()
{
    QPointer<WizCertificateCreationDialog> guard(this);
    const bool input = m_stage == Stage::Input;
    m_password->setEnabled(input); if (!guard) return;
    m_confirmation->setEnabled(input); if (!guard) return;
    m_hint->setEnabled(input); if (!guard) return;
    m_ack->setEnabled(input); if (!guard) return;
    m_generate->setEnabled(input && m_ack->isChecked()); if (!guard) return;
    m_save->setEnabled(m_stage == Stage::Ready && !m_selecting);
}

void WizCertificateCreationDialog::clearVisiblePasswords()
{
    QPointer<WizCertificateCreationDialog> guard(this);
    clearEdit(m_password); if (!guard) return;
    clearEdit(m_confirmation);
}

void WizCertificateCreationDialog::clearSecrets()
{
    m_secret.clear();
    m_certificate = {};
    clearVisiblePasswords();
    // QString clearing is not a secure-memory-erasure guarantee.
}

void WizCertificateCreationDialog::generate()
{
    if (m_stage != Stage::Input || !m_ack->isChecked()) return;
    const QString password = m_password->text(), confirmation = m_confirmation->text(), hint = m_hint->text();
    const auto error = validateInput(password, confirmation, hint);
    if (error != Error::None) { m_status->setText(inputError(error)); return; }
    auto task = std::make_shared<Task>();
    m_task = task;
    m_secret = password;
    m_stage = Stage::Generating;
    QPointer<WizCertificateCreationDialog> guard(this);
    updateControls(); if (!guard || m_stage != Stage::Generating) return;
    clearVisiblePasswords(); if (!guard || m_stage != Stage::Generating) return;
    m_status->setText(tr("正在本地生成证书；可以取消，不会登记账号。"));
    if (!guard || m_stage != Stage::Generating) return;
    const auto generator = m_services.generate;
    m_timer->start();
    QThreadPool::globalInstance()->start(new Job([task, generator, password, confirmation, hint] {
        int expected = Queued;
        if (!task->state.compare_exchange_strong(expected, Running)) return;
        try { task->generated = generator(password, confirmation, hint); }
        catch (...) { task->generated.error = Error::GenerationFailed; }
        // A custom provider cannot report a malformed or locked key as ready.
        if (task->generated && verify(task->generated.certificate, password) != Error::None)
            task->generated = {Error::InvalidCertificate, {}};
        task->state.store(Complete, std::memory_order_release);
    }));
}

void WizCertificateCreationDialog::chooseBackup()
{
    if (m_stage != Stage::Ready || m_selecting) return;
    m_selecting = true;
    const quint64 selection = ++m_selection;
    QPointer<WizCertificateCreationDialog> guard(this);
    updateControls(); if (!guard || m_stage != Stage::Ready || !m_selecting) return;
    const Selection selected = [guard, selection](const QString& path) {
        if (!guard || guard->m_stage != Stage::Ready || !guard->m_selecting || guard->m_selection != selection) return;
        guard->m_selecting = false;
        ++guard->m_selection;
        if (path.isEmpty()) { guard->updateControls(); return; }
        guard->saveBackup(path);
    };
    if (m_services.choosePath) {
        const auto chooser = m_services.choosePath;
        try { chooser(this, selected); }
        catch (...) { selected({}); }
        return;
    }
    auto* picker = new QFileDialog(this, tr("保存未登记的加密证书备份"));
    m_picker = picker;
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setAcceptMode(QFileDialog::AcceptSave);
    picker->setFileMode(QFileDialog::AnyFile);
    picker->setOption(QFileDialog::DontConfirmOverwrite);
    picker->setNameFilter(tr("加密证书备份 (*.wizcert.json)"));
    picker->setDefaultSuffix("wizcert.json");
    if (!m_services.initialDirectory.isEmpty()) picker->setDirectory(m_services.initialDirectory);
    picker->selectFile("WizNote-unregistered.wizcert.json");
    connect(picker, &QDialog::finished, this, [guard, selected](int result) {
        if (!guard) return;
        const auto picker = guard->m_picker;
        guard->m_picker.clear();
        selected(result == QDialog::Accepted && picker ? picker->selectedFiles().value(0) : QString());
    });
    picker->open();
}

void WizCertificateCreationDialog::saveBackup(const QString& path)
{
    if (m_stage != Stage::Ready) return;
    m_backupPath = path;
    m_stage = Stage::Saving;
    auto task = std::make_shared<Task>();
    m_task = task;
    QPointer<WizCertificateCreationDialog> guard(this);
    updateControls(); if (!guard || m_stage != Stage::Saving) return;
    m_status->setText(tr("正在写入并重新读取备份。若写入已经开始，关闭将等待本次保存结束。"));
    if (!guard || m_stage != Stage::Saving) return;
    const auto saver = m_services.save;
    const auto certificate = m_certificate;
    const auto password = m_secret;
    m_timer->start();
    QThreadPool::globalInstance()->start(new Job([task, saver, path, certificate, password] {
        int expected = Queued;
        if (!task->state.compare_exchange_strong(expected, Running)) return;
        try {
            task->saved = saver(path, certificate, password);
            if (task->saved) {
                const auto restored = restoreBackup(path, password);
                if (!task->saved.fileCreated || !restored || !same(restored.certificate, certificate))
                    task->saved.error = Error::BackupReadFailed;
            }
        } catch (...) {
            // Publication may have happened before a provider threw. Treat the
            // selected path as uncertain; never retry with another key/delete it.
            task->saved = {Error::BackupWriteFailed, true};
        }
        task->state.store(Complete, std::memory_order_release);
    }));
}

void WizCertificateCreationDialog::poll()
{
    const auto task = m_task;
    if (!task || task->state.load(std::memory_order_acquire) != Complete) return;
    m_task.reset();
    m_timer->stop();
    QPointer<WizCertificateCreationDialog> guard(this);
    if (m_stage == Stage::Generating) {
        if (!task->generated) {
            m_stage = Stage::Input;
            clearSecrets(); if (!guard) return;
            m_status->setText(inputError(task->generated.error));
        } else {
            m_certificate = task->generated.certificate;
            m_stage = Stage::Ready;
            m_fingerprint->setText(tr("公钥指纹（SHA-256）：\n%1").arg(publicFingerprint(m_certificate)));
            if (!guard || m_stage == Stage::Closed) return;
            m_status->setText(tr("已生成，尚未登记。请选择新的备份文件；取消选择不会重新生成证书。"));
        }
    } else if (m_stage == Stage::Saving) {
        if (task->saved && task->saved.fileCreated) {
            m_stage = Stage::Saved;
            clearSecrets(); if (!guard) return;
            m_status->setText(tr("备份已保存并重新读取核对：\n%1\n仍未登记或启用账号证书，请妥善保管备份与密码。").arg(m_backupPath));
        } else if (task->saved.fileCreated) {
            m_stage = Stage::Unconfirmed;
            clearSecrets(); if (!guard) return;
            m_status->setText(tr("保存结果尚未完全确认，请保留所选文件，不要重新生成或覆盖：\n%1\n账号证书未登记或启用。").arg(m_backupPath));
        } else {
            m_stage = Stage::Ready;
            m_status->setText(task->saved.error == Error::BackupExists
                ? tr("所选路径已存在，未覆盖。请为同一份证书另选新文件。")
                : tr("备份未保存成功。请为同一份证书另选位置；不会重新生成。"));
        }
    }
    if (!guard || m_stage == Stage::Closed) return;
    if (m_closePending) { finishClose(m_closeCode); return; }
    updateControls();
}

void WizCertificateCreationDialog::requestClose(int code)
{
    if (m_stage == Stage::Closed) return;
    if (m_task) {
        int expected = Queued;
        const bool canceled = m_task->state.compare_exchange_strong(expected, Canceled);
        if (m_stage == Stage::Saving && !canceled && expected != Canceled) {
            m_closePending = true;
            m_closeCode = code;
            m_status->setText(tr("正在完成已选择的备份保存，完成后关闭；不会撤销或删除已写入文件。"));
            return;
        }
    }
    finishClose(code);
}

void WizCertificateCreationDialog::finishClose(int code)
{
    m_stage = Stage::Closed;
    m_timer->stop();
    m_task.reset(); // A running worker retains only its own disposable copy.
    ++m_selection;
    m_selecting = false;
    QPointer<WizCertificateCreationDialog> guard(this);
    clearSecrets(); if (!guard) return;
    if (m_picker) m_picker->reject();
    if (!guard) return;
    QDialog::done(code); // finished() may delete the parent; no subsequent access.
}
void WizCertificateCreationDialog::accept() { requestClose(QDialog::Accepted); }
void WizCertificateCreationDialog::reject() { requestClose(QDialog::Rejected); }
void WizCertificateCreationDialog::done(int code) { requestClose(code); }
void WizCertificateCreationDialog::closeEvent(QCloseEvent* event)
{
    // Set the event before finished() can delete us. Deferred saves keep their
    // window alive; finishClose() hides it only once the worker has completed.
    event->ignore();
    requestClose(QDialog::Rejected);
}
