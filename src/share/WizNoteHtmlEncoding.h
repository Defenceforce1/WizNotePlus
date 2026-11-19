#ifndef WIZ_NOTE_HTML_ENCODING_H
#define WIZ_NOTE_HTML_ENCODING_H

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QTextCodec>
#include <utility>

namespace WizNoteHtmlEncoding {

// Only the leading HTML can declare the encoding. Recognize actual meta
// attributes, not a charset mentioned in a comment, script, or note body.
inline QByteArray declaredCharset(const QByteArray& bytes)
{
    const auto head = QString::fromLatin1(bytes.left(1024));
    static const QRegularExpression tag(
        QStringLiteral(R"rx(<!--[\s\S]*?(?:-->|$)|<(/?)([A-Za-z][A-Za-z0-9:-]*)(?=[\t\n\f\r />])((?:"[^"]*"|'[^']*'|[^"'<>])*)>)rx"));
    static const QRegularExpression attribute(
        QStringLiteral(R"rx(([^\t\n\f\r "'<>/=]+)(?:[\t\n\f\r ]*=[\t\n\f\r ]*(?:"([^"]*)"|'([^']*)'|([^\t\n\f\r "'=<>\x60]+)))?)rx"));
    static const QRegularExpression pragmaCharset(
        QStringLiteral(R"rx((?:^|;)\s*charset\s*=\s*(?:"([^"]+)"|'([^']+)'|([^\s;]+)))rx"),
        QRegularExpression::CaseInsensitiveOption);
    auto tags = tag.globalMatch(head);
    QString rawElement;
    while (tags.hasNext()) {
        const auto t = tags.next();
        const auto name = t.captured(2).toLower();
        const bool closing = !t.captured(1).isEmpty();
        if (!rawElement.isEmpty()) {
            if (closing && name == rawElement) rawElement.clear();
            continue;
        }
        if ((closing && name == "head") || (!closing && name == "body")) break;
        if (closing) continue;
        if (name == "script" || name == "style" || name == "title" ||
            name == "textarea" || name == "xmp" || name == "noscript" ||
            name == "template" || name == "iframe" || name == "noembed" ||
            name == "noframes") {
            rawElement = name;
            continue;
        }
        if (name == "plaintext") break;
        if (name != "meta") continue;
        QMap<QString,QString> attrs;
        auto matches = attribute.globalMatch(t.captured(3));
        while (matches.hasNext()) {
            const auto a = matches.next();
            const auto key = a.captured(1).toLower();
            if (!attrs.contains(key)) attrs.insert(key, a.captured(a.lastCapturedIndex()).trimmed());
        }
        QString label = attrs.value("charset");
        if (!attrs.contains("charset") &&
            attrs.value("http-equiv").compare("content-type", Qt::CaseInsensitive) == 0) {
            const auto c = pragmaCharset.match(attrs.value("content"));
            if (c.hasMatch()) label = c.captured(c.lastCapturedIndex());
        }
        label = label.trimmed().toLower();
        // Honor explicit modern UTF-8 and the legacy Chinese encodings that
        // triggered this issue; do not guess other encodings for old notes.
        if (label == "utf-8" || label == "utf8" || label == "gb18030" ||
            label == "gbk" || label == "gb2312" || label == "gb_2312" ||
            label == "gb_2312-80" || label == "x-gbk" || label == "chinese" ||
            label == "csiso58gb231280" || label == "iso-ir-58" ||
            label == "big5" || label == "big5-hkscs" || label == "cn-big5" ||
            label == "csbig5" || label == "x-x-big5") return label.toLatin1();
    }
    return {};
}

inline bool decode(const QByteArray& bytes, QString& text)
{
    auto* utf8 = QTextCodec::codecForName("UTF-8");
    auto* bom = QTextCodec::codecForUtfText(bytes, nullptr);
    if (bom) {
        text = bom->toUnicode(bytes);
        return true;
    }
    const auto label = declaredCharset(bytes);
    if (!label.isEmpty() && label != "utf-8" && label != "utf8") {
        const bool big5 = label.contains("big5");
        auto* legacy = QTextCodec::codecForName(big5 ? "Big5-HKSCS" : "GB18030");
        if (legacy) {
            QTextCodec::ConverterState state;
            auto decoded = legacy->toUnicode(bytes.constData(), bytes.size(), &state);
            if (state.invalidChars == 0 && state.remainingChars == 0 && !decoded.contains(QChar(0xfffd))) {
                text = std::move(decoded);
                return true;
            }
            // A stale declaration must not damage otherwise valid UTF-8.
            QTextCodec::ConverterState fallback;
            auto decodedUtf8 = utf8->toUnicode(bytes.constData(), bytes.size(), &fallback);
            if (fallback.invalidChars || fallback.remainingChars) return false;
            text = std::move(decodedUtf8);
            return true;
        }
    }
    text = utf8->toUnicode(bytes);
    return true;
}

inline bool readFile(const QString& path, QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = file.readAll();
    return file.error() == QFileDevice::NoError && decode(bytes, text);
}

}
#endif
