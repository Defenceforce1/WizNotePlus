#include "WizCertificateCreation.h"
#include "WizEnc.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <cryptopp/misc.h>
#include <cerrno>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace WizCertificateCreation {
namespace {
constexpr int maximumBackupBytes = 16 * 1024;
bool passwordSupported(const QString& password)
{
    // Legacy password conversion is UTF-8 followed by strlen. Do not accept a
    // password whose suffix would silently disappear in that existing format.
    const auto utf8 = password.toUtf8();
    return !password.contains(QChar::Null) && utf8.size() <= 1024 && QString::fromUtf8(utf8) == password;
}
bool shape(const Certificate& certificate)
{
    static const QRegularExpression decimal(QStringLiteral("^[1-9][0-9]{307,308}$"));
    static const QRegularExpression base64(QStringLiteral("^[A-Za-z0-9+/]+={0,2}$"));
    if (!decimal.match(certificate.n).hasMatch() || certificate.e != QStringLiteral("65537")
        || certificate.encryptedD.size() > 1024 || !base64.match(certificate.encryptedD).hasMatch()
        || certificate.hint.size() > 256 || certificate.hint.contains(QChar::Null)) return false;
    const auto raw = QByteArray::fromBase64(certificate.encryptedD.toLatin1());
    if (raw.isEmpty() || raw.size() % 16 || raw.toBase64() != certificate.encryptedD.toLatin1()) return false;
    try {
        return CryptoPP::Integer(certificate.n.toLatin1().constData()).BitCount() == 1024;
    } catch (...) { return false; }
}
Result failed(Error error) { return {error, {}}; }
}

Error validateInput(const QString& password, const QString& confirmation, const QString& hint)
{
    if (password.isEmpty() || password.trimmed().isEmpty()) return Error::PasswordRequired;
    if (password.size() < 8) return Error::PasswordTooShort;
    if (!passwordSupported(password)) return Error::UnsupportedPassword;
    if (password != confirmation) return Error::PasswordMismatch;
    // Hint is stored alongside the encrypted key, not a private password field.
    if (hint.size() > 256 || hint.contains(QChar::Null) || hint.contains(password, Qt::CaseInsensitive)) return Error::InvalidHint;
    return Error::None;
}

Result generate(const QString& password, const QString& confirmation, const QString& hint)
{
    const auto input = validateInput(password, confirmation, hint);
    if (input != Error::None) return failed(input);
    try {
        CryptoPP::AutoSeededRandomPool random;
        CryptoPP::InvertibleRSAFunction key;
        // The existing ZIWR v1 header has a fixed 128-byte RSA field. This is
        // explicit legacy interoperability, NOT a modern-encryption upgrade.
        key.Initialize(random, 1024, CryptoPP::Integer(65537));
        if (!key.Validate(random, 3)) return failed(Error::GenerationFailed);
        Certificate certificate;
        certificate.n = QString::fromStdString(CryptoPP::IntToString(key.GetModulus(), 10));
        certificate.e = QStringLiteral("65537");
        certificate.hint = hint;
        QString privateExponent = QString::fromStdString(CryptoPP::IntToString(key.GetPrivateExponent(), 10));
        const bool encrypted = WizAESEncryptStringToBase64String(password, privateExponent, certificate.encryptedD);
        privateExponent.clear(); // No disk/log output; not a secure-erasure claim for Qt copies.
        if (!encrypted || verify(certificate, password) != Error::None) return failed(Error::GenerationFailed);
        return {Error::None, certificate};
    } catch (...) { return failed(Error::GenerationFailed); }
}

Error verify(const Certificate& certificate, const QString& password)
{
    if (!shape(certificate)) return Error::InvalidCertificate;
    if (password.isEmpty() || !passwordSupported(password)) return Error::PasswordInvalid;
    try {
        QString exponent;
        if (!WizAESDecryptBase64StringToString(password, certificate.encryptedD, exponent)) return Error::PasswordInvalid;
        static const QRegularExpression decimal(QStringLiteral("^[1-9][0-9]{0,308}$"));
        if (!decimal.match(exponent).hasMatch()) return Error::PasswordInvalid;
        const auto n = certificate.n.toLatin1(), e = certificate.e.toLatin1(), d = exponent.toLatin1();
        // Exercise the unchanged reader's primitives rather than accepting
        // nonempty decrypted text as proof that a certificate is usable.
        const QByteArray original("WizNotePlus local certificate round trip");
        QByteArray encrypted, plain;
        const bool ok = WizRSAEncryptToString(n.constData(), e.constData(), original, encrypted)
            && encrypted.size() == 128
            && WizRSADecryptToString(n.constData(), e.constData(), d.constData(), encrypted, plain)
            && plain == original;
        exponent.clear();
        return ok ? Error::None : Error::PasswordInvalid;
    } catch (...) { return Error::PasswordInvalid; }
}

QString publicFingerprint(const Certificate& certificate)
{
    if (!shape(certificate)) return {};
    return QString::fromLatin1(QCryptographicHash::hash((certificate.n + '\n' + certificate.e).toLatin1(),
                                                       QCryptographicHash::Sha256).toHex());
}

QByteArray backupBytes(const Certificate& certificate)
{
    if (!shape(certificate)) return {};
    const QJsonObject fields{{"n", certificate.n}, {"e", certificate.e}, {"d", certificate.encryptedD}, {"hint", certificate.hint}};
    const QJsonObject backup{{"type", "WizNotePlus.EncryptedCertificateBackup"}, {"version", 1},
        {"state", "unregistered"}, {"format", "ZIWR-v1-legacy"},
        {"publicFingerprint", publicFingerprint(certificate)}, {"certificate", fields}};
    return QJsonDocument(backup).toJson(QJsonDocument::Indented);
}

Result restoreBytes(const QByteArray& bytes, const QString& password)
{
    if (bytes.isEmpty() || bytes.size() > maximumBackupBytes) return failed(Error::InvalidBackup);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return failed(Error::InvalidBackup);
    const auto object = document.object();
    const QStringList keys{"certificate", "format", "publicFingerprint", "state", "type", "version"};
    if (object.keys() != keys || object.value("type") != "WizNotePlus.EncryptedCertificateBackup"
        || object.value("version").toDouble() != 1 || object.value("state") != "unregistered"
        || object.value("format") != "ZIWR-v1-legacy" || !object.value("certificate").isObject()) return failed(Error::InvalidBackup);
    const auto fields = object.value("certificate").toObject();
    if (fields.keys() != QStringList{"d", "e", "hint", "n"}) return failed(Error::InvalidBackup);
    for (const auto& field : fields) if (!field.isString()) return failed(Error::InvalidBackup);
    Certificate certificate{fields.value("n").toString(), fields.value("e").toString(), fields.value("d").toString(), fields.value("hint").toString()};
    const auto fingerprint = publicFingerprint(certificate);
    if (fingerprint.isEmpty() || object.value("publicFingerprint") != fingerprint) return failed(Error::InvalidBackup);
    const auto verification = verify(certificate, password);
    if (verification != Error::None) return failed(verification);
    return {Error::None, certificate};
}

BackupResult saveNewBackup(const QString& path, const Certificate& certificate, const QString& password)
{
    const auto verification = verify(certificate, password);
    if (verification != Error::None) return {verification, false};
    const auto bytes = backupBytes(certificate);
    const QFileInfo target(path);
    if (path.isEmpty() || !target.isAbsolute() || !QDir(target.absolutePath()).exists()) return {Error::BackupWriteFailed, false};
#ifdef Q_OS_UNIX
    // Build privately and publish with link(2), whose destination MUST NOT
    // exist. QSaveFile's replacement semantics are inappropriate for backups.
    QTemporaryFile temporary(target.absolutePath() + "/.wizcert-XXXXXX");
    if (!temporary.open() || !temporary.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || temporary.write(bytes) != bytes.size() || !temporary.flush()
        || ::fsync(temporary.handle()) != 0) return {Error::BackupWriteFailed, false};
    const auto sourceName = QFile::encodeName(temporary.fileName()), targetName = QFile::encodeName(path);
    if (::link(sourceName.constData(), targetName.constData()) != 0)
        return {errno == EEXIST ? Error::BackupExists : Error::BackupWriteFailed, false};
    const auto parentName = QFile::encodeName(target.absolutePath());
    const int parent = ::open(parentName.constData(), O_RDONLY | O_DIRECTORY);
    const bool synced = parent >= 0 && ::fsync(parent) == 0;
    if (parent >= 0) ::close(parent);
    // TemporaryFile removes ONLY its private staging name. A published file
    // survives a sync warning and is never unlinked or silently overwritten.
    return {synced ? Error::None : Error::BackupSyncUnconfirmed, true};
#else
    Q_UNUSED(bytes);
    return {Error::UnsupportedPlatform, false};
#endif
}

Result restoreBackup(const QString& path, const QString& password)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)
        || file.size() > maximumBackupBytes) return failed(Error::BackupReadFailed);
    const auto bytes = file.read(maximumBackupBytes + 1);
    if (file.error() != QFileDevice::NoError) return failed(Error::BackupReadFailed);
    return restoreBytes(bytes, password);
}
}
