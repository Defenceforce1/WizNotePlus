#include "share/WizCertificateCreation.h"
#include "share/WizZiwReader.h"
#include "share/WizEnc.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>
#include <quazip.h>
#include <quazipfile.h>
#include <cstring>

namespace {
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
bool same(const WizCertificateCreation::Certificate& a, const WizCertificateCreation::Certificate& b)
{
    return a.n == b.n && a.e == b.e && a.encryptedD == b.encryptedD && a.hint == b.hint;
}
}

int wizCertificateCreationSelfTest(int argc, char** argv)
{
    // Early diagnostic dispatch, before mainCore/login/settings/network. All
    // generated keys, files and database writes belong to this disposable root.
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    using namespace WizCertificateCreation;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    const QString password = QStringLiteral("Synthetic-only-密码-2026");
    const QString hint = QStringLiteral("仅合成测试，不用于账号");
    check(validateInput({}, {}, {}) == Error::PasswordRequired, "empty password refuses local generation");
    check(validateInput("        ", "        ", {}) == Error::PasswordRequired, "blank password refuses local generation");
    check(validateInput("short", "short", {}) == Error::PasswordTooShort, "creation requires at least eight characters");
    check(validateInput(password, "different", hint) == Error::PasswordMismatch, "password confirmation must match exactly");
    const QString nullPassword = password + QChar::Null + "suffix";
    check(validateInput(nullPassword, nullPassword, hint) == Error::UnsupportedPassword, "legacy NUL truncation is not accepted as a usable password");
    check(validateInput(password, password, password) == Error::InvalidHint, "public hint cannot contain the entered password");
    check(validateInput(password, password, QString(257, 'h')) == Error::InvalidHint, "hint length is bounded before key generation");
    const auto rejected = generate(password, "different", hint);
    check(!rejected && rejected.certificate.n.isEmpty() && rejected.certificate.encryptedD.isEmpty(), "invalid input returns no partial certificate");

    WizDatabaseManager manager("synthetic-certificate-maintenance-only");
    if (!check(manager.open(), "fresh temporary database opens without a real account or login")) return 1;
    auto& database = manager.db();
    check(!database.hasCert(), "temporary account initially has no certificate");
    const auto generated = generate(password, password, hint);
    if (!check(bool(generated), "local OS-random generation produces the unchanged legacy certificate fields")) return 1;
    const auto certificate = generated.certificate;
    check(certificate.e == "65537" && certificate.hint == hint && publicFingerprint(certificate).size() == 64,
          "public fields and fingerprint are stable without exposing a private exponent");
    check(!database.hasCert(), "local generation never activates or writes an account certificate");
    check(verify(certificate, password) == Error::None, "existing AES and RSA primitives accept the generated certificate");
    check(verify(certificate, "wrong-synthetic-password") == Error::PasswordInvalid, "ordinary wrong password fails locally without server refresh");
    const auto another = generate("another-synthetic-password", "another-synthetic-password", {});
    check(bool(another) && another.certificate.n != certificate.n && publicFingerprint(another.certificate) != publicFingerprint(certificate),
          "a separate local generation uses a different public key");

    const auto bytes = backupBytes(certificate);
    const auto object = QJsonDocument::fromJson(bytes).object();
    check(!bytes.isEmpty() && object.value("state") == "unregistered" && object.value("format") == "ZIWR-v1-legacy",
          "application-owned backup explicitly says unregistered legacy format");
    check(object.value("certificate").toObject().value("d") == certificate.encryptedD
          && !object.contains("password") && !object.contains("privateExponent") && !bytes.contains(password.toUtf8()),
          "backup contains encrypted d and no password or plaintext-private-key field");
    const auto restored = restoreBytes(bytes, password);
    check(bool(restored) && same(restored.certificate, certificate), "in-memory backup restores exact encrypted certificate fields");
    const auto wrong = restoreBytes(bytes, "wrong-synthetic-password");
    check(!wrong && wrong.certificate.n.isEmpty(), "failed backup unlock returns no installable certificate");
    check(restoreBytes("incomplete", password).error == Error::InvalidBackup, "incomplete backup is not accepted as a certificate");
    auto otherVersion = object; otherVersion["version"] = 2;
    check(restoreBytes(QJsonDocument(otherVersion).toJson(), password).error == Error::InvalidBackup, "unknown backup version refuses before activation");
    auto otherFingerprint = object; otherFingerprint["publicFingerprint"] = QString(64, '0');
    check(restoreBytes(QJsonDocument(otherFingerprint).toJson(), password).error == Error::InvalidBackup, "public-key identity mismatch refuses restore");

    const QString backup = root.filePath(QStringLiteral("证书 备份 #%.wizcert.json"));
    const auto saved = saveNewBackup(backup, certificate, password);
    if (!check(bool(saved) && saved.fileCreated && read(backup) == bytes, "private encrypted backup publishes complete bytes at a new Unicode path")) return 1;
    const auto permissions = QFileInfo(backup).permissions();
    check(permissions.testFlag(QFileDevice::ReadOwner) && permissions.testFlag(QFileDevice::WriteOwner)
          && !(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                              | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)),
          "backup is owner-readable and owner-writable only");
    const auto duplicate = saveNewBackup(backup, another.certificate, "another-synthetic-password");
    check(duplicate.error == Error::BackupExists && !duplicate.fileCreated && read(backup) == bytes,
          "existing certificate backup is never replaced by a different key");
    check(QDir(root.path()).entryList({".wizcert-*"}, QDir::Files | QDir::Hidden).isEmpty(), "successful and refused backup staging names are released");
    const auto fileRestored = restoreBackup(backup, password);
    check(bool(fileRestored) && same(fileRestored.certificate, certificate), "saved backup reopens and verifies exact certificate fields");
    check(saveNewBackup(root.filePath("missing/backup.json"), certificate, password).error == Error::BackupWriteFailed
          && !QFileInfo::exists(root.filePath("missing")), "missing backup parent fails without creating unrelated directories");
    const QString blocker = root.filePath("directory"); QDir().mkdir(blocker);
    check(!saveNewBackup(blocker, certificate, password) && QFileInfo(blocker).isDir(), "backup cannot replace a directory");
    const QString sentinel = root.filePath("old.json");
    check(put(sentinel, "original unrelated output"), "ordinary old-output sentinel exists only in disposable test root");
    check(saveNewBackup(sentinel, certificate, password).error == Error::BackupExists && read(sentinel) == "original unrelated output",
          "unrelated existing file remains byte-identical");
    check(restoreBackup(root.filePath("absent.json"), password).error == Error::BackupReadFailed, "missing backup reports an ordinary read failure");
    check(!database.hasCert(), "backup generation and restore do not write the account database");

    // All following calls operate ONLY on the temporary synthetic database.
    // Correct password verification takes its local success branch, never the
    // old refresh-on-wrong-password branch. No network collaborator is started.
    if (!check(database.setUserCert(certificate.n, certificate.e, certificate.encryptedD, certificate.hint)
               && database.verifyCertPassword(password), "actual temporary database accepts generated legacy fields and password locally")) return 1;
    WizZiwReader reader; reader.setDatabase(&database);
    reader.setRSAKeys(certificate.n.toUtf8(), certificate.e.toUtf8(), certificate.encryptedD.toUtf8(), hint);
    const QByteArray payload = QStringLiteral("<html><body>合成数学笔记 $A+B=C$</body></html>").toUtf8();
    const QString plainZiw = root.filePath("plain.ziw");
    QuaZip zip(plainZiw);
    bool archive = zip.open(QuaZip::mdCreate);
    if (archive) {
        QuaZipFile entry(&zip);
        archive = entry.open(QIODevice::WriteOnly, QuaZipNewInfo("index.html"));
        if (archive) { archive = entry.write(payload) == payload.size(); entry.close(); archive = archive && entry.getZipError() == UNZ_OK; }
        zip.close(); archive = archive && zip.getZipError() == UNZ_OK;
    }
    if (!check(archive, "synthetic ZIP note is built without reading any user notes")) return 1;
    const QByteArray original = read(plainZiw);
    const QString encryptedPath = root.filePath("encrypted.ziw");
    if (!check(reader.encryptFileToFile(plainZiw, encryptedPath), "unchanged native ZIW reader encrypts the synthetic note with the generated key")) return 1;
    const auto encrypted = read(encryptedPath);
    WIZZIWHEADER header{};
    if (encrypted.size() >= int(sizeof(header))) std::memcpy(&header, encrypted.constData(), sizeof(header));
    check(std::memcmp(header.szSign, "ZIWR", 4) == 0 && header.nVersion == 1 && header.nKeyLength == 128,
          "encrypted note retains native ZIWR v1 and fixed 128-byte RSA header");
    QByteArray decrypted;
    check(reader.decryptFileToData(encryptedPath, decrypted) && decrypted == original, "actual native ZIW reader decrypts exact original ZIP bytes");
    WizZiwReader fromBackup; fromBackup.setDatabase(&database);
    fromBackup.setRSAKeys(fileRestored.certificate.n.toUtf8(), fileRestored.certificate.e.toUtf8(), fileRestored.certificate.encryptedD.toUtf8(), hint);
    decrypted.clear();
    check(fromBackup.decryptFileToData(encryptedPath, decrypted) && decrypted == original, "fresh native reader using restored backup decrypts the same synthetic note");
    check(read(plainZiw) == original && read(backup) == bytes, "original synthetic ZIP and encrypted certificate backup remain unchanged");
    database.clearCertPassword();
    const QPointer<WizDatabase> previous(&database);
    manager.closeAll();
    // closeAll schedules deletion. Reopening before delivery only observes a
    // non-null pointer to the already closed old object, not a database reopen.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(!previous, "old temporary database is actually destroyed before reopen");
    if (!check(manager.open(), "temporary database reopens after local certificate storage")) return 1;
    Certificate reopened;
    check(manager.db().getUserCert(reopened.n, reopened.e, reopened.encryptedD, reopened.hint) && same(reopened, certificate),
          "actual database reopen preserves exact legacy certificate fields");
    check(verify(reopened, password) == Error::None, "reopened certificate remains usable through existing local primitives");
    manager.closeAll();
    qInfo() << "Local certificate checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
