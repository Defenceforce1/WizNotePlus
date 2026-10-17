#include "share/WizDocumentAsync.h"
#include "share/WizDocumentPageReady.h"
#include "share/WizQuitUiGuard.h"
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace {
QVariant evaluate(QWebEnginePage& page, const QString& script) {
    struct State { QVariant value; bool done = false; };
    const auto state = std::make_shared<State>();
    QEventLoop loop;
    QTimer poll, deadline;
    QObject::connect(&poll, &QTimer::timeout, [&] { if (state->done) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    page.runJavaScript(script, [state](const QVariant& value) { state->value=value; state->done=true; });
    poll.start(5); deadline.setSingleShot(true); deadline.start(10000); loop.exec();
    return state->value;
}
}

int main(int argc, char** argv) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    QWebEngineProfile profile; // Off the record, no account or note database.
    QWebEnginePage page(&profile);
    int failures=0, count=0;
    const auto check=[&](bool ok,const char* label){
        ++count; failures+=!ok; qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;
    };
    {
        QWidget window,editor(&window),disabled(&window),parent(&window),inherited(&parent);
        QAction enabledAction(&window),disabledAction(&window);
        disabledAction.setEnabled(false);disabled.setEnabled(false);parent.setEnabled(false);
        bool committed=false;
        {
            WizQuitUiGuard guard(&window,committed);
            guard.freeze(&editor);guard.freeze(&disabled);guard.freeze(&inherited);
            guard.freeze(&window);guard.freeze(&editor);
            check(!editor.isEnabled()&&!enabledAction.isEnabled(),
                "quit preparation freezes ordinary editor input and actions while async work continues");
            auto disappearing=std::make_unique<QWidget>();guard.freeze(disappearing.get());disappearing.reset();
        }
        check(editor.isEnabled()&&!disabled.isEnabled()&&enabledAction.isEnabled()&&!disabledAction.isEnabled(),
            "failed/canceled quit restores original editor and action enabled states");
        parent.setEnabled(true);
        check(inherited.isEnabled(),"quit input restoration preserves parent-inherited disabled state without making it permanent");
        {
            WizQuitUiGuard guard(&window,committed);guard.freeze(&editor);committed=true;
        }
        check(!editor.isEnabled()&&!enabledAction.isEnabled(),"committed quit cannot accept new typing after final local-save confirmation");
    }
    QEventLoop loaded;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&loaded,&QEventLoop::quit);
    QObject::connect(&page,&QWebEnginePage::loadFinished,&loaded,&QEventLoop::quit);
    page.setHtml("<!doctype html><meta charset='utf-8'><p>Isolated save snapshot fixture</p>");
    deadline.start(10000);loaded.exec();
    using namespace WizDocumentSaveSnapshot;
    check(parse(evaluate(page,editorScript(false,false))).state==State::Failed,
        "uninitialized editor is not mistaken for an unchanged saved note");
    check(parse(evaluate(page,readerScript())).state==State::Failed,
        "missing reader is not mistaken for successful saving");
    evaluate(page, "document.body.contentEditable='true';window.WizNativeEditorInitialized=true;window.text='old';window.baseline='old';window.captures=0;"
        "window.WizEditor={isModified:()=>text!==baseline,getContentHtml:()=>{++captures;return text;},setUnModified:()=>{baseline=text;}};");
    check(parse(evaluate(page,editorScript(false,false))).state==State::Unchanged
        &&evaluate(page,"captures").toInt()==0,"clean editor avoids unnecessary HTML capture");
    const auto forced=parse(evaluate(page,editorScript(true,false)));
    check(forced.state==State::Changed&&forced.html=="old","forced save captures unchanged content");
    {
        QWebEngineView view;
        view.setPage(&page);
        bool committed=false;
        {
            WizQuitUiGuard guard(&view,committed);guard.freeze(&view);
            const auto frozen=parse(evaluate(page,editorScript(true,false)));
            check(!view.isEnabled()&&frozen.state==State::Changed&&frozen.html=="old",
                "disabled native WebEngine input still permits final JavaScript save snapshots");
        }
        check(view.isEnabled(),"canceled quit restores native WebEngine input without changing its editor mode");
    }
    evaluate(page,"document.body.contentEditable='false';");
    check(parse(evaluate(page,editorScript(true,false))).state==State::Failed,
        "stale native editor mode cannot serialize a rendered preview as editable source");
    evaluate(page,"document.body.contentEditable='true';");
    evaluate(page,"window.WizNativeEditorSetup={state:'pending'};");
    check(parse(evaluate(page,editorScript(true,false))).state==State::Failed,
        "partially initialized reader/editor cannot be saved as a finished document");
    evaluate(page,"delete window.WizNativeEditorSetup;");
    check(parse(evaluate(page,editorScript(false,true))).state==State::Changed,
        "native dirty state is saved even when the JS baseline is unchanged");
    evaluate(page,"text='新内容 $x^2$';");
    const auto changed=parse(evaluate(page,editorScript(false,false)));
    check(changed.state==State::Changed&&changed.html==QStringLiteral("新内容 $x^2$")
        &&!evaluate(page,"WizEditor.isModified()").toBool(),"snapshot returns exact Unicode content and updates the same-task baseline");
    evaluate(page,"text+=' later typing';");
    check(evaluate(page,"WizEditor.isModified()").toBool(),"typing after capture remains dirty without a late baseline reset");
    evaluate(page,"WizEditor.getContentHtml=()=>'';");
    check(parse(evaluate(page,editorScript(true,false))).state==State::Failed
        &&evaluate(page,"WizEditor.isModified()").toBool(),"empty HTML fails without clearing the dirty baseline");
    evaluate(page,"WizEditor.getContentHtml=()=>{throw Error('synthetic capture failure');};");
    check(parse(evaluate(page,editorScript(true,false))).state==State::Failed,
        "throwing editor reports failure explicitly");
    evaluate(page,"window.WizReader={closeDocument:()=>null};");
    check(parse(evaluate(page,readerScript())).state==State::Failed,
        "stale native reader mode cannot acknowledge an active editable page as saved");
    evaluate(page,"document.body.contentEditable='false';");
    check(parse(evaluate(page,readerScript())).state==State::Unchanged,
        "original reader null return is a valid unchanged result");
    evaluate(page,"WizEditor.getContentHtml=()=>text;");
    const auto retry=parse(evaluate(page,readerScript(true)));
    check(retry.state==State::Changed&&retry.html.contains("later typing"),
        "failed reader save can retry from the original serializer despite a clean reader flag");
    evaluate(page,"WizReader.closeDocument=()=>'<html><body>changed</body></html>';");
    check(parse(evaluate(page,readerScript())).state==State::Changed,"changed reader HTML is captured");
    evaluate(page,"WizReader.closeDocument=()=>{throw Error('synthetic reader failure');};");
    check(parse(evaluate(page,readerScript())).state==State::Failed,"reader exception is not reported as successful saving");
    evaluate(page,"WizReader.closeDocument=()=>42;");
    check(parse(evaluate(page,readerScript())).state==State::Failed,"non-string reader payload is rejected");
    check(parse(QVariant()).state==State::Failed&&parse(true).state==State::Failed,
        "invalid WebEngine callback and legacy boolean cannot masquerade as a snapshot");
    check(parse(QVariantMap{{"ok","true"},{"changed",false}}).state==State::Failed
        &&parse(QVariantMap{{"ok",true},{"changed",true},{"html",42}}).state==State::Failed,
        "malformed snapshot fields are strictly rejected");

    WizDocumentCallbackEpoch epoch;
    auto old=epoch.ticket();check(WizDocumentCallbackEpoch::current(old),"current page accepts its callback ticket");
    epoch.invalidate();check(!WizDocumentCallbackEpoch::current(old),"navigation invalidates a pending old-page callback");
    auto fresh=epoch.ticket();check(WizDocumentCallbackEpoch::current(fresh),"new page gets an independent valid ticket");
    epoch.cancel();check(!WizDocumentCallbackEpoch::current(fresh),"owner shutdown cancels callbacks before child page destruction");
    auto owner=std::make_unique<WizDocumentCallbackEpoch>();auto closed=owner->ticket();owner.reset();
    check(!WizDocumentCallbackEpoch::current(closed),"callbacks do not retain a destroyed document owner");
    {
        struct Delivery { int called=0;int accepted=0; };
        auto delivery=std::make_shared<Delivery>();
        auto dyingPage=std::make_unique<QWebEnginePage>(&profile);
        WizDocumentCallbackEpoch dyingOwner;
        const auto ticket=dyingOwner.ticket();
        dyingPage->runJavaScript("1",[delivery,ticket](const QVariant&){
            ++delivery->called;if(WizDocumentCallbackEpoch::current(ticket))++delivery->accepted;
        });
        dyingOwner.cancel();dyingPage.reset();app.processEvents();
        check(delivery->called==1&&delivery->accepted==0,
            "actual page-destruction callback completes once and cannot enter a canceled owner");
    }

    const auto target=root.filePath(QStringLiteral("临时 笔记.html"));
    check(writeHtmlFile(target,changed.html),"synthetic HTML is committed to a Unicode path");
    QFile file(target);const bool opened=file.open(QIODevice::ReadOnly);const auto bytes=file.readAll();file.close();
    check(opened&&bytes==QByteArray::fromHex("efbbbf")+changed.html.toUtf8(),"HTML retains the original UTF-8 BOM and exact contents");
    check(!writeHtmlFile(target,{}),"empty HTML cannot overwrite a saved extraction");
    const bool reopened=file.open(QIODevice::ReadOnly);check(reopened&&file.readAll()==bytes,"failed save preserves the previous extraction");file.close();
    check(!writeHtmlFile(root.path(),changed.html)&&!writeHtmlFile(root.filePath("missing/note.html"),changed.html),
        "directory target and missing parent fail without direct-write fallback");
    {
        const auto waitFor=[&](const std::function<bool()>& predicate,int timeout=1500){
            QEventLoop loop;QTimer poll,deadline;
            QObject::connect(&poll,&QTimer::timeout,[&]{if(predicate())loop.quit();});
            QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
            poll.start(5);deadline.setSingleShot(true);deadline.start(timeout);loop.exec();
            return predicate();
        };
        struct Completion { int count=0;bool ready=false; };
        auto completion=std::make_shared<Completion>();
        QObject owner;
        // Release a real JS Promise from a GUI timer, without relying on
        // background-page JavaScript timer scheduling in an invisible fixture.
        page.runJavaScript(WizDocumentPageReady::setupScript("'synthetic-token'",
            "await new Promise(resolve=>window.completeSetup=resolve);"));
        QTimer::singleShot(150,&page,[&page]{page.runJavaScript("window.completeSetup()");});
        WizDocumentPageReady::watch(&owner,&page,WizDocumentPageReady::stateExpression("'synthetic-token'"),[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        check(completion->count==0,"asynchronous editor setup does not notify plugins before readiness");
        check(waitFor([completion]{return completion->count>0;})&&completion->ready&&completion->count==1,
            "actual delayed JS setup completes exactly once after readiness");
        page.runJavaScript(WizDocumentPageReady::setupScript("'failed-token'","throw Error('synthetic setup error');"));
        completion=std::make_shared<Completion>();
        WizDocumentPageReady::watch(&owner,&page,WizDocumentPageReady::stateExpression("'failed-token'"),[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready&&completion->count==1,
            "explicit editor initialization failure reports failure once");
        completion=std::make_shared<Completion>();
        WizDocumentPageReady::watch(&owner,&page,"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready,
            "unresolved initialization has a bounded timeout");
        completion=std::make_shared<Completion>();
        WizDocumentCallbackEpoch navigation;const auto ticket=navigation.ticket();
        WizDocumentPageReady::watch(&owner,&page,"'pending'",[ticket]{return WizDocumentCallbackEpoch::current(ticket);},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        navigation.invalidate();
        waitFor([]{return false;},200);
        check(completion->count==0,"navigation cancellation suppresses old-page readiness notifications");
        completion=std::make_shared<Completion>();
        auto closingOwner=std::make_unique<QObject>();
        WizDocumentPageReady::watch(closingOwner.get(),&page,"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        const QPointer<QTimer> pendingTimer=closingOwner->findChild<QTimer*>();closingOwner.reset();
        waitFor([]{return false;},150);
        check(pendingTimer.isNull()&&completion->count==0,"owner destruction releases the setup timer without late notification");
        completion=std::make_shared<Completion>();
        auto closingPage=std::make_unique<QWebEnginePage>(&profile);
        WizDocumentPageReady::watch(&owner,closingPage.get(),"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        closingPage.reset();
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready&&completion->count==1,
            "destroyed renderer page ends the readiness wait without calling its methods");
    }
    qInfo()<<"Document async checks:"<<count<<"failures:"<<failures;
    return failures?1:0;
}
