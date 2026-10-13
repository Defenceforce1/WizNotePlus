#include "WizNotePresentation.h"
#include "share/WizSettings.h"
#include "share/WizGlobal.h"
#include "share/WizWebEngineView.h"
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
    Style custom{QStringLiteral("body { line-height:1.7; }"), QStringLiteral("h1 { color:#164e86; }"), true};
    QString error;
    check(saveStyle(&settings, custom, error) && loadStyle(&settings).screen == custom.screen
        && loadStyle(&settings).print == custom.print, "screen and print CSS persist only in local settings");
    Style huge{QString(65537, 'x'), "", true};
    check(!saveStyle(&settings, huge, error) && loadStyle(&settings).screen == custom.screen,
        "oversize CSS rejected without changing settings");
    check(styleSheet(Style{custom.screen, custom.print, false}).isEmpty(), "CSS can be disabled without losing source");
    Style live;
    bool dialogVisited = false;
    QTimer::singleShot(100, [&] {
        auto* dialog = QApplication::activeModalWidget();
        auto* field = dialog ? dialog->findChild<QPlainTextEdit*>("wiz-note-screen-css") : nullptr;
        auto* preview = dialog ? dialog->findChild<QPushButton*>("wiz-note-css-preview") : nullptr;
        if (field && preview) { field->setPlainText("body { color:red; }"); preview->click(); dialogVisited = true; }
        if (dialog) qobject_cast<QDialog*>(dialog)->reject();
    });
    editStyle(nullptr, &settings, [&](const Style& value) { live = value; });
    check(dialogVisited && live.screen == custom.screen && loadStyle(&settings).screen == custom.screen,
        "canceling live CSS preview restores saved style");
    WizUserSettings user("synthetic-presentation-account");
    user.setPrintMarginUnit(2);
    for (auto side : {wizPositionLeft, wizPositionTop, wizPositionRight, wizPositionBottom}) user.setPrintMarginValue(side, 0.5);
    check(qAbs(printLayout(user).margins(QPageLayout::Millimeter).left() - 12.7) < 0.01,
        "legacy inch margins are converted to millimeters");
    user.setPrintMarginValue(wizPositionLeft, -10);
    check(printLayout(user).margins(QPageLayout::Millimeter).left() == 5, "invalid legacy margins use safe default");
    QString artifactDir = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
    if (artifactDir.isEmpty()) artifactDir = root.filePath("pdfs");
    if (!QDir().mkpath(artifactDir)) return 1;
    const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    const QString image = "<img width='100' height='40' src='data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%22100%22 height=%2240%22%3E%3Crect width=%22100%22 height=%2240%22 fill=%22%23448aff%22/%3E%3C/svg%3E'>";
    QString rich = QStringLiteral("<html><head><meta charset='utf-8'></head><body><h1>打印测试 / Print check</h1><p>中文字体、图片和自定义打印 CSS。</p>") + image;
    rich += "<table style='border-collapse:collapse'><tr><th>项目</th><th>结果</th></tr><tr><td>原笔记</td><td>保持可编辑</td></tr></table>";
    for (int i = 1; i <= 60; ++i) rich += QString("<p>段落 %1：验证分页、清晰度与正文边距。 A local synthetic note, not user data.</p>").arg(i);
    rich += "<h2>最后一节</h2><p>分页后的结束标记 / END OF FIXTURE</p></body></html>";
    const QString markdown = QString::fromUtf8(R"HTML(<html><head><meta charset="utf-8"></head><body># 数学公式与 Markdown<br><br>这是一份隔离测试，保留原始 Markdown 输入。<br><br>行内公式 $a^2+b^2=c^2$，以及显示公式：<br><br>$$\begin{equation}\label{eq:energy}E=mc^2\end{equation}$$<br><br>交叉引用：公式 $\eqref{eq:energy}$。<br><br>$$\int_0^1 x^2\,dx=\frac{1}{3}$$<br><br>## 列表与代码<br><br>- 项目一<br>- 项目二<br><br>```cpp<br>int answer = 42;<br>```<br><br>Markdown source remains unchanged.</body></html>)HTML");
    const QStringList htmls{rich, markdown};
    for (int index = 0; index < htmls.size(); ++index) {
        const QString type = index ? "markdown" : "common";
        WizNotePrintDialog dialog(htmls[index], type, QUrl::fromLocalFile(root.filePath("note.html")), custom, layout);
        dialog.show();
        check(waitFor([&] { return dialog.ready() || !dialog.error().isEmpty(); }) && dialog.ready(),
            index ? "Markdown print copy renders offline" : "rich-text print copy renders offline");
        if (!dialog.ready()) { qInfo() << dialog.error(); continue; }
        if (index) {
            const auto maths = evaluate(dialog.webView()->page(), "window.MathJax && MathJax.Hub ? MathJax.Hub.getAllJax().length : 0").toInt();
            check(maths >= 4, "original formula renderer finished all inline/display/reference expressions");
            const QString text = evaluate(dialog.webView()->page(), "document.body.innerText").toString();
            qInfo() << "Markdown rendered text" << text.left(1000);
            check(text.contains("int answer = 42;") && !text.contains("\\eqref") && !text.contains("??"),
                "Markdown code and resolved cross-reference are rendered");
        }
        const QString sourceQuery = index ? "WizEditor.getMarkdownSrc({unEscapeHtml:true})" : "WizEditor.getContentHtml()";
        const QString before = evaluate(dialog.webView()->page(), sourceQuery).toString();
        check(!before.isEmpty() && (!index || before.contains("\\eqref{eq:energy}")), "original note source is retained after rendering");
        evaluate(dialog.webView()->page(), "WizEditor.insertCustomStyle('wiz_tmp_style_test','body { --synthetic-css-sentinel: 12345; }',true)");
        const QString serialized = evaluate(dialog.webView()->page(), "WizEditor.getContentHtml()").toString();
        check(!serialized.contains("synthetic-css-sentinel"), "temporary CSS is excluded from serialized note HTML");
        const QString after = evaluate(dialog.webView()->page(), sourceQuery).toString();
        check(before == after, "CSS does not change note source");
        bool done = false, saved = false;
        const QString path = QDir(artifactDir).filePath(index ? "markdown-formulas.pdf" : "rich-text-pagination.pdf");
        dialog.writePdf(path, [&](bool ok, const QString& message) { saved = ok; done = true; if (!ok) qInfo() << message; });
        check(waitFor([&] { return done; }, 30000) && saved, "actual app PDF generation completes");
        QFile pdf(path);
        const bool readable = pdf.open(QIODevice::ReadOnly);
        const QByteArray bytes = readable ? pdf.readAll() : QByteArray();
        check(bytes.startsWith("%PDF-") && bytes.size() > 1000, "generated PDF has valid signature and content");
        auto pages = QRegularExpression("/Type\\s*/Page(?=\\s|/)").globalMatch(QString::fromLatin1(bytes));
        int pageCount = 0; while (pages.hasNext()) { pages.next(); ++pageCount; }
        check(index ? pageCount == 1 : pageCount >= 2,
            index ? "short Markdown PDF has no extra blank page" : "long rich-text PDF is paginated");
        done = false; saved = true;
        dialog.writePdf(root.filePath("missing-directory/no.pdf"), [&](bool ok, const QString&) { saved = ok; done = true; });
        check(waitFor([&] { return done; }, 30000) && !saved, "unwritable output reports failure safely");
        dialog.reject();
    }
    check(requests.blocked == 0, "bundled Markdown/formula printing requires no remote resource request");
    WizWebEngineProfile()->setUrlRequestInterceptor(nullptr);
    qInfo() << "Presentation failures:" << failures;
    return failures ? 1 : 0;
}
