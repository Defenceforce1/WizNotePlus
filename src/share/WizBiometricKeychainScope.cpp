#include "mac/WizBiometricKeychain.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <atomic>

namespace WizBiometricKeychain {
QString itemIdentity(const Scope& scope)
{
    const auto validText = [](const QString& text, int maximum) {
        return !text.trimmed().isEmpty() && text.size() <= maximum
            && !text.contains(QChar::Null) && QString::fromUtf8(text.toUtf8()) == text;
    };
    if (!validText(scope.serverIdentity, 2048) || !validText(scope.accountGuid, 256)
        || scope.kbGuid.size() > 256 || scope.kbGuid.contains(QChar::Null)
        || QString::fromUtf8(scope.kbGuid.toUtf8()) != scope.kbGuid) return {};
    const auto& cert = scope.certificate;
    // The existing 128-byte ZIWR reader is unchanged. Unsupported legacy shapes
    // keep manual password access; never reinterpret them to enable biometrics.
    if (WizCertificateCreation::publicFingerprint(cert).isEmpty()) return {};
    QJsonArray framed{QStringLiteral("WizNotePlus.biometric-certificate.v1"),
        scope.serverIdentity, scope.accountGuid, scope.kbGuid, cert.n, cert.e, cert.encryptedD};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(framed).toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256).toHex());
}
Error validateEnrollment(const Scope& scope, const QString& password, bool explicitConsent)
{
    if (!explicitConsent) return Error::ConsentRequired;
    if (itemIdentity(scope).isEmpty()) return Error::InvalidScope;
    return WizCertificateCreation::verify(scope.certificate, password) == WizCertificateCreation::Error::None
        ? Error::None : Error::PasswordInvalid;
}
#ifndef Q_OS_MAC
bool hasSigningPrerequisites() { return false; }
bool isBiometryAvailable() { return false; }
Error enroll(const Scope& scope, const QString& password, bool consent)
{
    const auto error = validateEnrollment(scope, password, consent);
    return error == Error::None ? Error::UnsupportedPlatform : error;
}
Error forget(const Scope& scope, bool consent)
{
    if (!consent) return Error::ConsentRequired;
    return itemIdentity(scope).isEmpty() ? Error::InvalidScope : Error::UnsupportedPlatform;
}
struct ReadOperation::Private { std::atomic<bool> canceled{false}, started{false}; };
ReadOperation::ReadOperation() : d(new Private) {}
ReadOperation::~ReadOperation() = default;
Result ReadOperation::read(const Scope& scope)
{
    if (d->canceled.load() || d->started.exchange(true)) return {Error::Canceled, {}};
    return {itemIdentity(scope).isEmpty() ? Error::InvalidScope : Error::UnsupportedPlatform, {}};
}
void ReadOperation::cancel() { d->canceled.store(true); }
#endif
}
