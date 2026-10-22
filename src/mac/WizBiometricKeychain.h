#ifndef WIZ_BIOMETRIC_KEYCHAIN_H
#define WIZ_BIOMETRIC_KEYCHAIN_H

#include "share/WizCertificateCreation.h"
#include <QString>
#include <memory>

// Opt-in local convenience only; no account registration or certificate change.
// This building block has NO normal-client caller until UI/context/quit review.
namespace WizBiometricKeychain {
struct Scope {
    QString serverIdentity, accountGuid, kbGuid;
    WizCertificateCreation::Certificate certificate;
};
enum class Error {
    None, InvalidScope, ConsentRequired, PasswordInvalid, SigningRequired,
    BiometryUnavailable, Canceled, Missing, AlreadyExists, AccessDenied,
    CredentialMismatch, Failed, UnsupportedPlatform
};
struct Result {
    Error error = Error::None;
    QString password;
    explicit operator bool() const { return error == Error::None; }
};
// Hashes framed server/account/kb and the entire encrypted-key revision, not
// just the public key. Never put usernames, note titles or tokens in the item ID.
QString itemIdentity(const Scope& scope);
Error validateEnrollment(const Scope& scope, const QString& password, bool explicitConsent);
// Reads this process's signing metadata only. Not a keychain probe or promise
// that an item access will succeed; the OS still authorizes each SecItem call.
bool hasSigningPrerequisites();
// Noninteractive current-system capability check: no evaluate/credential read.
bool isBiometryAvailable();
Error enroll(const Scope& scope, const QString& verifiedPassword, bool explicitConsent);
Error forget(const Scope& scope, bool explicitConsent);

// One-use read operation. Invoke read() on a worker after an explicit user
// gesture; cancel() may be called by the GUI when its owner/context changes.
// Caller must also reject late results for changed accounts/notes/certificates.
// Keep the operation alive (e.g. shared ownership) until its worker returns.
class ReadOperation final {
public:
    ReadOperation();
    ~ReadOperation();
    ReadOperation(const ReadOperation&) = delete;
    ReadOperation& operator=(const ReadOperation&) = delete;
    Result read(const Scope& scope);
    void cancel();
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
#endif
