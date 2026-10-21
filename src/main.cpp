#include <QtGlobal>
#include <QApplication>
#include <QTreeWidget>
#include <QMessageBox>
#include <QIcon>
#include <QDir>
#include <QPixmapCache>
#include <QTranslator>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QTemporaryDir>
#include <QDesktopServices>
#include "share/WizNetworkSecurity.h"
#include <QNetworkProxy>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtWebEngine>
#endif
#include <QStyleFactory>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include "share/WizWebEngineView.h"
#include <QVersionNumber>

#include <sys/stat.h>

#include "utils/WizPathResolve.h"
#include "utils/WizLogger.h"
#include "utils/WizStyleHelper.h"
#include "share/WizSettings.h"
#include "share/WizWin32Helper.h"
#include "database/WizDatabaseManager.h"
#include "share/WizSingleApplication.h"
#include "share/WizThreads.h"
#include "share/WizQueuedThreadsSession.h"
#include "share/WizGlobal.h"
#include "share/WizLegacyLightAppearance.h"
#include "WizBuildIdentity.h"
#include "WizStandaloneStorage.h"

#include "core/WizNoteManager.h"

#ifdef Q_OS_MAC
#include "mac/WizMacHelper.h"
#include "mac/WizIAPHelper.h"
#endif

#include "sync/WizToken.h"
#include "sync/WizApiEntry.h"
#include "sync/WizAvatarHost.h"
#include "share/WizUiLanguages.h"
#include "WizThumbCache.h"
#include "WizMainWindow.h"
#include "WizLoginDialog.h"

#ifdef Q_OS_MAC
#  define SHARE_PATH "/../Resources"
#else
#  define SHARE_PATH "/../share/wiznote"
#endif

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#ifdef Q_OS_LINUX
#include <gnu/libc-version.h>
#endif

void disableWebengineSandboxIfNeeded()
{
#ifndef Q_OS_LINUX
    return;
#else
#   if (QT_VERSION > QT_VERSION_CHECK(6, 0, 0))
    return;
#   else
    const char* version = gnu_get_libc_version();
    QVersionNumber current = QVersionNumber::fromString(version);
    QVersionNumber compareTo(2, 34);
    if (current >= compareTo) {
        qDebug() << "Disabling Qt WebEngine sandbox as GLIBC"
                 << version << "will break it";
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    }
#   endif
#endif
}

//-------------------------------------------------------------------
// 登录与启动
//-------------------------------------------------------------------

int mainCore(int argc, char *argv[], WizStandaloneStorage::Session* standaloneSession, QString& restartExecutable,
             bool cancelStartupProbe = false)
{
    // 设置高分屏支持
    //-------------------------------------------------------------------

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif

    // Init Application and QtWebEngine
    //-------------------------------------------------------------------

#ifdef Q_OS_LINUX
    // create single application for linux
    WizSingleApplication app(argc, argv, "Special-Message-for-WizNote-SingleApplication");
    if (app.isRunning())
    {
        app.sendMessage(WIZ_SINGLE_APPLICATION);
        return 0;
    }
#else

    // create application for Windows and MacOS
    QApplication app(argc, argv);

#ifdef BUILD4APPSTORE
    QDir dir(QApplication::applicationDirPath());
    dir.cdUp();
    dir.cd("PlugIns");
    QApplication::setLibraryPaths(QStringList(dir.absolutePath()));
#endif // BUILD4APPSTORE

#ifdef BUILD4APPSTORE
    WizIAPHelper helper;
    helper.validteReceiptOnLauch();
#endif // BUILD4APPSTORE

#endif // Q_OS_LINUX

    WizNetworkSecurity::installSecureDefaults();
    if (cancelStartupProbe) {
        QTimer::singleShot(100, &app, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->done(QMessageBox::Cancel);
        });
    }
    if (standaloneSession && !WizStandaloneStorage::confirmFirstLaunch(*standaloneSession)) return 0;
    disableWebengineSandboxIfNeeded();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QtWebEngine::initialize();
    QWebEngineSettings* webEngineSettings = QWebEngineSettings::defaultSettings();
#else
    // Set the same identity before Qt 6 resolves the persistent profile path.
    // Qt 5's profile was created later, after the identity setup below.
    QApplication::setApplicationName(QObject::tr("WizNotePlus"));
    QApplication::setOrganizationName(WizBuildIdentity::organizationName());
    QWebEngineSettings* webEngineSettings = WizWebEngineProfile()->settings();
#endif
    webEngineSettings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    webEngineSettings->setAttribute(QWebEngineSettings::AllowWindowActivationFromJavaScript, true);
#endif

    webEngineSettings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    webEngineSettings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);

    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

    // 配置QtApp和Debug
    //-------------------------------------------------------------------

    // Debug 输出
    qInstallMessageHandler(Utils::WizLogger::messageHandler); // 输出到 Wiznote 消息控制台

    // 设置应用名和组织名用于QSetting
    QApplication::setApplicationName(QObject::tr("WizNotePlus"));
    QApplication::setOrganizationName(WizBuildIdentity::organizationName());
    WizBuildIdentity::applyDisplayName();

    QIcon icon;
    icon.addPixmap(QPixmap(":/logo_16.png"));
    icon.addPixmap(QPixmap(":/logo_32.png"));
    icon.addPixmap(QPixmap(":/logo_48.png"));
    icon.addPixmap(QPixmap(":/logo_64.png"));
    icon.addPixmap(QPixmap(":/logo_128.png"));
    icon.addPixmap(QPixmap(":/logo_256.png"));
    QApplication::setWindowIcon(icon);

#ifdef Q_OS_MAC
    wizMacInitUncaughtExceptionHandler();
    wizMacRegisterSystemService();

    // init sys local for crash report
    QString sysLocal = QLocale::system().name();
    QTranslator translatorSys;
    QString sysLocalFile = Utils::WizPathResolve::localeFileName(sysLocal);
    translatorSys.load(sysLocalFile);
    app.installTranslator(&translatorSys);

    initCrashReporter();

    app.removeTranslator(&translatorSys);

    WizQueuedThreadsSession queuedThreads([] { WizNetworkSecurity::beginShutdown(); });

#endif

    // 获取默认用户设置
    //-------------------------------------------------------------------

    QSettings::setDefaultFormat(QSettings::IniFormat);

    /** Global INI setting. File is located at ~/.wiznote/wiznote.ini or %HOMEPATH%/Wiznote/wiznote.ini */
    QSettings* globalSettings = new QSettings(Utils::WizPathResolve::globalSettingsFile(), QSettings::IniFormat);
    WizGlobal::setGlobalSettings(globalSettings);

    // use 3 times(30M) of Qt default usage
    int nCacheSize = globalSettings->value("Common/Cache", 10240*3).toInt();
    QPixmapCache::setCacheLimit(nCacheSize);

    /** GUID of default user. */
    QString strUserGuid = globalSettings->value("Users/DefaultUserGuid").toString();
    QList<WizLocalUser> localUsers;
    WizGetLocalUsers(localUsers);
    /** Folder name of default user. */
    QString strAccountFolderName = WizGetLocalFolderName(localUsers, strUserGuid);

    /** Setting from META table within user sqlite database. */
    WizUserSettings userSettings(strAccountFolderName);

    /** User specific INI setting. */
    QSettings* settings = new QSettings(Utils::WizPathResolve::userSettingsFile(strAccountFolderName), QSettings::IniFormat);
    WizGlobal::setSettings(settings);

    // 样式
    app.setStyle(QStyleFactory::create("fusion"));
    WizApplyLegacyLightAppearance();
    app.setStyleSheet(WizLoadSkinStyleSheet(userSettings.skin()));

    // 语言本地化
    //-------------------------------------------------------------------

    // setup locale for welcome dialog
    QString strLocale = userSettings.locale();
    QLocale::setDefault(QLocale(strLocale));

    QTranslator translatorWizNote;
    QString strLocaleFile = Utils::WizPathResolve::localeFileName(strLocale);
    translatorWizNote.load(strLocaleFile);
    app.installTranslator(&translatorWizNote);

    QTranslator translatorQt;
    strLocaleFile = Utils::WizPathResolve::qtLocaleFileName(strLocale);
    translatorQt.load(strLocaleFile);
    app.installTranslator(&translatorQt);

    WizSettingsMenuTranslator settingsMenuTranslator;
    settingsMenuTranslator.locale = strLocale;
    app.installTranslator(&settingsMenuTranslator);

    // 登录程序
    //-------------------------------------------------------------------

    QFont font(WizUserSettings::kDefaultUIFontFamily, WizUserSettings::kDefaultUIFontSize);
    app.setFont(font);

    // figure out auto login or manually login
    bool bFallback = true;

    // FIXME: move to WizService initialize
    WizToken token;

    QString strPassword = userSettings.password();

    bool bAutoLogin = userSettings.autoLogin();
    if (bAutoLogin && !strPassword.isEmpty()) {
        bFallback = false;
    }

    //set network proxy
    WizSettings wizSettings(Utils::WizPathResolve::globalSettingsFile());
    if (wizSettings.getProxyStatus())
    {
        QNetworkProxy proxy;
        proxy.setType(QNetworkProxy::HttpProxy);
        proxy.setHostName(wizSettings.getProxyHost());
        proxy.setPort(wizSettings.getProxyPort());
        proxy.setUser(wizSettings.getProxyUserName());
        proxy.setPassword(wizSettings.getProxyPassword());
        QNetworkProxy::setApplicationProxy(proxy);
    }

    // Login procedure

    QString strUserId = WizGetLocalUserId(localUsers, strUserGuid);
    bool isNewRegisterAccount = false;

    if (bFallback)
    {
        WizLoginDialog loginDialog(strLocale, localUsers);
        if (QDialog::Accepted != loginDialog.exec())
            return 0;

        if (strUserId.isEmpty() || loginDialog.loginUserGuid() != strUserGuid)
        {
            // 获取另一个用户的账户文件夹
            strAccountFolderName = WizGetLocalFolderName(localUsers, loginDialog.loginUserGuid());
            if (strAccountFolderName.isEmpty())
            {
                strAccountFolderName = loginDialog.userId();
            }
            qDebug() << "login user id : " << loginDialog.userId();

            // 写入另一个用户设置
            settings = new QSettings(Utils::WizPathResolve::userSettingsFile(strAccountFolderName), QSettings::IniFormat);
            WizGlobal::setSettings(settings);
        }

        strPassword = loginDialog.password();
        strUserId = loginDialog.userId();
        isNewRegisterAccount = loginDialog.isNewRegisterAccount();
    }
    else
    {
        // WizBox企业登录设置
        if (userSettings.serverType() == EnterpriseServer)
        {
            WizCommonApiEntry::setEnterpriseServerIP(userSettings.enterpriseServerIP());
        }
        else if (userSettings.serverType() == WizServer ||
                 (userSettings.serverType() == NoServer && !userSettings.myWizMail().isEmpty()))
        {
            WizCommonApiEntry::setEnterpriseServerIP(WIZNOTE_API_SERVER);
        }
    }

    // Reset locale for current user.
    userSettings.setAccountFolderName(strAccountFolderName);
    userSettings.setUserId(strUserId);
    strLocale = userSettings.locale();

    app.removeTranslator(&translatorWizNote);
    strLocaleFile = Utils::WizPathResolve::localeFileName(strLocale);
    translatorWizNote.load(strLocaleFile);
    app.installTranslator(&translatorWizNote);

    app.removeTranslator(&translatorQt);
    strLocaleFile = Utils::WizPathResolve::qtLocaleFileName(strLocale);
    translatorQt.load(strLocaleFile);
    app.installTranslator(&translatorQt);

    WizCommonApiEntry::setLanguage(strLocale);
    app.removeTranslator(&settingsMenuTranslator);
    settingsMenuTranslator.locale = strLocale;
    app.installTranslator(&settingsMenuTranslator);

    QFont uiFont(userSettings.UIFontFamily(), userSettings.UIFontSize());
    app.setFont(uiFont);

    // 登录数据库管理器
    WizDatabaseManager dbMgr(strAccountFolderName);
    if (!dbMgr.openAll()) {
        QMessageBox::critical(NULL, "", QObject::tr("Can not open database"));
        return 0;
    }

    qDebug() << "set user id for token ; " << strUserId;
    WizToken::setUserId(strUserId);
    WizToken::setPasswd(strPassword);

    dbMgr.db().setPassword(::WizEncryptPassword(strPassword));
    dbMgr.db().updateInvalidData();

    // FIXME: move to plugins
    WizAvatarHost avatarHost;

    // FIXME: move to core plugin initialize
    WizThumbCache cache;

    // 启动Wiz主窗口
    WizMainWindow window(dbMgr);

#ifdef Q_OS_LINUX
    QObject::connect(&app, SIGNAL(messageAvailable(QString)), &window,
                     SLOT(on_application_messageAvailable(QString)));
#endif

    window.show();
    window.init();

#ifdef Q_OS_MAC
    //start and set safari extension
    WIZUSERINFO userInfo;
    dbMgr.db().getUserInfo(userInfo);
    if (!WizBuildIdentity::appleSiliconTrial) {
        WizExecuteOnThread(WIZ_THREAD_DEFAULT, [strUserId, userInfo](){
            updateShareExtensionAccount(strUserId, userInfo.strUserGUID, userInfo.strMywizEmail ,userInfo.strDisplayName);
        });
    }
#endif

    //create introduction note for new register users
    WizNoteManager noteManager(dbMgr);
    noteManager.updateTemplateJS(userSettings.locale());
    noteManager.downloadTemplatePurchaseRecord();
    if (isNewRegisterAccount)
    {
        noteManager.createIntroductionNoteForNewRegisterAccount();
    }

    int ret = app.exec();
    if (window.isLogout()) {
        userSettings.setPassword("");
#ifndef BUILD4APPSTORE
        // Restart only after browser/database destruction, temp cleanup and
        // release of the standalone store lock in main(). Never find an old app.
        restartExecutable = QApplication::applicationFilePath();
#else
        QString strAppFile = QApplication::applicationDirPath().remove("/Contents/MacOS");
        if (!QProcess::startDetached("/usr/bin/open -W "+strAppFile))
        {
            QMessageBox::information(0, "Info", "open " + strAppFile + " failed");
        }
#endif
    }

    return ret;
}

int main(int argc, char *argv[])
{
#if defined(WIZNOTE_NATIVE_TRANSFER_TESTS) && defined(Q_OS_MAC)
    if (QByteArray(argv[0]).endsWith("/WizNoteBiometricMainTest")) {
        if (argc != 2 || (QByteArray(argv[1]) != "--biometric-main-window-probe"
                         && QByteArray(argv[1]) != "--biometric-save-quit-probe")
            || qgetenv("WIZNOTE_SYNTHETIC_TOUCH_ID_CONSENT") != "create-read-delete") {
            qCritical() << "Isolated main-window fixture: explicit option/consent required; normal startup refused.";
            return 2;
        }
        qputenv("WIZNOTE_TEST_FOCUS", "biometric-main-window");
        extern int wizNativeFeatureAcceptance(int, char**);
        return wizNativeFeatureAcceptance(argc, argv);
    }
    // The separately named signed fixture must never reach account bootstrap,
    // including a Finder launch, malformed flag, or unrelated diagnostic flag.
    if (QByteArray(argv[0]).endsWith("/WizNoteTouchIdTest")
        || (argc == 2 && (QByteArray(argv[1]) == "--touch-id-system-probe"
                         || QByteArray(argv[1]) == "--touch-id-prerequisite-probe"))) {
        extern int wizTouchIdSystemProbe(int, char**);
        return wizTouchIdSystemProbe(argc, argv);
    }
#endif
    if (argc == 2 && QByteArray(argv[1]) == "--check-public-https") {
        QCoreApplication diagnostic(argc, argv);
        return WizNetworkSecurity::checkPublicHttps();
    }
    if (WizBuildIdentity::appleSiliconTrial && argc == 2 && QByteArray(argv[1]) == "--check-startup") {
        QCoreApplication diagnostic(argc, argv);
        const QString candidate = qEnvironmentVariableIsSet("WIZNOTE_DATA_STORE")
            ? qEnvironmentVariable("WIZNOTE_DATA_STORE") : WizStandaloneStorage::defaultRoot(QDir::homePath());
        QString root, error;
        if (!WizStandaloneStorage::validateRoot(QDir::homePath(), candidate, root, error)) {
            qWarning().noquote() << error; return 1;
        }
        qInfo().noquote() << "独立资料库路径：" << root;
        qInfo() << "仅校验路径；未打开账号、创建目录或联网。";
        return 0;
    }
#ifdef WIZNOTE_NATIVE_TRANSFER_TESTS
    if (argc == 2 && QByteArray(argv[1]) == "--certificate-creation-self-test") {
        extern int wizCertificateCreationSelfTest(int, char**);
        return wizCertificateCreationSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--certificate-creation-ui-self-test") {
        extern int wizCertificateCreationUiSelfTest(int, char**);
        return wizCertificateCreationUiSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--biometric-keychain-self-test") {
        extern int wizBiometricKeychainSelfTest(int, char**);
        return wizBiometricKeychainSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--biometric-unlock-ui-self-test") {
        extern int wizBiometricUnlockUiSelfTest(int, char**);
        return wizBiometricUnlockUiSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--biometric-document-routing-self-test") {
        extern int wizBiometricDocumentRoutingSelfTest(int, char**);
        return wizBiometricDocumentRoutingSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--document-unprotect-self-test") {
        extern int wizDocumentUnprotectSelfTest(int, char**);
        return wizDocumentUnprotectSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--biometric-note-access-self-test") {
        extern int wizBiometricNoteAccessSelfTest(int, char**);
        return wizBiometricNoteAccessSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--settings-ui-self-test") {
        extern int wizSettingsUiSelfTest(int, char**);
        return wizSettingsUiSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--account-file-selection-self-test") {
        extern int wizAccountFileSelectionSelfTest(int, char**);
        return wizAccountFileSelectionSelfTest(argc, argv);
    }
    if (argc == 2 && (QByteArray(argv[1]) == "--native-editor-runtime-self-test" || QByteArray(argv[1]) == "--editor-memory-probe")) {
        extern int wizNativeEditorRuntimeSelfTest(int, char**);
        return wizNativeEditorRuntimeSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--comment-lifetime-self-test") {
        extern int wizCommentLifetimeSelfTest(int, char**);
        return wizCommentLifetimeSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--spellcheck-self-test") {
        extern int wizSpellCheckSelfTest(int, char**);
        return wizSpellCheckSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--native-clipper-safari-probe") {
        extern int wizNativeClipperSafariProbe(int, char**);
        return wizNativeClipperSafariProbe(argc, argv);
    }
    if (argc == 3 && QByteArray(argv[1]) == "--native-clipper-transport-self-test") {
        extern int wizNativeClipperTransportSelfTest(int, char**);
        return wizNativeClipperTransportSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--native-clipper-self-test") {
        extern int wizNativeClipperSelfTest(int, char**);
        return wizNativeClipperSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--shutdown-self-test") {
        extern int wizShutdownSelfTest(int, char**);
        return wizShutdownSelfTest(argc, argv);
    }
    if (argc >= 2 && QByteArray(argv[1]) == "--network-security-self-test") {
        extern int wizNetworkSecuritySelfTest(int, char**);
        return wizNetworkSecuritySelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--standalone-bootstrap-cancel-probe") {
        QTemporaryDir fixture;
        if (!fixture.isValid()) return 1;
        const QString home = QFileInfo(fixture.path()).canonicalFilePath() + "/home";
        QDir().mkpath(home);
        WizStandaloneStorage::Session testSession; QString error, restart;
        if (!testSession.prepare(home, QString(), error)) return 1;
        const int ret = mainCore(argc, argv, &testSession, restart, true);
        const bool isolated = ret == 0 && !testSession.hasConsent() && restart.isEmpty()
            && !WizGlobal::settings() && !WizGlobal::globalSettings()
            && !QFileInfo::exists(testSession.root() + "/webengine")
            && !QFileInfo::exists(home + "/.wiznote");
        qInfo() << "Production bootstrap cancel before account/profile:" << isolated;
        return isolated ? 0 : 1;
    }
    if (argc == 2 && QByteArray(argv[1]) == "--standalone-startup-self-test") {
        extern int wizStandaloneStartupSelfTest(int, char**);
        return wizStandaloneStartupSelfTest(argc, argv);
    }
    if (argc == 4 && QByteArray(argv[1]) == "--standalone-lock-probe") {
        extern int wizStandaloneLockProbe(int, char**);
        return wizStandaloneLockProbe(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--note-presentation-self-test") {
        extern int wizNotePresentationSelfTest(int, char**);
        return wizNotePresentationSelfTest(argc, argv);
    }
    if (argc == 2 && QByteArray(argv[1]) == "--advanced-search-self-test") {
        extern int wizAdvancedSearchSelfTest(int, char**);
        return wizAdvancedSearchSelfTest(argc, argv);
    }
    // This route never starts login, networking, or the normal data-store cleanup.
    if (argc == 2 && QByteArray(argv[1]) == "--native-note-transfer-self-test") {
        extern int wizNativeNoteTransferSelfTest(int, char**);
        return wizNativeNoteTransferSelfTest(argc, argv);
    }
#ifdef Q_OS_MAC
    if (argc == 2 && QByteArray(argv[1]) == "--native-feature-acceptance-probe") {
        extern int wizNativeFeatureAcceptance(int, char**);
        return wizNativeFeatureAcceptance(argc, argv);
    }
    if (argc == 2 && (QByteArray(argv[1]) == "--mac-window-controls-self-test"
                     || QByteArray(argv[1]) == "--mac-main-window-controls-probe")) {
        extern int wizMacWindowControlsSelfTest(int, char**);
        return wizMacWindowControlsSelfTest(argc, argv);
    }
    // A uniquely named local fixture bundle can only open isolated test data,
    // even if a UI inspection tool launches it without command-line arguments.
    if (QByteArray(argv[0]).endsWith("/WizWindowControlsTest")) {
        extern int wizMacWindowControlsSelfTest(int, char**);
        return wizMacWindowControlsSelfTest(argc, argv);
    }
#endif
#endif
    // Misspelled test/probe flags must never fall through to real account startup.
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        if (argument.startsWith("--") && (argument.contains("self-test") || argument.endsWith("-probe"))) {
            qCritical() << "Unknown or malformed test option; normal startup refused.";
            return 2;
        }
    }
    WizStandaloneStorage::Session session;
    if (WizBuildIdentity::appleSiliconTrial) {
        QString error;
        const QString overrideRoot = qEnvironmentVariableIsSet("WIZNOTE_DATA_STORE")
            ? qEnvironmentVariable("WIZNOTE_DATA_STORE") : QString();
        if (!session.prepare(QDir::homePath(), overrideRoot, error)) {
            QApplication errorApp(argc, argv);
            QMessageBox::critical(nullptr, QStringLiteral("无法启动独立资料库"), error);
            return 1; // No login, old-data fallback, profile or cleanup on failure.
        }
    }
    QString restartExecutable;
    int ret = mainCore(argc, argv, WizBuildIdentity::appleSiliconTrial ? &session : nullptr, restartExecutable);

    // clean up the last temporary files
    QString strTempPath = Utils::WizPathResolve::tempPath();
    ::WizDeleteAllFilesInFolder(strTempPath);
    session.release();
    if (!restartExecutable.isEmpty()) QProcess::startDetached(restartExecutable, QStringList());

    return ret;
}

