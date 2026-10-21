#include "WizPreferenceDialog.h"
#include "share/WizUiLanguages.h"
#include "share/WizMisc.h"
#include "share/WizSettings.h"
#include "share/WizPdfExportMenu.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTemporaryDir>
#include <QTabWidget>
#include <stdexcept>

namespace {
class App final : public WizExplorerApp {
public:
    App(WizDatabaseManager& manager, WizUserSettings& settings) : manager(manager), settings(settings) {}
    QWidget window;
    WizDatabaseManager& manager;
    WizUserSettings& settings;
    QWidget* mainWindow() override { return &window; }
    QObject* object() override { return &window; }
    WizDatabaseManager& databaseManager() override { return manager; }
    WizCategoryBaseView& category() override { throw std::logic_error("no category in settings diagnostic"); }
    WizUserSettings& userSettings() override { return settings; }
};
}
int wizSettingsUiSelfTest(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    QStringList locales; WizGetTranslatedLocales(locales);
    check(locales == QStringList({"zh_CN", "en_US"}), "UI language choices contain Simplified Chinese and English only");
    check(WizGetTranslatedLocaleDisplayName(0) == "Simplified Chinese"
          && WizGetTranslatedLocaleDisplayName(1) == "English(US)", "language names stay aligned after removing Traditional option");
    for (const auto& locale : {"zh_TW", "zh_HK", "zh-Hant-TW", "zh_CN", "zh_SG"})
        check(wizSupportedUiLocale(locale) == "zh_CN", "Chinese UI variants fall back to the retained Simplified catalog");
    check(wizSupportedUiLocale("en_US") == "en_US" && wizSupportedUiLocale("fr_FR") == "en_US",
          "unavailable UI catalogs fall back to English");
    WizSettingsMenuTranslator menuTranslator; menuTranslator.locale = "zh_CN";
    app.installTranslator(&menuTranslator);
    check(QCoreApplication::translate("QMenuBar", "Preferences...") == QStringLiteral("设置…"),
          "Cocoa application menu PreferencesRole is translated to Settings");
    check(menuTranslator.translate("OtherContext", "Preferences...").isNull()
          && menuTranslator.translate("QMenuBar", "Quit").isNull(), "native-menu override affects only the settings item");
    menuTranslator.locale = "en_US";
    check(QCoreApplication::translate("QMenuBar", "Preferences...") == QStringLiteral("Settings…"),
          "English native application menu also uses Settings");
    app.removeTranslator(&menuTranslator);
    QTranslator chinese;
    const QString resources = QCoreApplication::applicationDirPath() + "/../Resources/locales/";
    if (!check(chinese.load(resources + "wiznote_zh_CN.qm"), "current built Simplified app catalog loads")) return 1;
    app.installTranslator(&chinese);
    check(QObject::tr("Settings...") == QStringLiteral("设置…"), "normal settings action uses the updated app translation");
    WizDatabaseManager manager("synthetic-settings-ui-only");
    if (!check(manager.open(), "temporary private database opens without login")) return 1;
    WizUserSettings settings(manager.db());
    App explorer(manager, settings);
    check(settings.syncGroupMethod() == -1 && manager.db().getObjectSyncTimeline() == 99999,
          "group default is title-only while personal sync default is unchanged");
    manager.db().setMeta("QT_WIZNOTE", "Locale", "zh_TW");
    check(settings.locale() == "zh_CN" && manager.db().getMetaDef("QT_WIZNOTE", "Locale") == "zh_TW",
          "stored Traditional UI preference falls back without rewriting account metadata");
    {
        WizPreferenceWindow dialog(explorer, &explorer.window);
        auto* language = dialog.findChild<QComboBox*>("comboLang");
        auto* groups = dialog.findChild<QComboBox*>("comboSyncGroupMethod");
        check(dialog.windowTitle() == QStringLiteral("设置"), "actual settings window title is Settings");
        auto* tabs=dialog.findChild<QTabWidget*>("tabWidget");
        auto* pdfPage=dialog.findChild<QWidget*>("tabPrint");
        check(tabs && pdfPage && tabs->tabText(tabs->indexOf(pdfPage))==QStringLiteral("PDF 导出"),
              "actual translated margins page is named PDF Export");
        auto* description=dialog.findChild<QLabel*>("labelPdfMargins");
        check(description && description->wordWrap() && description->text()==QStringLiteral("页边距同时用于两种 PDF 版式。阅读版式保持正文宽度；A4 版式固定纸张大小。"),
              "actual translated margins description explains both unchanged layout rules");
        dialog.showPrintMarginPage();
        check(tabs && tabs->currentWidget()==pdfPage
              && dialog.findChild<QDoubleSpinBox*>("spinBox_top")->value()==settings.printMarginValue(wizPositionTop)
              && dialog.findChild<QComboBox*>("comboBox_unit")->currentIndex()==settings.printMarginUnit(),
              "existing page navigation and stored margin values stay connected");
        QAction pdfAction(&dialog);
        WizPdfExport::attachMenu(&pdfAction,&dialog,[](WizPdfExport::Mode){});
        const auto modes=pdfAction.menu()->actions();
        check(modes.size()==2 && modes[0]->text()==QStringLiteral("阅读版式…") && modes[1]->text()==QStringLiteral("A4 版式…")
              && modes[0]->toolTip()==QStringLiteral("保持当前阅读区域的正文宽度，页边距加在正文外侧。")
              && modes[1]->toolTip()==QStringLiteral("使用 A4 纸张，页边距决定正文可用区域。"),
              "actual Simplified catalog supplies both PDF menu titles and descriptions");
        check(language && language->count() == 2 && language->currentIndex() == 0,
              "actual settings language menu selects retained Chinese fallback");
        check(groups && groups->currentIndex() == 0 && groups->isEnabled() && groups->count() == 5,
              "no-group settings default to titles without locking the user's options");
        check(manager.count() == 0, "opening settings does not create any group database");
        const QString evidence = QString::fromUtf8(qgetenv("WIZNOTE_SETTINGS_EVIDENCE_DIR"));
        if (!evidence.isEmpty()) {
            dialog.show();
            QApplication::processEvents();
            dialog.grab().save(QDir(evidence).filePath("settings-general.png"));
            auto* tabs = dialog.findChild<QTabWidget*>("tabWidget");
            tabs->setCurrentWidget(dialog.findChild<QWidget*>("tabSyncing"));
            QApplication::processEvents();
            dialog.grab().save(QDir(evidence).filePath("settings-group-default.png"));
            dialog.showPrintMarginPage();
            QApplication::processEvents();
            dialog.grab().save(QDir(evidence).filePath("settings-pdf.png"));
            dialog.close();
        }
    }
    if (!check(manager.open("synthetic-existing-group"), "temporary group opens without contacting any service")) return 1;
    auto& group = manager.db("synthetic-existing-group");
    check(group.getObjectSyncTimeline() == -1, "a new group with no explicit timeline defaults to titles");
    CWizObjectDataArray download;
    check(group.getObjectsNeedToBeDownloaded(download) && download.empty(), "title-only default produces no automatic body or attachment download work");
    group.setObjectSyncTimeLine(7);
    settings.setSyncGroupMethod(30);
    check(group.getObjectSyncTimeline() == 7 && settings.syncGroupMethod() == 30,
          "existing explicit per-group and user settings are not overwritten by new defaults");
    {
        WizPreferenceWindow dialog(explorer, &explorer.window);
        check(dialog.findChild<QComboBox*>("comboSyncGroupMethod")->currentIndex() == 2
              && group.getObjectSyncTimeline() == 7, "actual settings honors an existing explicit group choice");
    }
    app.removeTranslator(&chinese);
    qInfo().noquote() << "settings-ui checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
