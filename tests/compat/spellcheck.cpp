#include "share/WizWebEngineView.h"
#include "utils/WizPathResolve.h"
#include "WizNotePresentation.h"
#include <QApplication>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineUrlRequestInterceptor>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDebug>
#include <memory>
#include <functional>

namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000) {
    QEventLoop loop; QTimer poll, deadline;
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, [&] { if (predicate()) { ok = true; loop.quit(); } });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(40); deadline.setSingleShot(true); deadline.start(timeout); loop.exec(); return ok;
}
QVariant evaluate(QWebEnginePage* page, const QString& code) {
    struct Result { bool ready = false; QVariant value; };
    auto result = std::make_shared<Result>();
    page->runJavaScript(code, [result](const QVariant& value) { result->value = value; result->ready = true; });
    waitFor([&] { return result->ready; }); return result->value;
}
class LocalOnly final : public QWebEngineUrlRequestInterceptor {
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme = info.requestUrl().scheme();
        if (scheme != "file" && scheme != "qrc" && scheme != "data" && scheme != "about" && scheme != "blob")
            info.block(true);
    }
};
class SpellingView final : public QWebEngineView {
public:
    int contextRequests = 0;
    QString misspelled;
    QStringList suggestions;
protected:
    void contextMenuEvent(QContextMenuEvent*) override {
        ++contextRequests;
        if (auto* request = lastContextMenuRequest()) {
            misspelled = request->misspelledWord();
            suggestions = request->spellCheckerSuggestions();
        }
    }
};
void rightClick(SpellingView& view, const QPoint& local) {
    QWidget* target = view.focusProxy() ? view.focusProxy() : &view;
    const QPoint p = target->mapFrom(&view, local), global = target->mapToGlobal(p);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), QPointF(global), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p), QPointF(global), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    QCoreApplication::sendEvent(target, &release);
}
}

// Actual bundled editor + browser checker, local synthetic HTML only. No user
// account/settings/notes, public network, clipboard or old client are accessed.
int wizSpellCheckSelfTest(int argc, char** argv) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Synthetic English Spelling Test");
    QWebEngineProfile profile;
    LocalOnly local;
    profile.setUrlRequestInterceptor(&local);
    profile.setSpellCheckLanguages({"en-US"});
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    const QString resource = Utils::WizPathResolve::resourcesPath() + "files/wizeditor/";
    using WizNotePresentation::jsonString;
    for (const QString& type : {QStringLiteral("common"), QStringLiteral("markdown")}) {
        qInfo() << "Spelling editor:" << type;
        const QString html = "<html><head><meta charset='utf-8'></head><body><p>This is speling.</p>"
            "<script src=" + jsonString(QUrl::fromLocalFile(resource + "wizEditorForMac.js").toString()) + "></script>"
            "<script src=" + jsonString(QUrl::fromLocalFile(resource + "editorHelper.js").toString()) + "></script>"
            "<script>WizEditor.init({document:document,lang:'zh-cn',clientType:'web',noteType:"
            + jsonString(type) + ",dependencyUrl:" + jsonString(QUrl::fromLocalFile(resource + "dependency").toString())
            + "});WizEditor.on(null,function(){WizSetSpellCheck(false);window.FixtureReady=true;});</script></body></html>";
        QFile input(root.filePath(type + ".html"));
        if (!input.open(QIODevice::WriteOnly) || input.write(html.toUtf8()) != html.toUtf8().size()) return 1;
        input.close();
        SpellingView view;
        view.setPage(new QWebEnginePage(&profile, &view));
        view.resize(850, 500); view.show(); view.activateWindow(); view.setFocus();
        view.load(QUrl::fromLocalFile(input.fileName()));
        if (!check(waitFor([&] { return evaluate(view.page(), "!!window.FixtureReady").toBool(); }, 20000),
                   "original editor loads and enters editing mode offline")) continue;
        const QString exportMethod = type == "markdown" ? "getMarkdownSrc" : "getContentHtml";
        const QString baseline = evaluate(view.page(), "WizEditor." + exportMethod + "()").toString();
        const bool modifiedBefore = evaluate(view.page(), "WizEditor.isModified()").toBool();
        check(!baseline.isEmpty() && baseline.contains("speling"), "serialization fixture contains the actual synthetic note text");
        check(evaluate(view.page(), "document.querySelector('.wiz-editor-body').getAttribute('spellcheck')==='false'").toBool(),
              "disabled preference leaves body spellchecking off");
        profile.setSpellCheckEnabled(true);
        evaluate(view.page(), "WizSetSpellCheck(true)");
        check(evaluate(view.page(), "(function(){var b=document.querySelector('.wiz-editor-body');"
            "return b.isContentEditable&&b.spellcheck&&b.lang==='en-US';})()").toBool(),
              "enabled preference reaches the existing prose editing root");
        const QString afterEnabling = evaluate(view.page(), "WizEditor." + exportMethod + "()").toString();
        if (afterEnabling != baseline)
            qInfo() << "Synthetic spelling serialization difference:" << baseline << afterEnabling;
        check(afterEnabling == baseline,
              "spelling attributes do not change saved HTML or Markdown source");
        check(evaluate(view.page(), "WizEditor.isModified()").toBool() == modifiedBefore,
              "enabling spelling cannot mark an untouched note as modified");
        evaluate(view.page(), "document.querySelector('.wiz-editor-body').setAttribute('spellcheck','false')");
        check(waitFor([&] { return evaluate(view.page(), "document.querySelector('.wiz-editor-body').spellcheck").toBool(); }),
              "editor/history attribute resets cannot silently disable spelling");

        // Real editing, not a fake spelling implementation. Browser execCommand
        // performs an ordinary text insertion into the unchanged input engine.
        evaluate(view.page(), "(function(){var b=document.querySelector('.wiz-editor-body');b.focus();"
            "var r=document.createRange();r.selectNodeContents(b);var s=getSelection();s.removeAllRanges();s.addRange(r);"
            "document.execCommand('insertText',false,'This is speling. ');b.blur();})()");
        const auto wordRect = evaluate(view.page(), "(function(){var b=document.querySelector('.wiz-editor-body');"
            "var w=document.createTreeWalker(b,NodeFilter.SHOW_TEXT),n;while(n=w.nextNode()){var i=n.textContent.indexOf('speling');"
            "if(i<0)continue;var r=document.createRange();r.setStart(n,i);r.setEnd(n,i+7);var p=r.getBoundingClientRect();"
            "return {x:p.x+p.width/2,y:p.y+p.height/2};}return {};})()").toMap();
        const QPoint p(qRound(wordRect.value("x").toDouble()), qRound(wordRect.value("y").toDouble()));
        const bool detected = waitFor([&] {
            if (view.misspelled == "speling" && !view.suggestions.isEmpty()) return true;
            rightClick(view, p); return false;
        }, 10000);
        qInfo() << "Browser spelling context:" << view.contextRequests << view.misspelled << view.suggestions;
        check(detected, "real browser flags English typo and supplies correction suggestions");
        const QString artifact = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
        if (!artifact.isEmpty() && QDir().mkpath(artifact)) view.grab().save(QDir(artifact).filePath("spelling-" + type + ".png"));
        evaluate(view.page(), "(function(){var b=document.querySelector('.wiz-editor-body');"
            "var p=document.createElement('pre');p.id='synthetic-code';p.textContent='speling variable_name';b.appendChild(p);})()");
        check(waitFor([&] { return evaluate(view.page(), "document.getElementById('synthetic-code').getAttribute('spellcheck')==='false'").toBool(); }),
              "recognized code blocks are excluded without switching the input engine");
        const QString withCode = evaluate(view.page(), "WizEditor." + exportMethod + "()").toString();
        check(!withCode.contains("spellcheck=") && !withCode.contains("lang=\"en-US\""),
              "temporary prose and code spelling attributes never leak into exported content");
        evaluate(view.page(), "document.getElementById('synthetic-code').remove()");
        profile.setSpellCheckEnabled(false);
        evaluate(view.page(), "WizSetSpellCheck(false)");
        check(evaluate(view.page(), "!document.querySelector('.wiz-editor-body').spellcheck").toBool(),
              "turning preference off disables spelling without reopening note");
        view.misspelled.clear(); view.suggestions.clear();
        const int previousRequests = view.contextRequests;
        rightClick(view, p);
        check(waitFor([&] { return view.contextRequests > previousRequests; }) && view.misspelled.isEmpty(),
              "disabled browser checker no longer reports the typo");
        const QString before = evaluate(view.page(), "WizEditor." + exportMethod + "()").toString();
        evaluate(view.page(), "WizSetSpellCheck(true);WizSetSpellCheck(true);WizSetSpellCheck(false)");
        check(evaluate(view.page(), "WizEditor." + exportMethod + "()").toString() == before,
              "repeated preference changes never rewrite text or auto-correct words");
        profile.setSpellCheckEnabled(true);
        evaluate(view.page(), "WizSetSpellCheck(true);window.FixtureReading=false;"
            "WizEditor.off(null,function(){window.FixtureReading=true;})");
        check(waitFor([&] { return evaluate(view.page(), "!!window.FixtureReading&&"
            "Array.from(document.querySelectorAll('.wiz-editor-body')).every(function(b){return !b.spellcheck;})").toBool(); }),
              "reading mode does not spellcheck the rendered preview");
        evaluate(view.page(), "window.FixtureEditing=false;WizEditor.on(null,function(){window.FixtureEditing=true;})");
        check(waitFor([&] { return evaluate(view.page(), "!!window.FixtureEditing&&"
            "!!document.querySelector('.wiz-editor-body')&&document.querySelector('.wiz-editor-body').spellcheck").toBool(); }),
              "returning to edit mode restores the selected spelling preference");
        const QString beforeEngineReset=evaluate(view.page(),"WizEditor.getContentHtml()").toString();
        evaluate(view.page(),"document.querySelector('.wiz-editor-body').setAttribute('spellcheck','false');WizApplySpellCheck();");
        check(evaluate(view.page(),"WizEditor.getContentHtml()").toString()==beforeEngineReset,
              "engine spellcheck reset cannot establish a new serialized attribute baseline");
        bool repeatedStable=true;
        for(int cycle=0;cycle<10;++cycle){
            evaluate(view.page(),"document.querySelector('.wiz-editor-body').setAttribute('spellcheck','false');WizApplySpellCheck();");
            repeatedStable&=evaluate(view.page(),"WizEditor.getContentHtml()").toString()==beforeEngineReset;
        }
        check(repeatedStable,"ten repeated engine resets and serializations retain exact rich HTML");
        evaluate(view.page(), "WizEditor.destroy()");
    }
    qInfo() << "Spelling checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
