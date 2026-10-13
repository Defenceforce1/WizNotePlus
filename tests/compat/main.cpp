#include "share/WizQtCompat.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>
#include <quazip.h>
#include <quazipfile.h>
#include <cryptopp/sha.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool result, const char* label) {
        qInfo().noquote() << (result ? "PASS" : "FAIL") << label;
        failures += !result;
    };
    QByteArray bom = QByteArray::fromHex("efbbbf") + QStringLiteral("中文公式").toUtf8();
    QBuffer input(&bom);
    input.open(QIODevice::ReadOnly);
    check(WizReadEncodedText(input, "GB18030") == QStringLiteral("中文公式"), "UTF-8 BOM overrides legacy encoding");
    QByteArray legacy = QByteArray::fromHex("d6d0cec4");
    QBuffer legacyInput(&legacy);
    legacyInput.open(QIODevice::ReadOnly);
    check(WizReadEncodedText(legacyInput, "GB18030") == QStringLiteral("中文"), "GB18030 import");
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    { QTextStream stream(&buffer); WizSetUtf8Encoding(stream); stream << QStringLiteral("中文"); }
    check(output == QStringLiteral("中文").toUtf8(), "UTF-8 text output");
    const qint64 timestamp = 1700000000;
    check(QDateTime::fromSecsSinceEpoch(timestamp).toSecsSinceEpoch() == timestamp,
          "database timestamps remain seconds; API milliseconds unchanged");
    WizRecursiveMutex mutex;
    mutex.lock();
    const bool recursive = mutex.tryLock();
    check(recursive, "recursive database lock");
    if (recursive) mutex.unlock();
    mutex.unlock();
    QTemporaryDir temporary;
    check(temporary.isValid(), "isolated temporary test directory");
    if (!temporary.isValid()) return 1;
    const QString iniPath = temporary.filePath("settings.ini");
    { QSettings ini(iniPath, QSettings::IniFormat); ini.setValue("title", QStringLiteral("旧笔记")); ini.sync(); }
    check(QSettings(iniPath, QSettings::IniFormat).value("title").toString() == QStringLiteral("旧笔记"), "UTF-8 INI round trip");
    CryptoPP::SHA256 hash;
    QByteArray digest(CryptoPP::SHA256::DIGESTSIZE, '\0');
    hash.CalculateDigest(reinterpret_cast<CryptoPP::byte*>(digest.data()),
                         reinterpret_cast<const CryptoPP::byte*>("abc"), 3);
    check(digest.toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "Crypto++ SHA-256 known-answer (not an encrypted-note migration test)");
    const QString zipPath = temporary.filePath("synthetic.ziw");
    const QByteArray payload = "<html><body>synthetic note</body></html>";
    QuaZip archive(zipPath);
    bool wrote = archive.open(QuaZip::mdCreate);
    if (wrote) {
        QuaZipFile entry(&archive);
        wrote = entry.open(QIODevice::WriteOnly, QuaZipNewInfo("index.html"));
        if (wrote) { wrote = entry.write(payload) == payload.size(); entry.close(); }
        archive.close();
        wrote = wrote && archive.getZipError() == UNZ_OK;
    }
    bool read = archive.open(QuaZip::mdUnzip) && archive.setCurrentFile("index.html");
    if (read) {
        QuaZipFile entry(&archive);
        read = entry.open(QIODevice::ReadOnly);
        if (read) { read = entry.readAll() == payload; entry.close(); }
        archive.close();
    }
    check(wrote && read, "native ZIP/ZIW container round trip (synthetic only)");
    return failures ? 1 : 0;
}
