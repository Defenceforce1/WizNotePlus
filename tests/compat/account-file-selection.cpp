#include "share/WizWebEngineView.h"
#include "share/WizAccountFileChooser.h"
#include "share/WizAccountWebContent.h"
#include <QApplication>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <functional>
#include <memory>

namespace {
bool until(const std::function<bool()>& predicate, int ms = 5000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < ms) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}
QVariant js(QWebEnginePage* page, const QString& code)
{
    struct Reply { bool done = false; QVariant value; };
    const auto reply = std::make_shared<Reply>();
    page->runJavaScript(code, [reply](const QVariant& value) { reply->value = value; reply->done = true; });
    until([&] { return reply->done; });
    return reply->value;
}
QFileDialog* picker()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "wiz-account-file-picker") return qobject_cast<QFileDialog*>(widget);
    return nullptr;
}
class NoNetwork final : public QWebEngineUrlRequestInterceptor {
public:
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme = info.requestUrl().scheme();
        if (scheme != "about" && scheme != "data") info.block(true);
    }
};
class ChooserDriver {
public:
    QTimer timer;
    bool observed = false, widgetDialog = false;
    bool completed=false,selectionConfirmed=false;
    int selectionRetries=0;
    QPointer<QFileDialog> active;
    bool acceptQueued=false,resetInjected=false;
    static bool selectionReady(QFileDialog* dialog,const QString& path){
        if(!dialog||!dialog->isVisible())return false;
        const auto files=dialog->selectedFiles();
        auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit");
        auto* buttons=dialog->findChild<QDialogButtonBox*>();
        auto* open=buttons?buttons->button(QDialogButtonBox::Open):nullptr;
        return files==QStringList{path}&&QFileInfo(path).isFile()&&edit
            &&edit->text()==QFileInfo(path).fileName()&&open&&open->isEnabled();
    }
    explicit ChooserDriver(const QString& path, bool cancel = false,bool resetBeforeAccept=false) {
        timer.setInterval(5);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this,path,cancel,resetBeforeAccept] {
            auto* dialog = picker();
            if (!dialog||!dialog->isVisible()) return;
            observed = true;
            widgetDialog = dialog->testOption(QFileDialog::DontUseNativeDialog);
            if(active!=dialog){
                active=dialog;
                QObject::connect(dialog,&QDialog::finished,&timer,[this,path](int result){
                    completed=true;selectionConfirmed=result==QDialog::Accepted&&active&&active->selectedFiles()==QStringList{path};timer.stop();
                });
            }
            if (cancel) { dialog->reject(); return; }
            if(acceptQueued)return;
            if(!selectionReady(dialog,path)){
                ++selectionRetries;
                dialog->setDirectory(QFileInfo(path).absolutePath());
                dialog->selectFile(QFileInfo(path).fileName());
                // The model may still hold the same row after its filename
                // editor was reset; reselecting that row emits no text change.
                if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(path).fileName());
                return;
            }
            if(resetBeforeAccept&&!resetInjected){
                resetInjected=true;dialog->findChild<QLineEdit*>("fileNameEdit")->clear();return;
            }
            acceptQueued=true;
            QTimer::singleShot(0,&timer,[this,path]{
                acceptQueued=false;
                if(selectionReady(active,path))QMetaObject::invokeMethod(active,"accept");
            });
        });
        timer.start();
        QTimer::singleShot(4000, &timer, [] { if (auto* dialog = picker()) dialog->reject(); });
    }
};
}

int wizAccountFileSelectionSelfTest(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    const QString imagePath = root.filePath(QStringLiteral("合成 头像 #%.png"));
    QImage image(32, 16, QImage::Format_RGB32); image.fill(Qt::red);
    if (!check(image.save(imagePath), "synthetic avatar image is created only under the disposable root")) return 1;
    QFile input(imagePath);
    if (!check(input.open(QIODevice::ReadOnly), "synthetic image opens for an exact-byte baseline")) return 1;
    const auto original = input.readAll(); input.close();
    {
        QObject owner; ChooserDriver driver(imagePath,false,true);
        const auto files = wizAccountUploadFiles(&owner, nullptr, false, {"image/*"});
        check(driver.observed && driver.widgetDialog, "account compatibility chooser uses the explicit desktop widget dialog");
        check(files == QStringList{imagePath}, "confirmed Unicode path is returned as a local path, not an encoded URL");
        check(driver.resetInjected&&driver.selectionRetries>=2&&driver.selectionConfirmed,
              "chooser driver defers acceptance after a synthetic filename reset until the exact readable selection is restored");
    }
    {
        QObject owner; ChooserDriver driver(imagePath, true);
        check(wizAccountUploadFiles(&owner, nullptr, false, {"image/png"}).isEmpty() && driver.observed,
              "cancel returns no selection instead of a stale previous avatar");
    }
    {
        auto* owner = new QObject;
        QTimer::singleShot(30, &app, [owner] { delete owner; });
        check(wizAccountUploadFiles(owner, nullptr, false, {".png"}).isEmpty(),
              "destroying the request owner dismisses its chooser without a selected file");
    }
    {
        QObject owner; auto* parent = new QWidget;
        QTimer::singleShot(30, &app, [parent] { delete parent; });
        check(wizAccountUploadFiles(&owner, parent, false, {"image/*"}).isEmpty(),
              "destroying the parent while choosing leaves no dangling file dialog");
    }
    NoNetwork interceptor;
    {
        QWebEngineProfile accountProfile;
        QWebEngineProfile otherProfile;
        QWebEnginePage accountPage(&accountProfile);
        QWebEnginePage otherPage(&otherProfile);
        auto* shared = QWebEngineProfile::defaultProfile();
        const auto sharedType = shared->httpCacheType();
        const auto otherType = otherProfile.httpCacheType();
        const auto storage = accountProfile.persistentStoragePath();
        const auto cookiePolicy = accountProfile.persistentCookiesPolicy();
        QWebEnginePage sharedPage(shared);
        check(!wizUseFreshAccountContent(nullptr), "fresh account policy refuses a missing page");
        check(!wizUseFreshAccountContent(&sharedPage) && shared->httpCacheType() == sharedType,
              "fresh account policy refuses to change the global default profile");
        check(wizUseFreshAccountContent(&accountPage)
              && accountProfile.httpCacheType() == QWebEngineProfile::NoCache,
              "dialog-owned account profile bypasses stale HTTP image cache");
        check(otherProfile.httpCacheType() == otherType && otherPage.profile() == &otherProfile,
              "fresh account policy leaves other pages and their cache policy unchanged");
        check(accountProfile.persistentStoragePath() == storage
              && accountProfile.persistentCookiesPolicy() == cookiePolicy,
              "fresh account policy does not clear cookies or move persistent storage");
        check(wizUseFreshAccountContent(&accountPage), "fresh account policy is idempotent");
    }
    QWebEngineProfile profile;
    profile.setUrlRequestInterceptor(&interceptor);
    for (const bool cancel : {false, true}) {
        QWebEngineView view;
        auto* page = new WizWebEnginePage(&profile, &view);
        page->setAccountFileChooserEnabled(true);
        view.setPage(page);
        view.resize(500, 180); view.show();
        bool loaded = false;
        QObject::connect(page, &QWebEnginePage::loadFinished, &view, [&](bool ok) { loaded = ok; });
        page->setHtml(QStringLiteral(R"HTML(<html><body><input id="f" type="file" accept="image/*" style="width:300px;height:50px"><script>
            window.selected=null;
            document.getElementById('f').onchange=function(){
                const f=this.files[0]; if(!f)return;
                const r=new FileReader(); r.onload=()=>{window.selected={name:f.name,size:f.size,type:f.type,data:r.result};};
                r.readAsDataURL(f);
            };
            </script></body></html>)HTML"), QUrl("about:blank"));
        if (!check(until([&] { return loaded; }), "actual account page class loads an isolated file-input fixture offline")) continue;
        ChooserDriver driver(imagePath, cancel);
        // Send ordinary pointer events to the isolated rendering widget, not
        // OS automation and not a file injected into the DOM.
        if (auto* target = view.focusProxy()) {
            const QPointF local(35, 25), global(target->mapToGlobal(QPoint(35,25)));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press); QApplication::sendEvent(target, &release);
        }
        // Observation of a newly opened modal is not successful selection.
        // Let it finish before polling the page/FileReader; no fixed30ms guess.
        const bool chosen = until([&] { return driver.completed; });
        check(chosen, "real Chromium file-input request reaches the account chooser override");
        if (!chosen) continue;
        if (cancel) {
            check(until([&] { return picker() == nullptr; })
                  && js(page, "document.getElementById('f').files.length").toInt() == 0,
                  "cancel through real Chromium leaves the file input unselected");
        } else {
            check(driver.selectionConfirmed,"Chromium fixture confirms its exact local selection before checking DOM delivery");
            QJsonObject result;
            const bool received = until([&] {
                result = QJsonDocument::fromJson(js(page,"JSON.stringify(window.selected)").toString().toUtf8()).object();
                return !result.isEmpty();
            });
            check(received && result.value("name").toString() == QFileInfo(imagePath).fileName()
                  && result.value("size").toInt() == original.size() && result.value("type") == "image/png",
                  "confirmed selection reaches the actual DOM file input with exact name size and MIME type");
            const auto bytes = QByteArray::fromBase64(result.value("data").toString().section(',',1).toLatin1());
            if(!received)qInfo()<<"Synthetic input diagnostic"<<js(page,"JSON.stringify({selected:!!window.selected,files:document.getElementById('f').files.length})");
            check(bytes == original && QImage::fromData(bytes).size() == image.size(),
                  "actual page FileReader receives the exact synthetic PNG bytes without uploading them");
        }
    }
    QFile unchanged(imagePath);
    if (!check(unchanged.open(QIODevice::ReadOnly), "selected source image still opens after page selection and cancellation")) return 1;
    check(unchanged.readAll() == original, "choosing and canceling never modify the source image");
    check(picker() == nullptr, "all owned compatibility choosers close before test exit");
    qInfo().noquote() << "account-file-selection checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
