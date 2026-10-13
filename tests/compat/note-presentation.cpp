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
    QTimer::singleShot(100, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* field = dialog ? dialog->findChild<QPlainTextEdit*>("wiz-note-screen-css") : nullptr;
        auto* preview = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-preview") : nullptr;
        auto* math = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-math-preset") : nullptr;
        if (field && preview) { field->setPlainText("body { color:red; }"); preview->click(); dialogVisited = true; }
        if (field && math) {
            math->click(); math->click();
            presetVisited = field->toPlainText().startsWith("body { color:red; }")
                && field->toPlainText().count("/* WizNotePlus math-note blocks */") == 1;
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

    Style math{mathBlockStyle(), true};
    check(saveStyle(&settings, math, error), "one global math-block stylesheet is saved locally");
    const QStringList sources{
        QString::fromUtf8("# 数学引用块测试\n\n> **定理 1（测试）**\n>\n> 对任意实数 $x$，有 $x^2 \\ge 0$。\n>\n> $$x^2 \\ge 0$$\n\n原来的 Markdown 引用块，不使用新语法。"),
        QString::fromUtf8("# 第二篇 Markdown\n\n> **定义（测试）**\n>\n> 这篇也使用同一份 CSS，不逐篇设置。\n\n```cpp\nint answer = 42;\n```\n")
    };
    for (int index = 0; index < sources.size(); ++index) {
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
        const QString query = "WizEditor.getMarkdownSrc({unEscapeHtml:true})";
        const QString before = evaluate(reader.page(), query).toString();
        evaluate(reader.page(), "WizApplyNoteStyle('')");
        // Restore from the same global setting, not per-note content.
        evaluate(reader.page(), "WizApplyNoteStyle(" + jsonString(styleSheet(loadStyle(&settings))) + ")");
        check(!before.isEmpty() && before == evaluate(reader.page(), query).toString(),
            "applying/resetting common CSS leaves original Markdown source unchanged");
        check(!evaluate(reader.page(), "WizEditor.getContentHtml()").toString().contains("WizNotePlus math-note blocks"),
            "math-block CSS is temporary and excluded from serialized note HTML");
        if (index == 0) check(evaluate(reader.page(), "window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0").toInt() >= 2,
            "original formula engine still renders inline and block mathematics");
        const QString artifacts = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) {
            QEventLoop settle; QTimer::singleShot(150, &settle, &QEventLoop::quit); settle.exec();
            reader.grab().save(QDir(artifacts).filePath(QString("v12-math-block-%1.png").arg(index)));
        }
        reader.close();
    }
    check(requests.blocked == 0, "Markdown math-block CSS tests require no remote resources");
    WizWebEngineProfile()->setUrlRequestInterceptor(nullptr);
    qInfo() << "Presentation failures:" << failures;
    return failures ? 1 : 0;
}
