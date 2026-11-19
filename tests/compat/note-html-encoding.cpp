#include "share/WizNoteHtmlEncoding.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int passed=0, failed=0;
    const auto check = [&](const char* label, bool ok) {
        std::printf("%s %s\n",ok?"PASS":"FAIL",label);
        if (ok) ++passed; else ++failed;
    };
    const QByteArray sameBytes=QByteArray::fromHex("e69687e5ad97");
    const auto alternate=QTextCodec::codecForName("GB18030")->toUnicode(sameBytes);
    const auto modern=QString::fromUtf8(sameBytes);
    const QString content=QStringLiteral("測試繁體中文");
    const auto traditional=QTextCodec::codecForName("Big5-HKSCS")->fromUnicode(content);
    const auto shows = [&](const QByteArray& head, const QByteArray& body, const QString& expected) {
        QString result;
        return WizNoteHtmlEncoding::decode(head+"<body>"+body+"</body>",result)
            && result.section("<body>",-1).section("</body>",0,0)==expected;
    };
    check("same legal UTF-8 bytes honor explicit GB18030",alternate!=modern &&
        shows("<meta charset=gb18030>",sameBytes,alternate));
    check("upper case, whitespace, single quotes",shows("<META CHARSET = ' GBK '>",sameBytes,alternate));
    check("GB2312 pragma with reversed attributes",shows(
        "<meta content='text/html; CHARSET = GB2312' http-equiv=Content-Type>",sameBytes,alternate));
    check("Big5",shows("<meta charset=Big5>",traditional,content));
    check("Big5 legacy alias",shows("<meta charset=csBig5>",traditional,content));
    check("unknown declaration keeps UTF-8",shows("<meta charset=unsupported>",sameBytes,modern));
    check("actual UTF-8 wins over following meta",shows("<meta charset=utf-8><meta charset=gb18030>",sameBytes,modern));
    check("comments do not declare charset",shows("<!--<meta charset=gb18030>-->",sameBytes,modern));
    check("script raw text does not declare charset",shows("<script>'<meta charset=gb18030>'</script>",sameBytes,modern));
    check("title raw text does not declare charset",shows("<title><meta charset=gb18030></title>",sameBytes,modern));
    check("quoted data attribute is not a tag",shows("<div title='<meta charset=gb18030>'></div>",sameBytes,modern));
    check("data-charset is not charset",shows("<meta data-charset=gb18030>",sameBytes,modern));
    check("content has no unqualified pragma",shows("<meta content='text/html; charset=gb18030'>",sameBytes,modern));
    check("late meta after body does not declare charset",shows("<body></body><meta charset=gb18030>",sameBytes,modern));
    check("only first attribute wins",shows("<meta charset=utf-8 charset=gb18030>",sameBytes,modern));
    check("bounded prescan",shows(QByteArray(1024,' ')+"<meta charset=gb18030>",sameBytes,modern));
    check("UTF-8 BOM wins over old charset",shows(QByteArray::fromHex("efbbbf")+"<meta charset=gb18030>",sameBytes,modern));
    auto* utf16=QTextCodec::codecForName("UTF-16");
    QString utf16Decoded;
    const auto wide=QStringLiteral("<meta charset=gb18030><body>測試</body>");
    check("UTF-16 BOM wins",WizNoteHtmlEncoding::decode(utf16->fromUnicode(wide),utf16Decoded)&&utf16Decoded==wide);
    const auto emoji=QStringLiteral("😀");
    check("invalid declared legacy falls back to valid UTF-8",
        shows("<meta charset=big5>",emoji.toUtf8(),emoji));
    QString preserved=QStringLiteral("previous");
    check("bad bytes under known legacy and UTF-8 fail without content output",
        !WizNoteHtmlEncoding::decode(QByteArray("<meta charset=gb18030>")+QByteArray::fromHex("ff"),preserved)
        && preserved==QStringLiteral("previous"));
    std::printf("%d passed, %d failed\n",passed,failed);
    return failed?1:0;
}
