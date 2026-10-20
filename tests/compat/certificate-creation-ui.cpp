#include "widgets/WizCertificateCreationDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRunnable>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {
using Dialog = WizCertificateCreationDialog;
using Stage = Dialog::Stage;
using namespace WizCertificateCreation;
const QString password = QStringLiteral("Synthetic-ui-only-密码-2026");
bool until(const std::function<bool()>& predicate, int timeout = 5000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}
template <typename T> T* child(Dialog& dialog, const char* name) { return dialog.findChild<T*>(name); }
void click(Dialog& dialog, const char* name) { child<QPushButton>(dialog, name)->click(); }
void fill(Dialog& dialog, const QString& confirmation = password)
{
    child<QLineEdit>(dialog, "certificatePassword")->setText(password);
    child<QLineEdit>(dialog, "certificateConfirmation")->setText(confirmation);
    child<QLineEdit>(dialog, "certificateHint")->setText(QStringLiteral("仅合成测试"));
    child<QCheckBox>(dialog, "certificateAcknowledgement")->setChecked(true);
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool put(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
struct Gate {
    std::atomic<bool> entered{false};
    std::mutex mutex;
    std::condition_variable condition;
    bool released = false;
    void wait() {
        entered.store(true);
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(10), [this] { return released; });
    }
    void release() {
        { std::lock_guard<std::mutex> lock(mutex); released = true; }
        condition.notify_all();
    }
    ~Gate() { release(); }
};
void snapshot(Dialog& dialog, const char* name)
{
    const QString directory = QString::fromUtf8(qgetenv("WIZNOTE_CERT_UI_EVIDENCE_DIR"));
    if (directory.isEmpty()) return;
    // Only synthetic, password-cleared stages are captured; never input fields.
    dialog.adjustSize();
    dialog.grab().save(QDir(directory).filePath(QString::fromLatin1(name) + ".png"));
}
}

int wizCertificateCreationUiSelfTest(int argc, char** argv)
{
    // Dispatched before normal startup. No account, database, WebEngine or
    // service clients. Only fresh synthetic keys and disposable local paths.
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    const auto generated = WizCertificateCreation::generate(password, password, QStringLiteral("仅合成测试"));
    if (!check(bool(generated), "synthetic legacy certificate fixture generates locally")) return 1;
    const auto count = std::make_shared<std::atomic<int>>(0);
    Dialog::Services base;
    base.initialDirectory = root.path();
    base.generate = [generated, count](const QString&, const QString&, const QString&) { ++*count; return generated; };
    base.choosePath = [](QWidget*, const Dialog::Selection& selected) { selected({}); };
    auto ready = [&](Dialog& dialog) {
        fill(dialog);
        click(dialog, "certificateGenerate");
        return until([&] { return dialog.stage() == Stage::Ready; });
    };

    {
        Dialog dialog(base); dialog.show();
        check(!child<QPushButton>(dialog, "certificateGenerate")->isEnabled()
              && !child<QPushButton>(dialog, "certificateSave")->isEnabled(), "no generation or save without explicit acknowledgement");
        check(!child<QPushButton>(dialog, "certificateGenerate")->isDefault()
              && !child<QPushButton>(dialog, "certificateGenerate")->autoDefault()
              && child<QPushButton>(dialog, "certificateClose")->isDefault(), "neutral close is default, generation is never an Enter default");
        check(child<QLineEdit>(dialog, "certificatePassword")->echoMode() == QLineEdit::Password
              && child<QLineEdit>(dialog, "certificateConfirmation")->echoMode() == QLineEdit::Password,
              "both secret fields use masked input");
        check(child<QLabel>(dialog, "certificateWarning")->text().contains(QStringLiteral("不会登记"))
              && child<QLabel>(dialog, "certificateWarning")->text().contains(QStringLiteral("iPhone"))
              && child<QLabel>(dialog, "certificateWarning")->textFormat() == Qt::PlainText,
              "visible plain-text warning distinguishes backup from registration and mobile import");
        fill(dialog, "not-the-confirmation"); click(dialog, "certificateGenerate");
        check(dialog.stage() == Stage::Input && count->load() == 0, "mismatched confirmation never queues generation");
        fill(dialog); child<QLineEdit>(dialog, "certificateHint")->setText(password);
        click(dialog, "certificateGenerate");
        check(dialog.stage() == Stage::Input && count->load() == 0, "public hint containing password is refused before generation");
        child<QLineEdit>(dialog, "certificatePassword")->setText("short");
        child<QLineEdit>(dialog, "certificateConfirmation")->setText("short");
        click(dialog, "certificateGenerate");
        check(dialog.stage() == Stage::Input && count->load() == 0, "short password never queues generation");
        bool emittedOnClear = false;
        fill(dialog);
        QObject::connect(child<QLineEdit>(dialog, "certificatePassword"), &QLineEdit::textChanged,
                         &dialog, [&](const QString&) { emittedOnClear = true; });
        click(dialog, "certificateGenerate");
        check(until([&] { return dialog.stage() == Stage::Ready; }), "asynchronous generated result reaches ready stage");
        check(!emittedOnClear && child<QLineEdit>(dialog, "certificatePassword")->text().isEmpty()
              && child<QLineEdit>(dialog, "certificateConfirmation")->text().isEmpty(), "visible passwords clear without outward textChanged callbacks");
        check(!child<QLineEdit>(dialog, "certificateHint")->isEnabled()
              && !child<QPushButton>(dialog, "certificateGenerate")->isEnabled(), "ready state freezes input and prevents a second key");
        const auto fingerprint = child<QLabel>(dialog, "certificateFingerprint")->text();
        check(fingerprint.contains(publicFingerprint(generated.certificate)) && !fingerprint.contains(password), "ready UI displays only public fingerprint, no password");
        snapshot(dialog, "ready");
        const int before = count->load(); click(dialog, "certificateSave");
        check(dialog.stage() == Stage::Ready && count->load() == before
              && child<QLabel>(dialog, "certificateFingerprint")->text() == fingerprint,
              "canceling path selection retains the same certificate with no regeneration");
        dialog.reject();
        check(dialog.stage() == Stage::Closed && !dialog.isVisible(), "cancel closes ready UI without any account action");
    }
    {
        auto services = base;
        services.generate = {}; // Actual default core, not the reusable fixture.
        const QString path = root.filePath("actual-default.wizcert.json");
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        Dialog dialog(services);
        check(ready(dialog), "default production generator completes through the actual UI worker");
        const auto fingerprint = child<QLabel>(dialog, "certificateFingerprint")->text();
        click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Saved; }), "default production saver completes through the actual UI worker");
        const auto restored = restoreBackup(path, password);
        check(bool(restored) && restored.certificate.n != generated.certificate.n
              && fingerprint.contains(publicFingerprint(restored.certificate)),
              "fresh default UI key is independently restored with the displayed public identity");
    }
    {
        Dialog::Selection selected;
        auto services = base;
        services.choosePath = [&](QWidget*, const Dialog::Selection& completion) { selected = completion; };
        Dialog dialog(services); dialog.show();
        check(ready(dialog), "success fixture reaches ready");
        click(dialog, "certificateSave");
        check(!child<QPushButton>(dialog, "certificateSave")->isEnabled(), "only one path selector can be outstanding");
        const auto stale = selected;
        selected({}); click(dialog, "certificateSave");
        stale(root.filePath("stale.wizcert.json"));
        check(dialog.stage() == Stage::Ready && !QFileInfo::exists(root.filePath("stale.wizcert.json")), "stale canceled selector cannot publish a backup");
        const QString path = root.filePath(QStringLiteral("合成 备份 #%.wizcert.json"));
        selected(path); selected(root.filePath("duplicate.wizcert.json"));
        check(until([&] { return dialog.stage() == Stage::Saved; }), "new private backup is saved and read back off the GUI thread");
        const auto restored = restoreBackup(path, password);
        check(bool(restored) && restored.certificate.encryptedD == generated.certificate.encryptedD
              && dialog.backupPath() == path, "saved file restores the exact original encrypted certificate");
        check(!QFileInfo::exists(root.filePath("duplicate.wizcert.json")), "duplicate selector completion has no second write");
        check(!read(path).contains(password.toUtf8()) && QFileInfo(path).size() > 0,
              "saved backup contains no plaintext password");
        check(!child<QPushButton>(dialog, "certificateSave")->isEnabled()
              && child<QLabel>(dialog, "certificateStatus")->text().contains(QStringLiteral("仍未登记")),
              "saved stage has no activation or repeated-write action");
        snapshot(dialog, "saved");
        dialog.accept();
        check(dialog.stage() == Stage::Closed && dialog.result() == QDialog::Accepted, "accepted close terminates the local dialog only");
    }
    {
        const QString existing = root.filePath("existing.wizcert.json"), next = root.filePath("retry.wizcert.json");
        check(put(existing, "existing-user-file"), "existing-file fixture created without overwriting anything");
        auto selectedPath = std::make_shared<QString>(existing);
        auto services = base;
        services.choosePath = [selectedPath](QWidget*, const Dialog::Selection& selected) { selected(*selectedPath); };
        Dialog dialog(services); check(ready(dialog), "retry fixture reaches ready");
        const int before = count->load(); click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Ready; }) && read(existing) == "existing-user-file",
              "existing path refuses publication and preserves exact old bytes");
        check(child<QLabel>(dialog, "certificateStatus")->text().contains(QStringLiteral("未覆盖")), "existing-path refusal is visible");
        *selectedPath = next; click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Saved; }) && count->load() == before
              && restoreBackup(next, password).certificate.n == generated.certificate.n, "retry chooses a new path for the same key, never regenerates");
    }
    {
        auto services = base;
        services.generate = [](const QString&, const QString&, const QString&) { return Result{Error::GenerationFailed, {}}; };
        Dialog dialog(services); fill(dialog); click(dialog, "certificateGenerate");
        check(until([&] { return dialog.stage() == Stage::Input; }), "generation failure returns to editable input without partial certificate");
        check(child<QLineEdit>(dialog, "certificatePassword")->text().isEmpty()
              && !child<QPushButton>(dialog, "certificateSave")->isEnabled(), "generation failure retains no visible password or save action");
        services.generate = [](const QString&, const QString&, const QString&) { throw 1; return Result{}; };
        Dialog throwing(services); fill(throwing); click(throwing, "certificateGenerate");
        check(until([&] { return throwing.stage() == Stage::Input; }), "provider exception is contained without a result or account action");
        services.generate = [](const QString&, const QString&, const QString&) { return Result{}; };
        Dialog malformed(services); fill(malformed); click(malformed, "certificateGenerate");
        check(until([&] { return malformed.stage() == Stage::Input; })
              && child<QLabel>(malformed, "certificateFingerprint")->text().isEmpty(),
              "provider success with invalid key fields never reaches ready");
    }
    {
        auto services = base;
        const QString path = root.filePath("failed.wizcert.json");
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [](const QString&, const Certificate&, const QString&) { return BackupResult{Error::BackupWriteFailed, false}; };
        Dialog dialog(services); check(ready(dialog), "write-failure fixture reaches ready");
        const int before = count->load(); click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Ready; }) && !QFileInfo::exists(path)
              && count->load() == before, "prepublication failure offers retry of same certificate without a new file");
    }
    {
        const QString path = root.filePath("sync-warning.wizcert.json");
        auto services = base;
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [](const QString& path, const Certificate& certificate, const QString& secret) {
            auto result = saveNewBackup(path, certificate, secret);
            if (result) result.error = Error::BackupSyncUnconfirmed;
            return result;
        };
        Dialog dialog(services); dialog.show(); check(ready(dialog), "sync-warning fixture reaches ready");
        click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Unconfirmed; }) && bool(restoreBackup(path, password)),
              "postpublication sync warning retains the valid new file as unconfirmed");
        check(!child<QPushButton>(dialog, "certificateGenerate")->isEnabled()
              && !child<QPushButton>(dialog, "certificateSave")->isEnabled(), "uncertain publication never offers regeneration or blind retry");
        snapshot(dialog, "unconfirmed");
        dialog.reject();
        check(QFileInfo::exists(path), "closing unconfirmed result never removes a published backup");
    }
    {
        const QString path = root.filePath("readback-failure.wizcert.json");
        auto services = base;
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [](const QString& path, const Certificate&, const QString&) {
            return BackupResult{Error::None, put(path, "incomplete-provider-output")};
        };
        Dialog dialog(services); check(ready(dialog), "readback-failure fixture reaches ready");
        click(dialog, "certificateSave");
        check(until([&] { return dialog.stage() == Stage::Unconfirmed; }) && read(path) == "incomplete-provider-output",
              "failed independent readback cannot claim success or delete the uncertain file");
    }
    {
        auto gate = std::make_shared<Gate>(); auto services = base;
        services.generate = [gate, generated](const QString&, const QString&, const QString&) { gate->wait(); return generated; };
        auto* parent = new QWidget;
        QPointer<Dialog> dialog = new Dialog(services, parent);
        fill(*dialog); click(*dialog, "certificateGenerate");
        check(until([&] { return gate->entered.load(); }), "delayed generation runs without blocking the GUI loop");
        delete parent;
        check(!dialog, "parent destruction cancels all GUI ownership during generation");
        gate->release();
        check(QThreadPool::globalInstance()->waitForDone(5000), "orphaned pure generation finishes without accessing deleted widgets");
    }
    {
        auto gate = std::make_shared<Gate>(); auto services = base;
        services.generate = [gate, generated](const QString&, const QString&, const QString&) { gate->wait(); return generated; };
        Dialog dialog(services); dialog.show(); fill(dialog); click(dialog, "certificateGenerate");
        check(until([&] { return gate->entered.load(); }), "cancel-generation fixture starts");
        dialog.reject();
        check(dialog.stage() == Stage::Closed && child<QLineEdit>(dialog, "certificatePassword")->text().isEmpty(),
              "Escape-style cancellation discards running generation immediately and clears input");
        gate->release();
        check(QThreadPool::globalInstance()->waitForDone(5000), "canceled pure generation completes without publication");
    }
    {
        Dialog::Selection selected; auto services = base;
        services.choosePath = [&](QWidget*, const Dialog::Selection& completion) { selected = completion; };
        auto* dialog = new Dialog(services); check(ready(*dialog), "destroyed-selector fixture reaches ready");
        click(*dialog, "certificateSave"); delete dialog;
        selected(root.filePath("destroyed-selector.wizcert.json"));
        check(!QFileInfo::exists(root.filePath("destroyed-selector.wizcert.json")), "late selection after owner destruction cannot write anything");
    }
    {
        auto gate = std::make_shared<Gate>(); auto services = base;
        const QString path = root.filePath("deferred-close.wizcert.json");
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [gate](const QString& path, const Certificate& certificate, const QString& secret) {
            gate->wait(); return saveNewBackup(path, certificate, secret);
        };
        Dialog dialog(services); dialog.show(); check(ready(dialog), "deferred-close fixture reaches ready");
        click(dialog, "certificateSave");
        check(until([&] { return gate->entered.load(); }), "backup worker starts while GUI remains responsive");
        dialog.close();
        check(dialog.stage() == Stage::Saving && dialog.isVisible(), "ordinary window close defers once an authorized write has begun");
        gate->release();
        check(until([&] { return dialog.stage() == Stage::Closed; }) && bool(restoreBackup(path, password)),
              "deferred close completes after verified save and retains backup");
    }
    {
        auto gate = std::make_shared<Gate>(); auto services = base;
        const QString path = root.filePath("destroyed-saving.wizcert.json");
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [gate](const QString& path, const Certificate& certificate, const QString& secret) {
            gate->wait(); return saveNewBackup(path, certificate, secret);
        };
        auto* parent = new QWidget; QPointer<Dialog> dialog = new Dialog(services, parent);
        check(ready(*dialog), "destroyed-saving fixture reaches ready"); click(*dialog, "certificateSave");
        check(until([&] { return gate->entered.load(); }), "destroyed-saving worker begins before owner destruction");
        delete parent; gate->release();
        check(!dialog && QThreadPool::globalInstance()->waitForDone(5000) && bool(restoreBackup(path, password)),
              "forced parent destruction permits selected new backup to finish without GUI access or deletion");
    }
    {
        auto services = base;
        const QString path = root.filePath("queued-cancel.wizcert.json");
        auto calls = std::make_shared<std::atomic<int>>(0);
        services.choosePath = [path](QWidget*, const Dialog::Selection& selected) { selected(path); };
        services.save = [calls](const QString& path, const Certificate& certificate, const QString& secret) {
            ++*calls; return saveNewBackup(path, certificate, secret);
        };
        Dialog dialog(services); check(ready(dialog), "queued-cancel fixture reaches ready");
        auto* pool = QThreadPool::globalInstance();
        check(pool->waitForDone(5000), "all previous own jobs finish before deterministic queue fixture");
        const int oldMax = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        auto gate = std::make_shared<Gate>();
        pool->start(QRunnable::create([gate] { gate->wait(); }));
        check(until([&] { return gate->entered.load(); }), "single-worker queue is held by an owned bounded fixture");
        click(dialog, "certificateSave"); dialog.done(QDialog::Rejected);
        check(dialog.stage() == Stage::Closed, "queued backup cancels immediately before any write starts");
        gate->release();
        check(pool->waitForDone(5000) && calls->load() == 0 && !QFileInfo::exists(path),
              "canceled queued job never invokes saver or creates a file");
        pool->setMaxThreadCount(oldMax);
    }
    {
        auto* parent = new QWidget; auto* dialog = new Dialog(base, parent);
        QPointer<QWidget> guard(parent);
        QObject::connect(dialog, &QDialog::finished, parent, [parent] { delete parent; });
        dialog->reject();
        check(!guard, "finished callback may delete the entire owner without post-close access");
    }
    check(QThreadPool::globalInstance()->waitForDone(5000), "all owned background jobs finish before isolated test exits");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    qInfo().noquote() << "certificate-creation-ui checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
