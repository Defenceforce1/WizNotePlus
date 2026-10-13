#include "WizNotePresentation.h"
#include "share/WizSettings.h"
#include "share/WizGlobal.h"
#include "share/WizWebEngineView.h"
#include "utils/WizPathResolve.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QDebug>
#include <memory>
#include <atomic>

namespace {
class LocalRequests final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme = info.requestUrl().scheme();
        if (scheme != "file" && scheme != "data" && scheme != "qrc" && scheme != "about" && scheme != "blob") {
            ++blocked; info.block(true);
        }
    }
};
bool waitFor(const std::function<bool()>& predicate, int timeout = 65000) {
    QEventLoop loop; QTimer poll; QTimer deadline;
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, [&] { if (predicate()) { ok = true; loop.quit(); } });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(50); deadline.setSingleShot(true); deadline.start(timeout); loop.exec(); return ok;
}
QVariant evaluate(QWebEnginePage* page, const QString& code) {
    struct Result { bool received = false; QVariant value; };
    auto result = std::make_shared<Result>();
    page->runJavaScript(code, [result](const QVariant& value) { result->value = value; result->received = true; });
    waitFor([&] { return result->received; }, 10000); return result->value;
}
}

int wizNotePresentationSelfTest(int argc, char** argv) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Presentation Regression Test");
    QSettings settings(root.filePath("user.ini"), QSettings::IniFormat);
    QSettings global(root.filePath("global.ini"), QSettings::IniFormat);
    WizGlobal::setSettings(&settings); WizGlobal::setGlobalSettings(&global);
    LocalRequests requests;
    WizWebEngineProfile()->setUrlRequestInterceptor(&requests);
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label; failures += !ok; return ok;
    };
    using namespace WizNotePresentation;
    const QString tricky = QString::fromUtf8("quotes \" backslash \\ </script> 中文") + QChar(0x2028);
    const QString json = jsonString(tricky);
    check(QJsonDocument::fromJson(("[" + json + "]").toUtf8()).array()[0].toString() == tricky
        && !json.contains("</script>"), "CSS JSON round-trip is script-safe");
    Style custom{QStringLiteral("body { line-height:1.7; } h1 { color:#164e86; }"), true};
    settings.setValue("NotePresentation/printCss", "retired-print-sentinel");
    QString error;
    check(saveStyle(&settings, custom, error) && loadStyle(&settings).screen == custom.screen
        && settings.value("NotePresentation/printCss").toString() == "retired-print-sentinel", "common CSS persists locally without deleting retired print CSS");
    Style huge{QString(65537, 'x'), true};
    check(!saveStyle(&settings, huge, error) && loadStyle(&settings).screen == custom.screen,
        "oversize CSS rejected without changing settings");
    check(styleSheet(Style{custom.screen, false}).isEmpty(), "CSS can be disabled without losing source");
    Style live;
    bool dialogVisited = false;
    bool presetVisited = false;
    bool scientificPresetVisited = false;
    QTimer::singleShot(100, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* field = dialog ? dialog->findChild<QPlainTextEdit*>("wiz-note-screen-css") : nullptr;
        auto* preview = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-preview") : nullptr;
        auto* math = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-math-preset") : nullptr;
        auto* scientific = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-scientific-preset") : nullptr;
        if (field && preview) { field->setPlainText("body { color:red; }"); preview->click(); dialogVisited = true; }
        if (field && math) {
            math->click(); math->click();
            presetVisited = field->toPlainText().startsWith("body { color:red; }")
                && field->toPlainText().count("/* WizNotePlus math-note blocks */") == 1;
        }
        if (field && scientific) {
            scientific->click(); scientific->click();
            scientificPresetVisited = field->toPlainText().startsWith("body { color:red; }")
                && field->toPlainText().count("/* WizNotePlus scientific math environments v1 */") == 1;
        }
        if (dialog) qobject_cast<QDialog*>(dialog)->reject();
    });
    editStyle(nullptr, &settings, [&](const Style& value) { live = value; });
    check(dialogVisited && live.screen == custom.screen && loadStyle(&settings).screen == custom.screen,
        "canceling live CSS preview restores saved style");
    check(styleSheet(custom) == custom.screen && !styleSheet(custom).contains("@media"), "one common stylesheet is not restricted to screen media");
    check(!styleSheet(loadStyle(&settings)).contains("retired-print-sentinel"), "retired print CSS is ignored");
    check(presetVisited && loadStyle(&settings).screen == custom.screen,
        "math block preset appends once and cancel never saves or replaces existing CSS");
    check(scientificPresetVisited && loadStyle(&settings).screen == custom.screen,
        "scientific preset appends once without replacing CSS or saving on cancel");
    check(!scientificMathStyle().contains("@nb_") && !scientificMathStyle().contains("<script")
        && !scientificMathStyle().contains("mjx-container"),
        "scientific stylesheet excludes Notebooks directives and foreign MathJax configuration");

    Style math{mathBlockStyle(), true};
    check(saveStyle(&settings, math, error), "one global math-block stylesheet is saved locally");
    QStringList sources{
        QString::fromUtf8("# 数学引用块测试\n\n> **定理 1（测试）**\n>\n> 对任意实数 $x$，有 $x^2 \\ge 0$。\n>\n> $$x^2 \\ge 0$$\n\n原来的 Markdown 引用块，不使用新语法。"),
        QString::fromUtf8("# 第二篇 Markdown\n\n> **定义（测试）**\n>\n> 这篇也使用同一份 CSS，不逐篇设置。\n\n```cpp\nint answer = 42;\n```\n"),
        QString::fromUtf8("# 分类数学样式\n\n<div class=\"definition\"><p>称 $\\mathbb{R}$ 为实数集。</p></div>\n\n"
            "<div class=\"theorem\"><p>对于 $x\\in\\mathbb{R}$，有 $x^2\\ge 0$。</p></div>\n\n"
            "<div class=\"lemma\"><p>平方和亦非负。</p></div>\n\n"
            "<div class=\"example\"><p>取 $x=2$，则 $x^2=4$。</p></div>\n\n"
            "<div class=\"proof\"><p>由实数乘法的性质可得结论。</p></div>\n\n")
        ,QString::fromUtf8("# 数学块与引用\n\n由定理 \\ref{thm:positive} 可知结论；再看 $\\ref{thm:positive}$。\n\n"
            "> **定义（实数）** \\label{def:real}\n>\n> 实数集记为 $\\mathbb{R}$。\n\n"
            "> **定理（非负性）** \\label{thm:positive}\n>\n> 对于实数 $x$，有 $x^2\\ge 0$。\n\n"
            "> **引理** \\label{lem:sum}\n>\n> 平方和非负。\n\n"
            "> **例**\n>\n> 取 $x=2$。\n\n"
            "> **证明**\n>\n> 由实数的乘法性质可得结论。\n\n"
            "> **定理 7（手写编号）** \\label{thm:manual}\n>\n> 此编号保持为 7。\n\n"
            "> **推论** \\label{cor:next}\n>\n> 编号继续为 8。\n\n"
            "<!-- -->\n\n> **定理相关问题**\n>\n> 普通引用块，不识别为环境。\n\n"
            "```text\n\\ref{thm:positive}\n> **定理** \\label{code:untouched}\n```\n")
        ,QString::fromUtf8("# 重复标签\n\n\\ref{thm:duplicate} 和 \\ref{unknown:label}\n\n"
            "> **定理** \\label{thm:duplicate}\n>\n> 第一条。\n\n"
            "> **定义** \\label{thm:duplicate}\n>\n> 第二条。\n\n")
    };
    QFile example(Utils::WizPathResolve::resourcesPath() + "files/examples/mathematical-notes.md");
    if (!check(example.open(QIODevice::ReadOnly) && example.size() > 0 && example.size() < 65536,
        "bundled user-facing Markdown example is available and bounded")) return 1;
    const QString exampleSource = QString::fromUtf8(example.readAll());
    sources.append(exampleSource);
    for (int index = 0; index < sources.size(); ++index) {
        if (index == 2) {
            math.screen = scientificMathStyle();
            check(saveStyle(&settings, math, error), "scientific CSS uses the same global setting");
        }
        const QString resource = Utils::WizPathResolve::resourcesPath() + "files/wizeditor/";
        QString source = sources[index].toHtmlEscaped(); source.replace('\n', "<br>");
        const QString html = "<html><head><meta charset='utf-8'></head><body>" + source
            + "<script src=\"" + QUrl::fromLocalFile(resource + "wizEditorForMac.js").toString().toHtmlEscaped() + "\"></script>"
            + "<script src=\"" + QUrl::fromLocalFile(resource + "editorHelper.js").toString().toHtmlEscaped() + "\"></script>"
            + "<script>window.WizNoteCss=" + jsonString(styleSheet(loadStyle(&settings)))
            + ";WizEditor.init({document:document,lang:'zh-cn',clientType:'web',noteType:'markdown',dependencyUrl:"
            + jsonString(QUrl::fromLocalFile(resource + "dependency").toString())
            + ",reader:{type:'markdown',callback:{markdown:function(){WizApplyNoteStyle();},mathJax:function(){WizApplyNoteStyle();}}}});"
            + "WizEditor.off({reader:{type:'markdown'}},function(){var done=function(){WizApplyNoteStyle();"
            + "requestAnimationFrame(function(){requestAnimationFrame(function(){window.FixtureReady=true;});});};"
            + "if(window.MathJax&&MathJax.Hub)MathJax.Hub.Queue(done);else done();});</script></body></html>";
        const QString path = root.filePath(QString("math-block-%1.html").arg(index));
        QFile input(path);
        if (!input.open(QIODevice::WriteOnly) || input.write(html.toUtf8()) != html.toUtf8().size()) return 1;
        input.close();
        QWebEngineView reader;
        reader.setPage(new QWebEnginePage(WizWebEngineProfile(), &reader));
        reader.resize(850, 660); reader.show(); reader.load(QUrl::fromLocalFile(path));
        const bool ready = waitFor([&] { return evaluate(reader.page(), "!!window.FixtureReady").toBool(); });
        check(ready, "existing bundled Markdown reader completes offline");
        if (!ready) continue;
        if (index < 2) {
        const auto style = evaluate(reader.page(),
            "(function(){var q=document.querySelector('blockquote'),t=q&&q.querySelector('strong');"
            "if(!q||!t)return {};var a=getComputedStyle(q),b=getComputedStyle(t);"
            "return {border:a.borderLeftWidth,background:a.backgroundColor,title:b.color,titleDisplay:b.display,"
            "bodyClass:document.body.className,markdownContainer:!!q.closest('.markdown-body')};})()").toMap();
        qInfo() << "Markdown rendered CSS/structure" << style;
        check(style.value("border") == "3px" && style.value("background") == "rgb(242, 246, 251)"
            && style.value("markdownContainer").toBool()
            && style.value("title") == "rgb(36, 83, 135)" && style.value("titleDisplay") == "block",
            "common CSS styles ordinary Markdown quote block and bold title in both notes");
        } else if (index == 2) {
            const auto style = evaluate(reader.page(),
                "(function(){var t=document.querySelector('.theorem'),d=document.querySelector('.definition'),"
                "e=document.querySelector('.example'),p=document.querySelector('.proof'),l=document.querySelector('.lemma');"
                "if(!t||!d||!e||!p||!l)return {};return {theorem:getComputedStyle(t).borderLeftColor,"
                "definition:getComputedStyle(d).borderLeftColor,example:getComputedStyle(e).borderLeftColor,"
                "width:getComputedStyle(t).borderLeftWidth,reset:getComputedStyle(document.body).counterReset,"
                "shared:getComputedStyle(l).counterIncrement,qed:getComputedStyle(p,'::after').content,"
                "title:getComputedStyle(t,'::before').content};})()").toMap();
            qInfo() << "Scientific rendered CSS/structure" << style;
            check(style.value("theorem") == "rgb(75, 105, 198)" && style.value("definition") == "rgb(51, 147, 111)"
                && style.value("example") == "rgb(196, 120, 44)" && style.value("width") == "4px",
                "scientific CSS renders distinct blue green orange environments in existing Markdown HTML");
            check(style.value("shared").toString().contains("theorem-counter")
                && style.value("reset").toString().contains("definition-counter")
                && style.value("title").toString().contains("counter(theorem-counter)")
                && style.value("qed").toString().contains(QString::fromUtf8("□")),
                "scientific CSS defines shared theorem and independent definition counters plus proof end mark");
        } else if (index == 3) {
            const auto blocks = evaluate(reader.page(),
                "Array.from(document.querySelectorAll('.wiz-math-block')).map(function(b){return {heading:b.getAttribute('data-wiz-math-heading'),"
                "number:b.getAttribute('data-wiz-math-number'),id:b.id};})").toList();
            qInfo() << "Scientific quote blocks" << blocks;
            check(blocks.size() == 7 && blocks[0].toMap().value("number") == "1"
                && blocks[1].toMap().value("number") == "1" && blocks[2].toMap().value("number") == "2"
                && blocks[3].toMap().value("number") == "1" && blocks[4].toMap().value("number") == ""
                && blocks[5].toMap().value("number") == "7" && blocks[6].toMap().value("number") == "8",
                "read-only quote environments classify and number by type while retaining manual numbering");
            check(evaluate(reader.page(),
                "(function(){var links=Array.from(document.querySelectorAll('.wiz-math-reference'));return links.length===2"
                "&&links.every(function(a){return a.textContent==='1'&&a.getAttribute('href')==='#wiz-math-thm:positive';})"
                "&&!!document.getElementById('wiz-math-thm:positive')&&!document.querySelector('pre .wiz-math-reference');})()").toBool()
                && evaluate(reader.page(), "WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()
                    .contains("```text\n\\ref{thm:positive}\n> **定理** \\label{code:untouched}"),
                "forward prose and standalone inline label references link to blocks without rewriting code");
            check(evaluate(reader.page(),
                "(function(){var url=location.href,a=document.querySelector('.wiz-math-reference'),e=new MouseEvent('click',{bubbles:true,cancelable:true});"
                "a.dispatchEvent(e);return e.defaultPrevented&&location.href===url;})()").toBool(),
                "block reference clicks scroll locally without forwarding a file URL to macOS");
        } else if (index == 4) {
            check(evaluate(reader.page(),
                "document.querySelectorAll('.wiz-math-block').length===2&&document.querySelectorAll('.wiz-math-reference').length===0"
                "&&Array.from(document.querySelectorAll('.wiz-math-block')).every(function(b){return b.getAttribute('data-wiz-math-heading').indexOf('标签重复')>=0;})").toBool(),
                "duplicate labels warn on both blocks and never create an ambiguous reference");
        } else {
            check(evaluate(reader.page(),
                "Array.from(document.querySelectorAll('.wiz-math-block')).map(function(b){return b.getAttribute('data-wiz-math-number');}).join(',')==='1,2,1,,2,1,,7,8'"
                "&&document.querySelectorAll('.wiz-math-reference').length===8"
                "&&!document.querySelector('pre .wiz-math-reference')").toBool(),
                "delivered example renders independent definitions shared theorem numbers manual seven/eight and eight references");
            check(evaluate(reader.page(),
                "Array.from(document.querySelectorAll('.wiz-math-reference')).every(function(a){return !!document.getElementById(a.getAttribute('href').slice(1));})").toBool(),
                "every delivered example reference has a local destination");
            const QString actualExampleSource = evaluate(reader.page(), "WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString();
            qInfo() << "Example source lengths:" << actualExampleSource.size() << exampleSource.size();
            check(actualExampleSource == exampleSource,
                "delivered Markdown demonstration survives rendering with identical source");
        }
        const QString query = "WizEditor.getMarkdownSrc({unEscapeHtml:true})";
        const QString before = evaluate(reader.page(), query).toString();
        const QString originalHtml = evaluate(reader.page(), "WizEditor.getContentHtml()").toString();
        evaluate(reader.page(), "WizApplyNoteStyle('')");
        if (index == 3) check(evaluate(reader.page(), "document.querySelectorAll('.wiz-math-block,.wiz-math-reference,.wiz-math-source-heading').length").toInt() == 0,
            "disabling scientific CSS restores unclassified quote headings and reference source");
        // Restore from the same global setting, not per-note content.
        evaluate(reader.page(), "WizApplyNoteStyle(" + jsonString(styleSheet(loadStyle(&settings))) + ")");
        check(!before.isEmpty() && before == evaluate(reader.page(), query).toString(),
            "applying/resetting common CSS leaves original Markdown source unchanged");
        if (index == 3) check(before.contains("\\label{thm:positive}")
            && originalHtml == evaluate(reader.page(), "WizEditor.getContentHtml()").toString()
            && !originalHtml.contains("wiz-math-block") && !originalHtml.contains("data-wiz-math-heading"),
            "label decoration is excluded from saved HTML and preserves original Markdown labels");
        if (index == 3) check(evaluate(reader.page(),
            "(function(){var html=WizReader.closeDocument()||'';return html.indexOf('wiz-math-block')<0"
            "&&html.indexOf('data-wiz-math-heading')<0&&WizEditor.getMarkdownSrc({unEscapeHtml:true}).indexOf('\\\\label{thm:positive}')>=0;})()").toBool(),
            "original reader close/save path excludes decoration and preserves label source");
        check(!evaluate(reader.page(), "WizEditor.getContentHtml()").toString().contains("WizNotePlus math-note blocks"),
            "math-block CSS is temporary and excluded from serialized note HTML");
        if (index == 0) check(evaluate(reader.page(), "window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0").toInt() >= 2,
            "original formula engine still renders inline and block mathematics");
        const QString artifacts = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) {
            QEventLoop settle; QTimer::singleShot(150, &settle, &QEventLoop::quit); settle.exec();
            evaluate(reader.page(), "window.scrollTo(0,0)");
            reader.grab().save(QDir(artifacts).filePath(QString("v14-math-block-%1.png").arg(index)));
            if (index == 5) {
                for (int section : {1, 2}) {
                    evaluate(reader.page(), QString("document.querySelectorAll('h2')[%1].scrollIntoView({block:'start'})").arg(section));
                    QEventLoop draw; QTimer::singleShot(100, &draw, &QEventLoop::quit); draw.exec();
                    reader.grab().save(QDir(artifacts).filePath(QString("v14.1-example-section-%1.png").arg(section)));
                }
            }
        }
        reader.close();
    }
    check(requests.blocked == 0, "Markdown math-block CSS tests require no remote resources");
    WizWebEngineProfile()->setUrlRequestInterceptor(nullptr);
    qInfo() << "Presentation failures:" << failures;
    return failures ? 1 : 0;
}
