// Synthetic, standalone Qt lifecycle reproducer. Never load an account or note.
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QPixmap>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSet>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#ifdef Q_OS_MAC
#include <mach/mach.h>
#endif

namespace {
const QString text = QStringLiteral("Synthetic standalone Qt text");
QString graphicsApi(QQuickWindow* window) {
    const auto api = window->rendererInterface()->graphicsApi();
    switch (api) {
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Software: return "Software";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    default: return QString::number(int(api));
    }
}
class LocalOnly final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& request) override {
        const auto scheme = request.requestUrl().scheme();
        if (scheme != "about" && scheme != "data" && scheme != "blob") {
            ++blocked;
            request.block(true);
        }
    }
};
QJsonObject memory() {
    // ps lists numeric metadata only; exclude its own observer process and all
    // unrelated applications. RSS sum is not unique physical memory.
    QProcess ps;
    ps.start("/bin/ps", {"-axo", "pid=,ppid=,rss="});
    const auto observer = ps.processId();
    if (!ps.waitForFinished(5000) || ps.exitCode()) return {{"available", false}};
    struct Row { qint64 pid, parent, rss; };
    QList<Row> rows;
    for (const auto& line : ps.readAllStandardOutput().split('\n')) {
        const auto fields = line.simplified().split(' ');
        if (fields.size() == 3)
            rows.append({fields[0].toLongLong(), fields[1].toLongLong(), fields[2].toLongLong()});
    }
    QSet<qint64> members{QCoreApplication::applicationPid()};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& row : rows)
            if (members.contains(row.parent) && !members.contains(row.pid)) {
                members.insert(row.pid);
                changed = true;
            }
    }
    qint64 rss = 0, mainRss = 0;
    int count = 0;
    for (const auto& row : rows) if (members.contains(row.pid) && row.pid != observer) {
        rss += row.rss;
        ++count;
        if (row.pid == QCoreApplication::applicationPid()) mainRss = row.rss;
    }
    QJsonObject result{{"available", true}, {"rssKiBSum", rss}, {"mainRssKiB", mainRss},
        {"processCount", count}, {"widgetCount", QApplication::allWidgets().size()},
        {"windowCount", QGuiApplication::allWindows().size()}};
#ifdef Q_OS_MAC
    task_vm_info_data_t info{};
    mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
            reinterpret_cast<task_info_t>(&info), &size) == KERN_SUCCESS) {
        result.insert("mainFootprintKiB", qint64(info.phys_footprint / 1024));
        result.insert("mainInternalKiB", qint64(info.internal / 1024));
        result.insert("mainCompressedKiB", qint64(info.compressed / 1024));
        result.insert("mainExternalKiB", qint64(info.external / 1024));
    }
#endif
    return result;
}

class Probe final : public QObject {
public:
    Probe(const QString& root, int batches) : batches(batches), profile("StandaloneSynthetic") {
        profile.setPersistentStoragePath(QDir(root).filePath("profile"));
        profile.setCachePath(QDir(root).filePath("cache"));
        profile.setUrlRequestInterceptor(&requests);
        auto* layout = new QGridLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        tabs = new QTabWidget(&host);
        layout->addWidget(tabs, 0, 0);
        host.resize(900, 600);
        host.setWindowTitle("Standalone Qt memory probe — synthetic text only");
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, this, [this] { fail("page load/check timeout"); });
    }
    void start() {
        host.show();
        retained = addPage();
        load(retained.get(), [this] { QTimer::singleShot(100, this, [this] { nextPage(); }); });
    }
private:
    LocalOnly requests;
    QWebEngineProfile profile;
    QWidget host;
    QTabWidget* tabs = nullptr;
    std::unique_ptr<QWebEngineView> retained;
    std::vector<std::unique_ptr<QWebEngineView>> transient;
    QList<QPointer<QWebEngineView>> deadViews;
    QList<QPointer<QWebEnginePage>> deadPages;
    QList<QPointer<QQuickWindow>> deadWindows;
    QTimer deadline;
    QElapsedTimer elapsed;
    QJsonArray samples;
    QString actualGraphicsApi;
    int batches, batch = 0, checks = 0;
    bool failed = false;
    const bool lifecycleStress = qEnvironmentVariableIsSet("WIZNOTE_QT_MEMORY_LIFECYCLE_STRESS");
    bool check(bool ok, const char* label) {
        ++checks;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) fail(label);
        return ok;
    }
    void fail(const char* reason) {
        if (failed) return;
        failed = true;
        deadline.stop();
        qCritical().noquote() << "FAIL standalone probe:" << reason;
        QCoreApplication::exit(2);
    }
    std::unique_ptr<QWebEngineView> addPage() {
        auto view = std::make_unique<QWebEngineView>(tabs);
        view->setPage(new QWebEnginePage(&profile, view.get()));
        tabs->addTab(view.get(), "Synthetic");
        tabs->setCurrentWidget(view.get());
        return view;
    }
    void verify(QWebEngineView* view, const std::function<void()>& done) {
        deadline.start(15000);
        const QPointer<QWebEngineView> guard(view);
        view->page()->runJavaScript(
            "document.body.textContent==='Synthetic standalone Qt text'"
            "&&document.scripts.length===0&&typeof WizEditor==='undefined'"
            "&&typeof MathJax==='undefined'&&document.visibilityState==='visible'",
            [this, guard, done](const QVariant& result) {
                deadline.stop();
                if (failed) return;
                if (!check(guard && guard->page()->webChannel() == nullptr && result.toBool(),
                        "visible plain Qt page has exact text, no scripts or channel")) return;
                if (actualGraphicsApi.isEmpty()) {
                    const auto widgets = guard->findChildren<QQuickWidget*>();
                    if (!check(widgets.size() == 1 && widgets[0]->quickWindow(),
                            "one actual Quick window supplies backend observation")) return;
                    actualGraphicsApi = graphicsApi(widgets[0]->quickWindow());
                }
                done();
            });
    }
    void load(QWebEngineView* view, const std::function<void()>& done) {
        deadline.start(15000);
        // One connection per view. No nested QEventLoop: each load, script
        // completion and settle returns to QApplication's ordinary event loop.
        connect(view, &QWebEngineView::loadFinished, this, [this, view, done](bool ok) {
            if (failed) return;
            if (!ok) { fail("local setHtml failed"); return; }
            verify(view, done);
        });
        view->setHtml("<!doctype html><meta charset='utf-8'><body>" + text + "</body>",
            QUrl("about:blank"));
    }
    void sample(const QString& phase) {
        auto record = memory();
        record.insert("batch", batch);
        record.insert("phase", phase);
        record.insert("profileDescendantObjects", profile.findChildren<QObject*>().size());
        if (phase == "four-open") record.insert("elapsedMs", elapsed.elapsed());
        samples.append(record);
        qInfo().noquote() << "MEMORY_SAMPLE"
            << QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact));
    }
    void nextPage() {
        if (failed) return;
        if (transient.empty()) elapsed.start();
        transient.push_back(addPage());
        load(transient.back().get(), [this] {
            if (transient.size() < 4) {
                QTimer::singleShot(0, this, [this] { nextPage(); });
            } else if (lifecycleStress) exercise(0);
            else QTimer::singleShot(250, this, [this] { closeBatch(); });
        });
    }
    void exercise(int step) {
        if (failed) return;
        if (step == 8) {
            host.hide();
            QTimer::singleShot(100, this, [this] {
                host.show();
                QTimer::singleShot(150, this, [this] { closeBatch(); });
            });
            return;
        }
        auto* view = transient[step % 4].get();
        tabs->setCurrentWidget(view);
        host.resize(step % 2 ? 900 : 1030, step % 2 ? 600 : 690);
        QTimer::singleShot(80, this, [this, view, step] {
            if (!check(host.isVisible() && view->isVisible() && !view->grab().isNull(),
                    "synthetic switch/resize has a visible capturable widget")) return;
            verify(view, [this, step] { exercise(step + 1); });
        });
    }
    void closeBatch() {
        sample("four-open");
        deadViews.clear(); deadPages.clear(); deadWindows.clear();
        for (const auto& view : transient) {
            deadViews.append(view.get());
            deadPages.append(view->page());
            for (auto* widget : view->findChildren<QQuickWidget*>())
                deadWindows.append(widget->quickWindow());
        }
        if (lifecycleStress) host.hide();
        transient.clear();
        tabs->setCurrentWidget(retained.get());
        if (lifecycleStress) host.show();
        QTimer::singleShot(1000, this, [this] {
            bool gone = deadViews.size() == 4 && deadPages.size() == 4 && deadWindows.size() == 4;
            for (const auto& view : deadViews) gone &= view.isNull();
            for (const auto& page : deadPages) gone &= page.isNull();
            for (const auto& window : deadWindows) gone &= window.isNull();
            if (!check(gone && tabs->count() == 1 && tabs->currentWidget() == retained.get(),
                    "four views, pages and Quick windows released; retained tab selected")) return;
            verify(retained.get(), [this] {
                sample("all-closed");
                if (++batch < batches) QTimer::singleShot(100, this, [this] { nextPage(); });
                else QTimer::singleShot(10000, this, [this] { finishRetained(); });
            });
        });
    }
    void finishRetained() {
        sample("all-closed-after-ten-seconds");
        deadViews = {retained.get()};
        deadPages = {retained->page()};
        deadWindows.clear();
        for (auto* widget : retained->findChildren<QQuickWidget*>())
            deadWindows.append(widget->quickWindow());
        if (lifecycleStress) host.hide();
        retained.reset();
        QTimer::singleShot(1000, this, [this] {
            bool gone = deadWindows.size() == 1 && deadViews[0].isNull() && deadPages[0].isNull();
            for (const auto& window : deadWindows) gone &= window.isNull();
            if (!check(gone && tabs->count() == 0 && requests.blocked.load() == 0,
                    "retained view/page/Quick window released with no remote requests")) return;
            sample("retained-tab-finally-closed");
            qInfo().noquote() << "MEMORY_PROBE" << QString::fromUtf8(QJsonDocument(QJsonObject{
                {"schema", 1}, {"engine", "fully standalone Qt; no Wiz code or resources"},
                {"qtVersion", qVersion()}, {"webEngine", qWebEngineVersion()},
                {"chromium", qWebEngineChromiumVersion()},
                {"chromiumSecurityPatch", qWebEngineChromiumSecurityPatchVersion()},
                {"platform", QGuiApplication::platformName()},
                {"quickBackend", qEnvironmentVariable("QT_QUICK_BACKEND", "default")},
                {"graphicsApi", actualGraphicsApi},
                {"sceneGraphBackend", QQuickWindow::sceneGraphBackend()},
                {"emptyControl", true}, {"visibleCocoa", true},
                {"lifecycleStress", lifecycleStress},
                {"switchResizeStepsPerBatch", lifecycleStress ? 8 : 0},
                {"closeWhileHidden", lifecycleStress},
                {"sharedHost", true}, {"nestedLoopControl", false},
                {"retainedNote", true}, {"stockView", true}, {"standalone", true},
                {"expectedClosedProcessCount", 2}, {"rssIsNotUniquePhysicalMemory", true},
                {"samples", samples}}).toJson(QJsonDocument::Compact));
            qInfo().noquote() << "Standalone Qt memory checks:" << checks << "passed, 0 failed";
            QCoreApplication::exit(0);
        });
    }
};
}

int main(int argc, char** argv) {
    // An explicit flag is required before constructing any QApplication/profile.
    const QStringList args = [&] {
        QStringList list;
        for (int i = 1; i < argc; ++i) list.append(QString::fromLocal8Bit(argv[i]));
        return list;
    }();
    bool numeric = false;
    const int batches = args.size() == 2 ? args[1].toInt(&numeric) : 0;
    if (args.size() != 2 || args[0] != "--visible-cocoa" || !numeric || batches < 1 || batches > 30) {
        qInfo("Usage: standalone-qt-memory --visible-cocoa BATCHES (1..30); synthetic windows, no accounts");
        return 4;
    }
    // The temporary profile must outlive QApplication: Chromium shutdown can
    // finish asynchronous storage work during application destruction.
    QTemporaryDir root(QDir::tempPath() + "/StandaloneQtMemory-XXXXXX");
    if (!root.isValid()) return 3;
    int result = 0;
    {
        QApplication app(argc, argv);
        app.setApplicationName("StandaloneQtMemory");
        app.setQuitOnLastWindowClosed(false);
        if (QGuiApplication::platformName() != "cocoa") return 4;
        Probe probe(root.path(), batches);
        QTimer::singleShot(0, &probe, [&probe] { probe.start(); });
        result = app.exec();
    }
    const bool removed = root.remove();
    qInfo().noquote() << (removed ? "PASS" : "FAIL") << "temporary profile removed after QApplication shutdown";
    return result ? result : removed ? 0 : 5;
}
