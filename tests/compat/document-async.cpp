#include "share/WizDocumentAsync.h"
#include "share/WizEditorFontQuery.h"
#include "share/WizDocumentPageReady.h"
#include "share/WizDocumentLinkRequest.h"
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
#include <limits>

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
    {
        // Exercise the exact production helper with real WebEngine callbacks,
        // not a separate model of its cancellation/serialization logic.
        QObject owner;
        WizDocumentCallbackEpoch document;
        WizDocumentLinkRequest request;
        int delivered = 0;
        QString link;
        evaluate(page, "window.linkCalls=[];window.linkRemoved=0;window.WizEditor={link:{"
            "getCurrentLink:()=>'',setCurrentLink:url=>linkCalls.push(url),removeSelectedLink:()=>++linkRemoved}};");
        const auto barrier = [&] { evaluate(page, "true"); };
        request.query(&owner, &page, document.ticket(), [&](const QString& url) { ++delivered; link=url; });
        barrier();
        check(delivered==1 && link.isEmpty() && request.current(&page),
            "actual link query accepts a valid empty string once without blocking in a dialog");
        auto command=request.takeCommand(&page,"example.test/path");
        evaluate(page,command);
        check(evaluate(page,"linkCalls[0]").toString()=="http://example.test/path",
            "link command preserves the existing default HTTP scheme");
        check(request.takeCommand(&page,"example.test/duplicate").isEmpty(),
            "accepted link request can be consumed only once");
        for (const auto& url : {QString(),QString("http://")}) {
            request.query(&owner,&page,document.ticket(),[](const QString&){});barrier();
            evaluate(page,request.takeCommand(&page,url));
        }
        check(evaluate(page,"linkRemoved").toInt()==2,
            "empty URL and the unchanged HTTP placeholder still remove the selected link");
        const QString unusual=QStringLiteral("https://example.test/中文?q='quoted'&double=\"x\"&slash=\\value");
        // The original form returns QUrl::toEncoded(); preserve that interface.
        const QString formValue=QString::fromUtf8(QUrl(unusual).toEncoded());
        const QString normalized=QUrl(formValue).toString();
        evaluate(page,"window.legacyLink='not-delivered';WizEditor.link.setCurrentLink=url=>legacyLink=url;");
        evaluate(page,QString("WizEditor.link.setCurrentLink('%1');").arg(normalized));
        check(evaluate(page,"legacyLink").toString()!=normalized,
            "old single-quoted link construction reproduces quote-bearing URL transport failure");
        request.query(&owner,&page,document.ticket(),[](const QString&){});barrier();
        evaluate(page,request.takeCommand(&page,formValue));
        check(evaluate(page,"legacyLink").toString()==normalized,
            "JSON link argument preserves normalized Unicode quotes and backslashes exactly");
        delivered=0;
        request.query(&owner,&page,document.ticket(),[&](const QString&){++delivered;});
        request.cancel();barrier();
        check(delivered==0&&!request.current(&page),"canceled pending link query cannot reopen a dialog");
        request.query(&owner,&page,document.ticket(),[&](const QString&){++delivered;});
        const QString premature=request.takeCommand(&page,"example.test/premature");barrier();
        check(premature.isEmpty()&&delivered==0,
            "link cannot be accepted before its pending lookup returns a valid string");
        int oldCalls=0,newCalls=0;
        request.query(&owner,&page,document.ticket(),[&](const QString&){++oldCalls;});
        request.query(&owner,&page,document.ticket(),[&](const QString&){++newCalls;});barrier();
        check(oldCalls==0&&newCalls==1,"only the latest pending link query may complete");
        request.query(&owner,&page,document.ticket(),[&](const QString&){++delivered;});
        document.invalidate();barrier();
        check(delivered==0&&request.takeCommand(&page,"example.test/stale").isEmpty(),
            "navigation invalidates both pending link lookup and later dialog acceptance");
        request.query(&owner,&page,document.ticket(),[](const QString&){});barrier();
        document.invalidate();
        check(request.takeCommand(&page,"example.test/old-dialog").isEmpty(),
            "already opened link dialog cannot submit into a replacement document");
        auto closingOwner=std::make_unique<QObject>();
        request.query(closingOwner.get(),&page,document.ticket(),[&](const QString&){++delivered;});
        closingOwner.reset();barrier();
        check(delivered==0&&!request.current(&page),"destroyed link owner suppresses actual JS callback delivery");
        auto closingPage=std::make_unique<QWebEnginePage>(&profile);
        request.query(&owner,closingPage.get(),document.ticket(),[&](const QString&){++delivered;});
        closingPage.reset();app.processEvents();
        check(delivered==0,"page destruction cannot turn an invalid callback into an empty-link dialog");
        auto closingRequest=std::make_unique<WizDocumentLinkRequest>();
        closingRequest->query(&owner,&page,document.ticket(),[&](const QString&){++delivered;});
        closingRequest.reset();barrier();
        check(delivered==0,"destroyed link request is not retained by a pending WebEngine callback");
        QWebEnginePage other(&profile);
        request.query(&owner,&page,document.ticket(),[](const QString&){});barrier();
        check(request.takeCommand(&other,"example.test/wrong-page").isEmpty(),
            "link request refuses a different renderer page");
        evaluate(page,"WizEditor.link.getCurrentLink=()=>undefined;");
        request.query(&owner,&page,document.ticket(),[&](const QString&){++delivered;});barrier();
        check(delivered==0&&request.takeCommand(&page,"example.test/undefined").isEmpty(),
            "undefined link result cannot open a dialog or authorize a later submission");
        evaluate(page,"delete window.WizEditor;delete window.linkCalls;delete window.linkRemoved;delete window.legacyLink;");
    }
    {
        const QString alias=QStringLiteral("\";window.initializerInjected=true;//\n中文\\alias");
        const QString path=QStringLiteral("/synthetic/quoted \"path\"/back\\slash/");
        evaluate(page,"window.WizEditorInit=function(...args){window.encodedInitializerArgs=args;return Promise.resolve(true);};");
        evaluate(page,WizDocumentPageReady::setupScript("'encoded-arguments'",
            WizDocumentPageReady::initializeEditorScript(path,"zh-cn","synthetic-guid",alias,true,"markdown")));
        const auto args=evaluate(page,"encodedInitializerArgs").toList();
        check(args.size()==7&&args[0].toString()==path&&args[3].toString()==alias
            &&args[4].userType()==QMetaType::Bool&&args[4].toBool()&&!args[6].toBool(),
            "native initializer JSON arguments preserve quotes backslashes Unicode newlines and booleans exactly");
        check(evaluate(page,"WizNativeEditorSetup.state==='ready'&&!window.initializerInjected").toBool(),
            "account display data cannot become executable initializer JavaScript");
        evaluate(page,"delete window.WizEditorInit;delete window.WizNativeEditorSetup;");
    }
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
        const QString setup = QStringLiteral(R"JS(
            window.fontSelection=1;window.changeFontSelection=true;window.fontQueries=[];
            document.queryCommandValue=function(name){
                var selection=fontSelection;fontQueries.push('value:'+name+':'+selection);
                if(name==='fontFamily'&&changeFontSelection){changeFontSelection=false;Promise.resolve().then(()=>fontSelection=2);}
                return name==='fontFamily'?(selection===1?'Fixture A':'Fixture B'):(selection===1?'11pt':'22pt');
            };
            document.queryCommandState=function(name){fontQueries.push('state:'+name+':'+fontSelection);
                return fontSelection===1?(name==='bold'||name==='underline'):(name==='italic'||name==='strikethrough');};
        )JS");
        evaluate(page, setup);
        const QString oldFamily=evaluate(page,"document.queryCommandValue('fontFamily')").toString();
        const int oldBold=evaluate(page,"document.queryCommandState('bold')").toInt();
        evaluate(page,"document.queryCommandState('italic')");
        evaluate(page,"document.queryCommandState('underline')");
        const QString oldSize=evaluate(page,"document.queryCommandValue('fontSize')").toString();
        evaluate(page,"document.queryCommandState('strikethrough')");
        check(oldFamily=="Fixture A"&&oldSize=="22pt"&&oldBold==0,
              "six legacy JS round trips reproduce mixed selection state across a real renderer microtask");
        evaluate(page, setup);
        const auto atomic=evaluate(page,WizEditorFontQuery::script()).toMap();
        const auto atomicFont=WizEditorFontQuery::font(atomic,13);
        check(atomicFont&&atomicFont->family()=="Fixture A"&&atomicFont->pointSize()==11
              &&atomicFont->bold()&&!atomicFont->italic()&&atomicFont->underline()&&!atomicFont->strikeOut(),
              "single JS font query keeps all six legacy values from the same selection task");
        check(evaluate(page,"fontSelection===2&&fontQueries.length===6").toBool(),
              "batched font query still permits the pending selection microtask after the read");
        auto fallback=atomic;fallback["size"]="";
        const auto fallbackFont=WizEditorFontQuery::font(fallback,13);
        check(fallbackFont&&fallbackFont->pointSize()==13,
              "missing browser font size retains the configured point-size fallback");
        fallback["size"]="-5pt";
        const auto boundedFont=WizEditorFontQuery::font(fallback,0);
        check(boundedFont&&boundedFont->pointSize()>0,
              "invalid nonpositive font sizes cannot reach QFont as negative values");
        auto malformed=atomic;malformed.remove("family");
        check(!WizEditorFontQuery::font(malformed,13)&&!WizEditorFontQuery::font({},13),
              "incomplete or failed font replies cannot authorize a font operation");
        auto numeric=atomic;numeric["bold"]=2;numeric["italic"]=1;numeric["underline"]=0;numeric["strike"]=1;
        const auto numericFont=WizEditorFontQuery::font(numeric,13);
        check(numericFont&&!numericFont->bold()&&numericFont->italic()&&!numericFont->underline()&&numericFont->strikeOut(),
              "numeric browser flags retain the existing exact-one style interpretation");
        numeric["bold"]=std::numeric_limits<double>::infinity();
        check(!WizEditorFontQuery::font(numeric,13),"nonfinite browser font flags cannot become an actionable font state");
        QObject queryOwner;
        WizDocumentCallbackEpoch queryDocument;
        int received=0;
        evaluate(page,setup);
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap& state){if(WizEditorFontQuery::valid(state))++received;});
        evaluate(page,"true");
        check(received==1,"actual asynchronous font query reaches a live owner and current document once");
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap&){++received;});
        queryDocument.invalidate();evaluate(page,"true");
        check(received==1,"navigation cancels only the old transient font result");
        auto closing=std::make_unique<QObject>();
        WizEditorFontQuery::query(closing.get(),&page,queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap&){++received;});
        closing.reset();evaluate(page,"true");
        check(received==1,"font result cannot enter a destroyed toolbar owner");
        bool currentDelegate=true;
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[&]{return currentDelegate;},
            [&](const QVariantMap&){++received;});
        currentDelegate=false;evaluate(page,"true");
        check(received==1,"changed toolbar delegate rejects an otherwise live font result");
        int latestRequest=1, newestReplies=0;
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[&]{return latestRequest==1;},
            [&](const QVariantMap&){++newestReplies;});
        latestRequest=2;
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[&]{return latestRequest==2;},
            [&](const QVariantMap&){++newestReplies;});
        evaluate(page,"true");
        check(newestReplies==1,"only the newest same-document font request can deliver its result");
        auto closingPage=std::make_unique<QWebEnginePage>(&profile);
        WizEditorFontQuery::query(&queryOwner,closingPage.get(),queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap&){++received;});
        closingPage.reset();app.processEvents();
        check(received==1,"page destruction cannot deliver a usable font state from its invalid callback");
        evaluate(page,"document.queryCommandState=function(){throw Error('synthetic query failure');}");
        WizEditorFontQuery::query(&queryOwner,&page,queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap&){++received;});
        evaluate(page,"true");
        check(received==1,"renderer query exception cannot open or apply a font result");
        evaluate(page,setup);
        auto selfDeletingOwner=std::make_unique<QObject>();
        WizEditorFontQuery::query(selfDeletingOwner.get(),&page,queryDocument.ticket(),[]{return true;},
            [&](const QVariantMap&){++received;selfDeletingOwner.reset();});
        evaluate(page,"true");
        check(received==2&&!selfDeletingOwner,"font completion may close its owner without a later owner access");
        evaluate(page,"delete document.queryCommandValue;delete document.queryCommandState;");
    }
    {
        int calls = 0;
        auto actionOwner = std::make_unique<QObject>();
        auto action = WizDocumentOwnerAction::guarded(actionOwner.get(), [&](QObject& receiver) {
            if (&receiver == actionOwner.get()) ++calls;
        });
        action();
        check(calls == 1, "guarded deferred action still invokes its live document owner");
        actionOwner.reset();
        action();
        check(calls == 1, "guarded deferred action cannot call a destroyed document owner");
        auto nullAction = WizDocumentOwnerAction::guarded(static_cast<QObject*>(nullptr), [&](QObject&) { ++calls; });
        nullAction();
        check(calls == 1, "empty document owner is safely ignored without invoking the action");
        auto queuedOwner = std::make_unique<QObject>();
        int queuedCalls = 0;
        QMetaObject::invokeMethod(&app, WizDocumentOwnerAction::guarded(queuedOwner.get(), [&](QObject&) { ++queuedCalls; }), Qt::QueuedConnection);
        queuedOwner.reset(); app.processEvents();
        check(queuedCalls == 0, "actual queued GUI delivery drops an action after its document owner closes");
        auto selfDeletingOwner = std::make_unique<QObject>();
        auto deleting = WizDocumentOwnerAction::guarded(selfDeletingOwner.get(), [&](QObject&) {
            ++calls; selfDeletingOwner.reset();
        });
        deleting(); deleting();
        check(calls == 2 && !selfDeletingOwner, "guarded action may delete its owner without a later dereference or repeat");
    }
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
