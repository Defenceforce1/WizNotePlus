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
    check(styleSheet(custom) == custom.screen && !styleSheet(custom).contains("@media"), "one common stylesheet is not restricted to screen media");
    check(!styleSheet(loadStyle(&settings)).contains("retired-print-sentinel"), "retired print CSS is ignored");
    WizWebEngineProfile()->setUrlRequestInterceptor(nullptr);
    qInfo() << "Presentation failures:" << failures;
    return failures ? 1 : 0;
}
