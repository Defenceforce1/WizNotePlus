#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <functional>
#include <memory>
#include "WizDocumentAsync.h"
#include "share/WizMarkdownExportTransaction.h"
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
#include "widgets/WizMarkdownExportRecoveryApp.h"
#include "widgets/WizMarkdownExportRecoverySession.h"
#include "widgets/WizMarkdownExportRecoveryPrompt.h"
#include "share/WizQueuedThreadsSession.h"
#include <QWidget>
// Result presentation only is a collaborator; the source flow, actual App
// adapter, session, queues, backend and filesystem transaction are compiled.
struct WizMessageBox { static void information(QWidget*, const QString&, const QString&) {} };
#endif

// Only the database collaborator is modeled. It writes synthetic HTML before
// reporting success/failure, matching the ordering under investigation. No SQL,
// archives, Wiz profile factory, account, or main-window code is linked.
struct WIZDOCUMENTDATA {
    QString strKbGUID = QStringLiteral("synthetic-only");
    QString strGUID = QStringLiteral("synthetic-document");
};
bool rawWriteUtf8UnderTest(const QString&, const QString&, bool);
static int markdownWrites = 0;
bool WizPathFileExists(const QString& path) { return QFile::exists(path); }
bool WizDeleteFile(const QString& path) { return QFile::remove(path); }
void WizSetUtf8Encoding(QTextStream& stream) { stream.setEncoding(QStringConverter::Utf8); }
bool WizSaveUnicodeTextToUtf8File(const QString& path, const QString& text, bool bom)
{
    ++markdownWrites;
    return rawWriteUtf8UnderTest(path, text, bom);
}
namespace Utils {
struct WizMisc {
    static QString extractFileName(const QString&);
    static QString extractFileTitle(const QString&);
};
}
struct WizDatabase {
    bool failAfterHtml = false;
    int calls = 0;
    bool exportToHtmlFile(const WIZDOCUMENTDATA&, const QString& path)
    {
        ++calls;
        const bool written = rawWriteUtf8UnderTest(path, QStringLiteral("<html>synthetic snapshot</html>"), true);
        return written && !failAfterHtml;
    }
};
struct DatabaseManager {
    WizDatabase database;
    WizDatabase& db(const QString&) { return database; }
};
struct DocumentView {
    WIZDOCUMENTDATA document;
    const WIZDOCUMENTDATA& note() const { return document; }
};
struct ExportHarness :
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    QWidget
#else
    QObject
#endif
{
    explicit ExportHarness(QWebEnginePage* page) : browser(page) {}
    ~ExportHarness() override {
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
        cancelMarkdownRecovery();
#endif
        m_documentCallbacks.cancel();
    }
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    QPointer<WizMarkdownExportRecoverySession> m_markdownRecoverySession;
    void cancelMarkdownRecovery() {
        const auto session = m_markdownRecoverySession;
        m_markdownRecoverySession = nullptr;
        if (session) session->invalidate();
    }
#endif
    QWebEnginePage* browser;
    DocumentView document;
    DatabaseManager m_dbMgr;
    WizDocumentCallbackEpoch m_documentCallbacks;
    DocumentView* view() { return &document; }
    QWebEnginePage* page() { return browser; }
    bool acceptsDocumentCallback(const WizDocumentCallbackEpoch::Ticket& ticket, const WIZDOCUMENTDATA& doc) const
    {
        return WizDocumentCallbackEpoch::current(ticket) && document.note().strGUID == doc.strGUID
            && document.note().strKbGUID == doc.strKbGUID;
    }
    void saveAsMarkdown(QString&, bool = true);
};
#ifdef EXPORT_CANDIDATE
#include "candidate-flow.inc"
#elif defined(EXPORT_LEGACY)
#include "legacy-flow.inc"
#else
#include "actual-flow.inc"
#endif

class LocalOnly final : public QWebEngineUrlRequestInterceptor {
public:
    void interceptRequest(QWebEngineUrlRequestInfo& info) override
    {
        const QString scheme = info.requestUrl().scheme();
        if (scheme != "data" && scheme != "about") info.block(true);
    }
};
static bool waitUntil(const std::function<bool()>& ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return ready();
}
static QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
static bool discoveryBarrier(ExportHarness& harness)
{
    auto session = harness.m_markdownRecoverySession;
    if (!session) return false;
    bool observed = false;
    const auto connection = QObject::connect(session, &WizMarkdownExportRecoverySession::discovered,
        &harness, [&](const auto&, const auto&) { observed = true; });
    const bool passed = waitUntil([&] { return observed; });
    QObject::disconnect(connection);
    return passed;
}
#endif
static bool evaluate(QWebEnginePage& page, const QString& code, QVariant* value = nullptr)
{
    struct Reply { bool done = false; QVariant value; };
    const auto reply = std::make_shared<Reply>();
    page.runJavaScript(code, [reply](const QVariant& value) { reply->value = value; reply->done = true; });
    if (!waitUntil([&] { return reply->done; })) return false;
    if (value) *value = reply->value;
    return true;
}
int main(int argc, char** argv)
{
    // Resource errors must fail BEFORE WebEngine starts (no relative paths).
    const QString sdk = QFileInfo(QString::fromLocal8Bit(qgetenv("WIZNOTE_TEST_QT_SDK"))).canonicalFilePath();
    if (sdk.isEmpty()) { fprintf(stderr, "Explicit readable WIZNOTE_TEST_QT_SDK required\n"); return 2; }
    const QString core = sdk + "/lib/QtWebEngineCore.framework/Versions/A/";
    const QString resources = core + "Resources";
    const QString helper = core + "Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess";
    for (const QString& path : {helper, resources + "/icudtl.dat", resources + "/qtwebengine_resources.pak",
                               resources + "/qtwebengine_locales/en-US.pak"}) {
        if (!QFileInfo(path).isReadable()) { fprintf(stderr, "Missing WebEngine resource\n"); return 2; }
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("XDG_CACHE_HOME", (temporary.path() + "/cache").toUtf8());
    qputenv("QTWEBENGINEPROCESS_PATH", helper.toUtf8());
    qputenv("QTWEBENGINE_RESOURCES_PATH", resources.toUtf8());
    qputenv("QTWEBENGINE_LOCALES_PATH", (resources + "/qtwebengine_locales").toUtf8());
    QApplication app(argc, argv);
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", temporary.filePath("audit").toUtf8());
    WizQueuedThreadsSession queues([] {});
#endif
    const bool requireSafe = app.arguments().contains("--require-safe");
    const bool requireReplySafe = requireSafe || app.arguments().contains("--require-reply-safe");
#if defined(EXPORT_CANDIDATE) || defined(EXPORT_ACTUAL_REPLY_FIRST)
    constexpr bool candidate = true;
#else
    constexpr bool candidate = false;
#endif
#ifdef EXPORT_PRODUCT_TRANSACTION
    constexpr bool productTransaction = true;
#else
    constexpr bool productTransaction = false;
#endif
    LocalOnly interceptor;
    QWebEngineProfile profile;
    profile.setUrlRequestInterceptor(&interceptor);
    QWebEnginePage page(&profile);
    bool loaded = false;
    QObject::connect(&page, &QWebEnginePage::loadFinished, &app, [&](bool ok) { loaded = ok; });
    page.setHtml(QStringLiteral("<!doctype html><meta charset='utf-8'><p>Synthetic Markdown export</p>"));
    if (!waitUntil([&] { return loaded; })) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const QString& name) {
        ++checks; if (!ok) ++failures;
        QTextStream(stdout) << (ok ? "PASS " : "FAIL ") << name << Qt::endl;
    };
    check(profile.isOffTheRecord(), "off-record synthetic profile");
    ExportHarness harness(&page);
    QString empty;
    harness.saveAsMarkdown(empty);
    check(harness.m_dbMgr.database.calls == 0 && markdownWrites == 0, "canceled file choice does no I/O");
    struct Case { const char* name; QString expression; QString expected; bool invalid; };
    const QString valid = QStringLiteral("# 定理 α\n\n![图](index_files/a.png)\n");
    const QString encoded = QString::fromUtf8(QJsonDocument(QJsonArray{valid}).toJson(QJsonDocument::Compact));
    const QList<Case> cases{
        {"unicode", encoded + "[0]", valid, false},
        {"empty", "''", "", false},
        {"undefined", "undefined", "", true},
        {"null", "null", "", true},
        {"number", "17", "17", true},
        {"throw", "(()=>{throw new Error('synthetic export error')})()", "", true}
    };
    QJsonArray observations;
    for (const Case& item : cases) {
        const QString setup = "window.exportCalls=0; window.WizEditor={getMarkdownSrc:function(o){"
                              "window.exportCalls++; return " + item.expression + ";}}; typeof WizEditor.getMarkdownSrc";
        QVariant ready;
        const bool setupDone = evaluate(page, setup, &ready);
        check(setupDone && ready.toString() == "function", QString(item.name) + " actual JS fixture ready");
        if (!setupDone || ready.toString() != "function") return 2;
        QString target = temporary.path() + "/A&B." + item.name + ".md";
        const QByteArray sentinel("EXISTING SYNTHETIC EXPORT\n");
        check(rawWriteUtf8UnderTest(target, QString::fromUtf8(sentinel), false), QString(item.name) + " sentinel created");
        const int before = markdownWrites;
        const int databaseBefore = harness.m_dbMgr.database.calls;
        harness.saveAsMarkdown(target);
        check(candidate ? readBytes(target) == sentinel : readBytes(target) != sentinel,
            QString(item.name) + (candidate ? " candidate leaves target untouched before reply" : " legacy overwrites before JS reply (risk reproduced)"));
        // A later JS barrier, then a GUI event turn, must finish even when the
        // candidate intentionally does not invoke the writer on invalid input.
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
        check(discoveryBarrier(harness), QString(item.name) + " actual asynchronous discovery delivered before JS barrier");
#endif
        check(evaluate(page, "'export barrier'"), QString(item.name) + " actual JS barrier completed");
        QCoreApplication::processEvents();
        check(markdownWrites == before + ((productTransaction || (candidate && item.invalid)) ? 0 : 1),
            QString(item.name) + " expected legacy-writer call count");
        check(harness.m_dbMgr.database.calls == databaseBefore + ((candidate && item.invalid) ? 0 : 1),
            QString(item.name) + " expected database export count");
        QVariant calls;
        check(evaluate(page, "window.exportCalls", &calls) && calls.toInt() == 1, QString(item.name) + " editor called exactly once");
        QString expected = item.expected;
        expected.replace("index_files/", Utils::WizMisc::extractFileTitle(target).toHtmlEscaped() + "_files/");
        const QByteArray result = readBytes(target);
        check(result == ((candidate && item.invalid) ? sentinel : expected.toUtf8()),
            QString(item.name) + " exact output bytes (valid empty allowed)");
        if (requireReplySafe && item.invalid) check(result == sentinel, QString(item.name) + " SAFETY: invalid reply preserves existing file");
        observations.append(QJsonObject{{"case", item.name}, {"invalidReply", item.invalid},
            {"existingPreserved", result == sentinel}, {"resultBytes", result.size()}});
    }
    check(evaluate(page, "window.WizEditor.getMarkdownSrc=function(){return 'valid synthetic markdown';};"),
          "valid source restored before database failure and lifetime controls");
    QString target = temporary.path() + "/failed-resource-copy.md";
    rawWriteUtf8UnderTest(target, QStringLiteral("EXISTING SYNTHETIC EXPORT\n"), false);
    harness.m_dbMgr.database.failAfterHtml = true;
    const int before = markdownWrites;
    harness.saveAsMarkdown(target);
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    check(discoveryBarrier(harness), "late database failure actual discovery delivered");
#endif
    check(evaluate(page, "'database failure barrier'"), "modeled database failure callback completed");
    QCoreApplication::processEvents();
    check(markdownWrites == before, "modeled DB failure performs no final Markdown write");
    check(productTransaction ? readBytes(target) == "EXISTING SYNTHETIC EXPORT\n"
                             : readBytes(target).contains("synthetic snapshot"),
        productTransaction ? "product transaction preserves Markdown after modeled late DB failure"
                           : "modeled late DB failure leaves HTML in Markdown target");
    if (requireSafe) check(readBytes(target) == "EXISTING SYNTHETIC EXPORT\n", "SAFETY: modeled DB failure preserves existing file");
#if defined(EXPORT_CANDIDATE) || defined(EXPORT_ACTUAL_REPLY_FIRST)
    harness.m_dbMgr.database.failAfterHtml = false;
    for (const QString& boundary : {QStringLiteral("destroy"), QStringLiteral("epoch"), QStringLiteral("document"), QStringLiteral("page")}) {
        auto owner = std::make_unique<ExportHarness>(&page);
        QString boundaryTarget = temporary.path() + "/boundary-" + boundary + ".md";
        rawWriteUtf8UnderTest(boundaryTarget, QStringLiteral("UNCHANGED BOUNDARY\n"), false);
        const int writesBefore = markdownWrites;
        owner->saveAsMarkdown(boundaryTarget);
        if (boundary == "destroy") owner.reset();
        else if (boundary == "epoch") owner->m_documentCallbacks.invalidate();
        else if (boundary == "document") owner->document.document.strGUID = "other-synthetic-document";
        else owner->browser = nullptr;
        check(evaluate(page, "'lifetime barrier'"), boundary + " actual JS barrier completed");
        QCoreApplication::processEvents();
        check(markdownWrites == writesBefore && readBytes(boundaryTarget) == "UNCHANGED BOUNDARY\n",
              boundary + " stale export leaves sentinel unchanged");
    }
#endif
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    {
        QString target = temporary.filePath("late-quit.md");
        check(rawWriteUtf8UnderTest(target, QStringLiteral("PRESERVE ON QUIT"), false), "late-quit synthetic export prepared");
        check(evaluate(page, "window.WizEditor={getMarkdownSrc:()=> 'LATE EXPORT'}"), "late-quit actual editor initialized");
        const int before = harness.m_dbMgr.database.calls;
        harness.saveAsMarkdown(target);
        auto* session = harness.m_markdownRecoverySession.data();
        check(session != nullptr, "actual extracted export entry creates experimental App session");
        std::unique_ptr<WizMarkdownExportRecoveryApp::QuitScope> quitting;
        QObject::connect(session, &WizMarkdownExportRecoverySession::discovered, &app,
            [&](const auto&, const auto&) { quitting = std::make_unique<WizMarkdownExportRecoveryApp::QuitScope>(); });
        check(waitUntil([&] { return bool(quitting); }), "quit starts after no-site continuation queued actual JavaScript query");
        check(evaluate(page, "'late-query-barrier'") && harness.m_dbMgr.database.calls == before
            && readBytes(target) == "PRESERVE ON QUIT", "late actual JavaScript reply cannot export after quit begins");
        quitting.reset();
        check(!WizMarkdownExportRecoveryApp::isQuitting(), "cancelled quit restores future explicit exports");
    }
    {
        QString target = temporary.filePath("interrupted.md");
        check(rawWriteUtf8UnderTest(target, QStringLiteral("PRESERVE RECOVERY"), false)
            && QDir().mkpath(WizMarkdownExportRecovery::recoveryDirectory(target)), "synthetic incomplete recovery site prepared");
        const int before = harness.m_dbMgr.database.calls;
        harness.saveAsMarkdown(target);
        auto session = harness.m_markdownRecoverySession;
        check(waitUntil([&] { return session && session->prompt(); }), "actual extracted entry presents recovery rather than ordinary overwrite");
        {
            WizMarkdownExportRecoveryApp::QuitScope quitting;
            check(evaluate(page, "'retain-barrier'") && harness.m_dbMgr.database.calls == before
                && readBytes(target) == "PRESERVE RECOVERY", "quit retains incomplete site and never resumes original export");
        }
    }
#endif
    QTextStream(stdout) << QJsonDocument(QJsonObject{
        {"diagnosticOnly", true}, {"actualClassOrDatabaseEndToEnd", false}, {"probeModifiesProduct", false},
        {"productTransaction", productTransaction},
        {"mode", EXPORT_MODE}, {"candidateSHA256", CANDIDATE_SOURCE_SHA},
        {"legacyControlSHA256", LEGACY_SOURCE_SHA},
        {"exportSourceSHA256", EXPORT_SOURCE_SHA}, {"writerSourceSHA256", WRITER_SOURCE_SHA},
        {"namesSourceSHA256", NAMES_SOURCE_SHA}, {"requireSafe", requireSafe}, {"requireReplySafe", requireReplySafe},
        {"checks", checks}, {"failures", failures}, {"observations", observations}
    }).toJson(QJsonDocument::Compact) << Qt::endl;
    return failures ? 1 : 0;
}
