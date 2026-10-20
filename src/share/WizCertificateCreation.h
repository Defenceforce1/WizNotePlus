#ifndef WIZ_CERTIFICATE_CREATION_H
#define WIZ_CERTIFICATE_CREATION_H

#include <QByteArray>
#include <QString>

// Local first-creation building block, not account registration or rotation.
// Never loads an existing account, activates a certificate, or sends requests.
namespace WizCertificateCreation {
struct Certificate {
    QString n, e, encryptedD, hint;
};
enum class Error {
    None, PasswordRequired, PasswordTooShort, PasswordMismatch,
    UnsupportedPassword, InvalidHint, GenerationFailed, InvalidCertificate,
    PasswordInvalid, InvalidBackup, BackupReadFailed, BackupExists,
    BackupWriteFailed, BackupSyncUnconfirmed, UnsupportedPlatform
};
struct Result {
    Error error = Error::None;
    Certificate certificate;
    explicit operator bool() const { return error == Error::None; }
};
struct BackupResult {
    Error error = Error::None;
    // In particular, sync failure after publication retains the backup and
    // reports it separately. A caller must not silently generate another key.
    bool fileCreated = false;
    explicit operator bool() const { return error == Error::None; }
};

Error validateInput(const QString& password, const QString& confirmation, const QString& hint);
Result generate(const QString& password, const QString& confirmation, const QString& hint);
Error verify(const Certificate& certificate, const QString& password);
QString publicFingerprint(const Certificate& certificate);

// Application-owned, unregistered encrypted backup, NOT an official iPhone
// import format. Uses the unchanged n/e/encrypted-d representation internally.
QByteArray backupBytes(const Certificate& certificate);
Result restoreBytes(const QByteArray& bytes, const QString& password);
BackupResult saveNewBackup(const QString& path, const Certificate& certificate, const QString& password);
Result restoreBackup(const QString& path, const QString& password);
}

#endif
