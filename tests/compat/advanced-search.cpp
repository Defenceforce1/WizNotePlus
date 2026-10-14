#include "widgets/WizAdvancedSearchDialog.h"
#include "share/WizSearch.h"
#include "share/WizGlobal.h"
#include "share/WizLegacyLightAppearance.h"
#include "WizSearchWidget.h"
#include "gui/documentviewer/WizTitleEdit.h"
#include "database/WizDatabase.h"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QAction>
#include <QMenu>
#include <QCompleter>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QEventLoop>
#include <QTimer>
#include <QStyleFactory>

namespace {
class FixtureSearcher : public WizSearcher
{
public:
    using WizSearcher::WizSearcher;
    CWizDocumentDataArray candidates;
    bool available = true;
    int requests = 0;
protected:
    bool onlineSearchOnly(const QString&, const QString&, CWizDocumentDataArray& result) override
    {
        ++requests;
        result = candidates;
        return available;
    }
};
QString tagParam(const WIZTAGDATA& tag)
{
    return "tag-guid:" + tag.strGUID + ":" + QString::fromLatin1(QUrl::toPercentEncoding(tag.strName));
}
}

int wizAdvancedSearchSelfTest(int argc, char** argv)
{
    // All fixtures live in a fresh temporary store. No token, login or network is used.
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("fusion")); // same renderer as normal startup
    QSettings settings(root.filePath("user.ini"), QSettings::IniFormat);
    QSettings global(root.filePath("global.ini"), QSettings::IniFormat);
    WizGlobal::setSettings(&settings);
    WizGlobal::setGlobalSettings(&global);
    QPalette dark = app.palette();
    dark.setColor(QPalette::Base, Qt::black);
    dark.setColor(QPalette::Text, Qt::white);
    app.setPalette(dark);
    WizApplyLegacyLightAppearance();
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
        return ok;
    };
    // The real menu action also logs a local usage event through the DB manager.
    // Supply that dependency before triggering it; never open an existing store.
    WizDatabaseManager manager("synthetic-search-account");
    if (!check(manager.open(), "fresh isolated database")) return 1;
    auto& db = manager.db();
    db.setKbGUID("synthetic-search-kb");
    {
        WizSearchView view;
        auto* edit = view.findChild<WizSearchEdit*>("wiz-note-search-field");
        bool received = false;
        QObject::connect(&view, &WizSearchView::advancedSearchRequest, [&] { received = true; });
        auto* menu = edit ? edit->findChild<QMenu*>("wiz-note-search-menu") : nullptr;
        auto* advanced = menu ? menu->findChild<QAction*>("actionAdvancedSearch") : nullptr;
        if (advanced) advanced->trigger();
        check(advanced && received, "magnifier menu action forwards advanced-search request");
        if (edit) {
            edit->ensurePolished();
            check((edit->palette().color(QPalette::Base).alpha() == 0
                   || edit->palette().color(QPalette::Base).lightnessF() > 0.8)
                  && edit->palette().color(QPalette::Text).lightnessF() < 0.4,
                  "search field stays readable under initial dark palette");
        }
        WizTitleEdit title(nullptr);
        title.setText(QStringLiteral("测试标题")); title.ensurePolished();
        check(title.palette().color(QPalette::Base).lightnessF() > 0.8
              && title.palette().color(QPalette::Text).lightnessF() < 0.4,
              "title field stays readable after setting text");
    }
    WIZTAGDATA parent, tag, other;
    if (!check(db.createTag("", QStringLiteral("父标签"), "", parent)
        && db.createTag(parent.strGUID, QStringLiteral("数学,公式/引用'_100%"), "", tag)
        && db.createTag("", QStringLiteral("另一标签"), "", other), "Unicode and punctuation tags")) return 1;
    WIZDOCUMENTDATAEX body, title, excluded, trash;
    if (!check(db.createDocument("Body-only match", "body.ziw", "/常用'_%/", "", 0, body)
        && db.createDocument("alpha beta 100%_ O'Reilly", "title.ziw", "/常用'_%/", "", 0, title)
        && db.createDocument("alpha beta 100xx O'Reilly", "excluded.ziw", "/elsewhere/", "", 0, excluded)
        && db.createDocument("alpha beta", "trash.ziw", db.getDeletedItemsLocation(), "", 0, trash),
        "synthetic personal notes")) return 1;
    check(db.insertDocumentTag(body, tag.strGUID) && db.insertDocumentTag(title, tag.strGUID)
        && db.insertDocumentTag(body, other.strGUID) && db.insertDocumentTag(trash, tag.strGUID),
        "synthetic note-tag associations");
    QString where, keyword, name;
    int scope = -100;
    const QString params = "name:Tag search/keyword:alpha/scope:1/" + tagParam(tag);
    check(WizAdvancedSearchDialog::paramToSQL(params, where, keyword, name, scope)
        && scope == Scope_PersonalNotes && keyword == "alpha" && where.contains(tag.strGUID),
        "GUID-backed personal tag condition");
    const QString tagWhere = where;
    FixtureSearcher searcher(manager);
    body.strHighlightText = "<em>alpha</em> in body";
    searcher.candidates = {excluded, body, body, title, trash};
    CWizDocumentDataArray result;
    bool fallback = true;
    check(searcher.searchPersonalWithFilters("alpha", tagWhere, 10, result, fallback)
        && !fallback && result.size() == 2 && result.front().strGUID == body.strGUID
        && result.front().strHighlightText == body.strHighlightText, "body-only online hit retained with highlights");
    check(searcher.searchPersonalWithFilters("alpha", tagWhere, 1, result, fallback)
        && result.size() == 1 && result.front().strGUID == body.strGUID, "limit applied after tag filtering");
    searcher.candidates.clear();
    check(searcher.searchPersonalWithFilters("alpha", tagWhere, 10, result, fallback)
        && result.empty() && !fallback, "successful empty online result is not a title fallback");
    const int calls = searcher.requests;
    check(searcher.searchPersonalWithFilters("", tagWhere, -1, result, fallback)
        && result.size() == 2 && !fallback && searcher.requests == calls, "tag-only search stays local and excludes trash");
    check(WizAdvancedSearchDialog::paramToSQL(params + "/" + tagParam(other), where, keyword, name, scope)
        && searcher.searchPersonalWithFilters("", where, 10, result, fallback)
        && result.size() == 1 && result.front().strGUID == body.strGUID, "multiple tags use AND");
    searcher.available = false;
    check(searcher.searchPersonalWithFilters("beta alpha", tagWhere, 10, result, fallback)
        && fallback && result.size() == 1 && result.front().strGUID == title.strGUID,
        "offline title fallback is explicit and supports multiple words");
    check(searcher.searchPersonalWithFilters("100%_ O'Reilly", "", 10, result, fallback)
        && fallback && result.size() == 1 && result.front().strGUID == title.strGUID,
        "literal percent, underscore and apostrophe in title fallback");
    check(WizAdvancedSearchDialog::paramToSQL("name:Old/keyword:/scope:1/param:"
        + QObject::tr("Folder") + ",常用'_%,", where, keyword, name, scope)
        && searcher.searchPersonalWithFilters("", where, 10, result, fallback) && result.size() == 2,
        "legacy folder condition safely escapes punctuation and LIKE wildcards");
    check(WizAdvancedSearchDialog::paramToSQL("name:Old/keyword:/scope:1/param:"
        + QObject::tr("Contains") + "," + QObject::tr("Attachment") + ",", where, keyword, name, scope)
        && where == "DOCUMENT_ATTACHEMENT_COUNT > 0", "legacy attachment condition");
    check(WizAdvancedSearchDialog::paramToSQL("name:Old/keyword:/scope:1/param:"
        + QObject::tr("Modified time") + "," + QObject::tr("After") + "," + QObject::tr("Last week"),
        where, keyword, name, scope) && where.startsWith("DT_MODIFIED >"), "legacy date condition");
    for (const auto& bad : QStringList{"name:x/keyword:k/scope:9", "scope:no", "scope:1/param:Folder",
            "scope:1/tag-guid:bad:label", "scope:2/" + tagParam(tag), "scope:1/param:Unknown,x",
            "scope:1/param:" + QObject::tr("Created time") + "," + QObject::tr("Before")})
        check(!WizAdvancedSearchDialog::paramToSQL(bad, where, keyword, name, scope), "reject malformed search rather than broaden it");
    check(WizAdvancedSearchDialog::paramToSQL("name:keyword:name:/keyword:encoded:literal/scope:1",
        where, keyword, name, scope) && name == "keyword:name:" && keyword == "encoded:literal",
        "legacy field values retain prefix-like text");

    {
        WizAdvancedSearchDialog dialog(false);
        auto* first = dialog.findChild<QComboBox*>("comboBox_first");
        auto* second = dialog.findChild<QComboBox*>("comboBox_second");
        auto* list = dialog.findChild<QListWidget*>("listWidget");
        auto* keyEdit = dialog.findChild<QLineEdit*>("lineEdit_keyword");
        auto* nameEdit = dialog.findChild<QLineEdit*>("lineEdit_name");
        if (!check(first && second && list && keyEdit && nameEdit, "real advanced search widgets")) return 1;
        const int tagIndex = first->findText(QObject::tr("Tag"));
        first->setCurrentIndex(tagIndex);
        check(tagIndex >= 0 && QMetaObject::invokeMethod(first, "activated", Q_ARG(int, tagIndex))
            && second->findData(tag.strGUID) >= 0, "Qt 6 selector signal populates GUID-backed tags");
        second->setCurrentIndex(second->findData(tag.strGUID));
        QMetaObject::invokeMethod(&dialog, "on_toolButton_add_clicked");
        QMetaObject::invokeMethod(&dialog, "on_toolButton_add_clicked");
        check(list->count() == 1 && list->item(0)->text().contains(parent.strName),
            "nested tag label and duplicate prevention");
        keyEdit->setText(QStringLiteral("x/y keyword:name:"));
        nameEdit->setText(QStringLiteral("搜索/数学"));
        const QString saved = dialog.getParams();
        check(WizAdvancedSearchDialog::paramToSQL(saved, where, keyword, name, scope)
            && keyword == keyEdit->text() && name == nameEdit->text(), "slash-containing search fields round trip");
        dialog.setParams(saved);
        check(dialog.getParams() == saved && list->count() == 1, "saved advanced search round trip without duplicate conditions");
        tag.strName = QStringLiteral("已改名");
        check(db.modifyTag(tag) && WizAdvancedSearchDialog::paramToSQL(saved, where, keyword, name, scope)
            && searcher.searchPersonalWithFilters("", where, 10, result, fallback) && result.size() == 2,
            "tag rename keeps saved search bound to same GUID");
    }
    {
        WizSearchView view;
        view.setFixedWidth(300);
        view.setAvailableTags({{QStringLiteral("父标签 / 已改名"), tag.strGUID}, {other.strName, other.strGUID}});
        view.setAvailableFolders({QStringLiteral("/常用'_ %/"), QStringLiteral("/研究,100%_/子/更深/")});
        auto* edit = view.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* tags = view.findChild<QListWidget*>("wiz-note-panel-tags");
        auto* folder = view.findChild<QComboBox*>("wiz-note-panel-folder");
        auto* attachment = view.findChild<QCheckBox*>("wiz-note-panel-attachments");
        auto* completer = view.findChild<QCompleter*>();
        if (!check(edit && tags && folder && attachment && completer, "inline search controls present")) return 1;
        int submissions = 0;
        QString submittedKeyword;
        QObject::connect(&view, &WizSearchView::doSearch, [&](const QString& text) {
            ++submissions; submittedKeyword = text;
        });
        tags->item(0)->setCheckState(Qt::Checked);
        app.processEvents();
        check(view.hasFilters() && view.queryParams().contains(tag.strGUID) && submissions == 1,
            "inline tag selection produces one local query with stable GUID");
        tags->item(0)->setCheckState(Qt::Checked); app.processEvents();
        check(submissions == 1, "selecting same tag does not duplicate condition or search");
        edit->setText("alpha");
        searcher.available = true; searcher.candidates = {body, title, excluded, trash};
        check(WizAdvancedSearchDialog::paramToSQL(view.queryParams(), where, keyword, name, scope)
            && keyword == "alpha" && searcher.searchPersonalWithFilters(keyword, where, 10, result, fallback)
            && !fallback && result.size() == 2 && result.front().strHighlightText == body.strHighlightText,
            "inline keyword plus tag reuses full-text filtering and retains body highlights");
        const QString deep = QStringLiteral("/研究,100%_/子/更深/");
        folder->setCurrentIndex(folder->findData(deep));
        QMetaObject::invokeMethod(folder, "activated", Q_ARG(int, folder->currentIndex()));
        attachment->setChecked(true); app.processEvents();
        const QString saved = view.queryParams();
        check(WizAdvancedSearchDialog::paramToSQL(saved, where, keyword, name, scope)
            && where.contains("DOCUMENT_LOCATION LIKE") && where.contains("100\\%\\_")
            && where.contains("DOCUMENT_ATTACHEMENT_COUNT > 0") && where.contains(tag.strGUID),
            "inline folder and attachment combine with tag and safely escape deep Unicode path");
        WizAdvancedSearchDialog advanced(false); advanced.setParams(saved);
        check(advanced.getParams() == saved && view.setQueryParams(advanced.getParams()),
            "inline query round trips through existing saved-search dialog");
        const QString date = "param:" + QObject::tr("Modified time") + "," + QObject::tr("After") + "," + QObject::tr("Last week");
        check(view.setQueryParams(saved + "/" + date), "complex date condition can return to inline search");
        edit->setText(QStringLiteral("x/y"));
        check(WizAdvancedSearchDialog::paramToSQL(view.queryParams(), where, keyword, name, scope)
            && keyword == "x/y" && where.contains("DT_MODIFIED >"),
            "editing keyword preserves complex date filters and slash encoding");
        auto* clear = view.findChild<QPushButton*>("wiz-note-panel-clear");
        if (clear) clear->click(); app.processEvents();
        check(clear && !view.hasFilters() && view.keyword() == "x/y", "clearing conditions preserves content keyword");
        view.clear(); check(!view.hasFilters() && view.keyword().isEmpty(), "reset clears all inline query state without search");
        view.show();
        view.activateWindow(); edit->setFocus(); app.processEvents();
        QEventLoop activation;
        QTimer focusPoll; focusPoll.setInterval(25);
        QObject::connect(&focusPoll, &QTimer::timeout, &activation, [&] {
            // Component tests use synthetic key events; activate their own Qt
            // window explicitly. Real Cocoa activation is covered separately
            // by the actual-main-window fixture, without weakening assertions.
            QApplication::setActiveWindow(&view);
            view.activateWindow(); edit->setFocus();
            if (edit->hasFocus()) activation.quit();
        });
        focusPoll.start();
        QTimer::singleShot(1500, &activation, &QEventLoop::quit); activation.exec(); focusPoll.stop();
        edit->setText(QStringLiteral("已改名"));
        QMetaObject::invokeMethod(edit, "textEdited", Q_ARG(QString, edit->text()));
        QEventLoop suggestions;
        QTimer poll; poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &suggestions, [&] {
            if (completer->popup()->isVisible() && completer->completionCount() == 1) suggestions.quit();
        });
        poll.start();
        QTimer::singleShot(2000, &suggestions, &QEventLoop::quit); suggestions.exec();
        poll.stop();
        const bool suggested = completer->popup()->isVisible() && completer->completionCount() == 1;
        check(suggested && !completer->popup()->currentIndex().isValid() && !view.hasFilters(),
            "typed tag suggestions are local and do not silently select a tag");
        const int before = submissions;
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(edit, &enter); app.processEvents();
        check(submissions == before + 1 && submittedKeyword == QStringLiteral("已改名") && !view.hasFilters(),
            "Return without explicit tag choice remains ordinary content search");
        completer->setCompletionPrefix(QStringLiteral("已改名"));
        completer->activated(completer->completionModel()->index(0, 0)); app.processEvents();
        check(view.hasFilters() && view.keyword().isEmpty() && view.queryParams().contains(tag.strGUID),
            "explicit tag completion consumes typed label and creates a removable filter");
        view.setAvailableTags({{QStringLiteral("新的标签名"), tag.strGUID}, {other.strName, other.strGUID}});
        check(view.queryParams().contains(tag.strGUID) && view.queryParams().contains(QString::fromLatin1(QUrl::toPercentEncoding("新的标签名"))),
            "renamed selected tag retains GUID and updates visible label");
        view.setAvailableTags({{other.strName, other.strGUID}});
        check(view.hasFilters() && view.queryParams().contains(tag.strGUID),
            "missing selected tag stays restrictive instead of silently broadening query");
        check(!WizAdvancedSearchDialog::paramToSQL("scope:1/folder-path-encoded:relative", where, keyword, name, scope)
            && !WizAdvancedSearchDialog::paramToSQL("scope:2/folder-path-encoded:%2Fx%2F", where, keyword, name, scope),
            "invalid or nonpersonal encoded folder query is rejected");
        view.clear(); view.hide();
    }
    qInfo() << "Advanced search failures:" << failures;
    return failures ? 1 : 0;
}
