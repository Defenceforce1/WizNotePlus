#include "mac/WizBiometricKeychain.h"
#include "share/WizEnc.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QTemporaryDir>

int wizBiometricKeychainSelfTest(int argc, char** argv)
{
    // Never perform authentication or SecItem calls, even if someone later
    // runs this route inside a properly signed client. Signed builds refuse.
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QCoreApplication app(argc, argv);
    using namespace WizBiometricKeychain;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    if (!check(!hasSigningPrerequisites(), "ad-hoc self-test refuses before any native credential or authentication call")) return 2;
    const QString password = QStringLiteral("Synthetic-biometric-only-2026");
#ifdef Q_OS_MAC
    const auto unavailable = Error::SigningRequired;
#else
    const auto unavailable = Error::UnsupportedPlatform;
#endif
    const auto generated = WizCertificateCreation::generate(password, password, QStringLiteral("仅合成"));
    if (!check(bool(generated), "synthetic certificate generates without an account")) return 1;
    Scope scope{QStringLiteral("https://synthetic.invalid/wiz/"), QStringLiteral("synthetic-account"), {}, generated.certificate};
    const auto identity = itemIdentity(scope);
    check(identity.size() == 64 && identity == itemIdentity(scope), "exact scope has a stable bounded opaque item identity");
    check(!identity.contains("synthetic") && !identity.contains(password), "item identity contains no account label or password");
    auto changed = scope; changed.accountGuid += "-other";
    check(itemIdentity(changed) != identity, "different personal account never shares an item");
    changed = scope; changed.serverIdentity += "other/";
    check(itemIdentity(changed) != identity, "different server identity never shares an item");
    changed = scope; changed.kbGuid = "synthetic-kb";
    check(itemIdentity(changed) != identity, "different knowledge base never shares an item");
    changed = scope; changed.certificate.hint = QStringLiteral("另一个公开提示");
    check(itemIdentity(changed) == identity, "public hint alone does not change credential revision");
    changed = scope;
    QString exponent;
    // Actual same-key password change, local synthetic fields only. No
    // plaintext private exponent enters disk or a log.
    const QString secondPassword = "second-synthetic-biometric-password";
    const bool reencrypted = WizAESDecryptBase64StringToString(password, scope.certificate.encryptedD, exponent)
        && WizAESEncryptStringToBase64String(secondPassword, exponent, changed.certificate.encryptedD);
    exponent.clear();
    check(reencrypted && changed.certificate.n == scope.certificate.n && itemIdentity(changed) != identity,
          "same public key with a changed encryption password addresses a different item");
    check(validateEnrollment(changed, secondPassword, true) == Error::None
          && validateEnrollment(changed, password, true) == Error::PasswordInvalid,
          "re-encrypted revision accepts only its actual current certificate password locally");
    const auto replacement = WizCertificateCreation::generate(secondPassword, secondPassword, {});
    if (!check(bool(replacement), "independent replacement certificate fixture generates locally")) return 1;
    changed.certificate = replacement.certificate;
    check(itemIdentity(changed) != identity, "replacement certificate cannot retrieve the old password item");
    changed = scope; changed.certificate.encryptedD.replace(0, 1, changed.certificate.encryptedD.at(0) == 'A' ? "B" : "A");
    check(!itemIdentity(changed).isEmpty() && itemIdentity(changed) != identity,
          "encrypted private-key revision participates even when public n and e stay unchanged");
    const QString null(1, QChar::Null);
    for (int field = 0; field < 3; ++field) {
        changed = scope;
        QString* value = field == 0 ? &changed.serverIdentity : field == 1 ? &changed.accountGuid : &changed.kbGuid;
        *value += null;
        check(itemIdentity(changed).isEmpty(), "NUL-containing identity field refuses before native access");
    }
    changed = scope; changed.accountGuid.clear();
    check(itemIdentity(changed).isEmpty(), "missing account identity cannot use an anonymous shared item");
    changed = scope; changed.serverIdentity = "   ";
    check(itemIdentity(changed).isEmpty(), "blank server identity refuses");
    changed = scope; changed.serverIdentity = QString(2049, 's');
    check(itemIdentity(changed).isEmpty(), "oversized server identity refuses");
    changed = scope; changed.accountGuid = QString(257, 'u');
    check(itemIdentity(changed).isEmpty(), "oversized account identity refuses");
    changed = scope; changed.kbGuid = QString(257, 'k');
    check(itemIdentity(changed).isEmpty(), "oversized knowledge-base identity refuses");
    changed = scope; changed.accountGuid += QChar(0xd800);
    check(itemIdentity(changed).isEmpty(), "lossy UTF-8 identity refuses instead of aliasing another scope");
    changed = scope; changed.certificate = {};
    check(itemIdentity(changed).isEmpty(), "missing certificate cannot address any saved password");
    changed = scope; changed.accountGuid = "synthetic-account/a"; changed.kbGuid = "b";
    auto other = scope; other.accountGuid = "synthetic-account"; other.kbGuid = "a/b";
    check(itemIdentity(changed) != itemIdentity(other), "structured framing prevents separator collisions");
    check(validateEnrollment(scope, password, false) == Error::ConsentRequired, "correct password alone is not enrollment consent");
    check(validateEnrollment(scope, "wrong-synthetic", true) == Error::PasswordInvalid, "wrong certificate password refuses locally without server refresh");
    check(validateEnrollment(scope, password, true) == Error::None, "explicit consent and actual local certificate verification accept enrollment input");
    check(enroll(scope, password, false) == Error::ConsentRequired, "native enrollment never starts without opt-in");
    check(enroll(scope, password, true) == unavailable, "ad-hoc enrollment refuses before biometric probe or keychain write");
    check(forget(scope, false) == Error::ConsentRequired, "removing remembered access requires a separate explicit decision");
    check(forget(scope, true) == unavailable, "ad-hoc removal refuses before touching a keychain");
    {
        ReadOperation request;
        const auto result = request.read(scope);
        check(result.error == unavailable && result.password.isEmpty(), "ad-hoc read returns no credential and never prompts");
        check(request.read(scope).error == Error::Canceled, "one-shot read cannot be replayed");
    }
    {
        ReadOperation request; request.cancel(); request.cancel();
        const auto result = request.read(scope);
        check(result.error == Error::Canceled && result.password.isEmpty(), "canceled operation stays canceled and contains no credential");
    }
    {
        ReadOperation request;
        check(request.read(Scope{}).error == Error::InvalidScope, "invalid-scope read refuses before signing or keychain lookup");
    }
    check(verify(generated.certificate, password) == WizCertificateCreation::Error::None,
          "diagnostic operations leave the original encrypted certificate usable and unchanged");
    qInfo().noquote() << "biometric-keychain checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
