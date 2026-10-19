#include "WizApiEntry.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QHostInfo>
#include <QLocale>
#include <QTime>
#include <QString>
#include <QDebug>
#include <QUrl>
#include <QMutexLocker>
#include <QUrlQuery>

#include "WizToken.h"
#include "WizKMServer.h"
#include "WizDef.h"
#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"
#include "share/jsoncpp/json/json.h"

/*
 * %1: product, use wiz
 * %2: locale, zh-cn|zh-tw|en-us
 * %3: version number
 * %4: command, see command list below
 * %5: ramdom number
 * %6: computer name, optional
 * %7: platform, ios|android|web|wp7|x86|x64|linux|macosx
 * %8: debug, true|false, optional
 */

#define WIZNOTE_API_ENTRY       "%1?p=%2&l=%3&v=%4&c=%5&random=%6&cn=%7&plat=%8&debug=%9"

#define WIZNOTE_API_ARG_PRODUCT "wiz"

#if defined(Q_OS_MAC)
#define WIZNOTE_API_ARG_PLATFORM "macosx"
#elif defined(Q_OS_WIN)
#define WIZNOTE_API_ARG_PLATFORM "windows"
#else
#define WIZNOTE_API_ARG_PLATFORM "linux"
#endif

// command list
#define WIZNOTE_API_COMMAND_AS_SERVER       "as"
#define WIZNOTE_API_COMMAND_SYNC_HTTP       "sync_http"
#define WIZNOTE_API_COMMAND_SYNC_HTTPS      "sync_https"
#define WIZNOTE_API_COMMAND_MESSAGE_SERVER       "message_server"
#define WIZNOTE_API_COMMAND_MESSAGE_VERSION "message_version"
#define WIZNOTE_API_COMMAND_AVATAR          "avatar"
#define WIZNOTE_API_COMMAND_UPLOAD_AVATAR   "upload_avatar"
#define WIZNOTE_API_COMMAND_USER_INFO       "user_info"
#define WIZNOTE_API_COMMAND_VIEW_GROUP      "view_group"
#define WIZNOTE_API_COMMAND_FEEDBACK        "feedback"
#define WIZNOTE_API_COMMAND_SUPPORT        "support"
#define WIZNOTE_API_COMMAND_COMMENT         "comment"
#define WIZNOTE_API_COMMAND_COMMENT_COUNT   "comment_count2"
#define WIZNOTE_API_COMMAND_CHANGELOG        "changelog"
#define WIZNOTE_API_COMMAND_UPGRADE        "updatev2"
#define WIZNOTE_API_COMMAND_MAIL_SHARE        "mail_share2"

//#ifdef _M_X64
//    QString strPlatform = "x64";
//#else
//    QString strPlatform = "x86";
//#endif


static QString LocalLanguage = QLocale::system().name();
QString WizCommonApiEntry::m_server = QString();
QMap<QString, QString> WizCommonApiEntry::m_cacheMap = QMap<QString, QString>();
WizRecursiveMutex WizCommonApiEntry::m_mutex;
quint64 WizCommonApiEntry::m_generation = 0;


QString _requestUrl(const QString& strUrl)
{
    QNetworkAccessManager net;
    QNetworkRequest request{QUrl(strUrl)};
    if (!WizNetworkSecurity::prepareAccountRequest(request)) return QString();
    QNetworkReply* reply = net.get(request);

    WizAutoTimeOutEventLoop loop(reply);
    loop.exec();

    if (loop.error() != QNetworkReply::NoError)
    {
        return QString();
    }

    //NOTE: reply has been delete in event loop, should not be deleted here
//    reply->deleteLater();


    return QString::fromUtf8(loop.result().constData());
}

QString addExtendedInfo(const QString& strUrl, const QString& strExt)
{
    QString strExtInfo = QString("&a=%1").arg(QUrl::toPercentEncoding(strExt).constData());
    return strUrl + strExtInfo;
}



void WizCommonApiEntry::setEnterpriseServerIP(const QString& strIP)
{
    if (strIP.isEmpty()) {
        qCritical() << "can not  set a empty server address";
        return;
    }
    QString normalized = strIP.startsWith("http") ? strIP : QString("http://%1/").arg(strIP);
    if (!normalized.endsWith("/")) normalized += "/";
    {
        QMutexLocker locker(&m_mutex);
        if (normalized == m_server) return;
        m_server = normalized;
        ++m_generation;
        m_cacheMap.clear();
    }
    qDebug() << "set server : " << normalized;
}

void WizCommonApiEntry::setLanguage(const QString& strLocal)
{
    QMutexLocker locker(&m_mutex);
    if (LocalLanguage == strLocal) return;
    LocalLanguage = strLocal;
    ++m_generation;
    m_cacheMap.clear();
}

WizCommonApiEntry::RequestContext WizCommonApiEntry::requestContext()
{
    QMutexLocker locker(&m_mutex);
    return {m_server, LocalLanguage, m_generation};
}

QString WizCommonApiEntry::server()
{
    return requestContext().server;
}

QString WizCommonApiEntry::language()
{
    return requestContext().language;
}

QString WizCommonApiEntry::syncUrl()
{
    QString strSyncUrl = getUrlByCommand("sync_https");

    if (!strSyncUrl.startsWith("http"))
    {
        qCritical() << "request url by command error. command : sync_https,  return : " << strSyncUrl;
        strSyncUrl.clear();
    }
    return strSyncUrl;
}

QString WizCommonApiEntry::asServerUrl()
{
    //使用endpoints获取as使用的API地址和之前的不同
    const auto context = requestContext();
    QString strAsUrl = getUrlFromCache("wizas", context);

    if (strAsUrl.isEmpty())
    {
        strAsUrl = requestUrl("as", context);
        updateUrlCache("wizas", strAsUrl, context);
    }

    if (!strAsUrl.startsWith("http"))
    {
        qCritical() << "request url by command error. command : sync_https,  return : " << strAsUrl;
        strAsUrl.clear();
    }


    return strAsUrl;
}

QString WizCommonApiEntry::newAsServerUrl()
{
    QString key = "account_server_url";
    QString strAsUrl = getUrlByCommand(key);
    //
    if (!strAsUrl.startsWith("http"))
    {
        qCritical() << "request url by command error. command : sync_https,  return : " << strAsUrl;
        strAsUrl = "https://as.wiz.cn";
    }

    return strAsUrl;
}


QString WizCommonApiEntry::messageServerUrl()
{    
#ifdef QT_DEBUG
    //return "http://localhost:5001/wizmessage";
#endif
    return getUrlByCommand(WIZNOTE_API_COMMAND_MESSAGE_SERVER);
}

QString WizCommonApiEntry::systemAvatarUrl(const QString& avatarName)
{
    return getUrlFromCache(avatarName, requestContext());
}

QString WizCommonApiEntry::avatarDownloadUrl(const QString& strUserGUID)
{
    QString strUrl = asServerUrl();
    if (strUrl.isEmpty())
        return QString();

    strUrl.append(QString("/a/users/avatar/%1").arg(strUserGUID));

    return strUrl;
}

QString WizCommonApiEntry::avatarDownloadUrlByGuid(const QString& userGUID)
{
    if (userGUID.isEmpty()) return QString();
    QString url = getUrlByCommand(WIZNOTE_API_COMMAND_AVATAR);
    if (!url.contains("{userGuid}")) return QString();
    url.replace("{userGuid}", QString::fromLatin1(QUrl::toPercentEncoding(userGUID)));
    return WizNetworkSecurity::allowsAccountUrl(QUrl(url)) ? url : QString();
}

QString WizCommonApiEntry::avatarUploadUrl()
{
    const QString url = getUrlByCommand(WIZNOTE_API_COMMAND_UPLOAD_AVATAR);
    return WizNetworkSecurity::allowsAccountUrl(QUrl(url)) ? url : QString();
}

QString WizCommonApiEntry::mailShareUrl(const QString& strKsServer, const QString& strMailInfo)
{
    // 通过endpoints获得api命令为mail_share，和之前使用的mail_share2不同，需要分开处理
    const auto context = requestContext();
    QString strMailShare = getUrlFromCache("mail_share", context);
    if (strMailShare.isEmpty())
    {
        strMailShare = requestUrl(WIZNOTE_API_COMMAND_MAIL_SHARE, context);
        updateUrlCache("mail_share", strMailShare, context);
    }

    QString strKSServer = strKsServer + "/wizks";
    strMailShare.replace("{server_url}", strKSServer);
    strMailShare.append(strMailInfo);
    return strMailShare;
}

QString WizCommonApiEntry::commentUrlTemplate()
{
    QString strCommentUrl = getUrlByCommand(WIZNOTE_API_COMMAND_COMMENT);
    return strCommentUrl;
}


QString WizCommonApiEntry::accountInfoUrl(const QString& strToken)
{
    QString strExt = QString("token=%1").arg(strToken);
    QString strUrl = makeUpUrlFromCommand(WIZNOTE_API_COMMAND_USER_INFO);
    return addExtendedInfo(strUrl, strExt);
}

QString WizCommonApiEntry::createGroupUrl(const QString& strToken)
{
    QUrl url = makeUpUrlFromCommand("create_group");
    QString extra = QString("token=%1&%2")
        .arg(strToken, WizCommonApiEntry::appstoreParam(false));

    QUrlQuery query(url.query());
    query.addQueryItem("a", QUrl::toPercentEncoding(extra).constData());
    query.removeQueryItem("plat");
    query.addQueryItem("plat", "macosx");
    url.setQuery(query);

    return url.toString();
}

QString WizCommonApiEntry::captchaUrl(const QString& strCaptchaID, int nWidth, int nHeight)
{
    QString strUrl = asServerUrl();
    strUrl += QString("/a/captcha/%1?width=%2&height=%3").arg(strCaptchaID).arg(nWidth).arg(nHeight);
    return strUrl;
}

QString WizCommonApiEntry::editStatusUrl()
{
    //使用endpoints获取edit使用的API地址和之前的不同
    const auto context = requestContext();
    QString strUrl = getUrlFromCache("edit_status", context);
    
    if (strUrl.isEmpty())
    {
        strUrl = requestUrl("note_edit_status_url", context);
        updateUrlCache("edit_status", strUrl, context);
    }
    
    return strUrl;
}

QString WizCommonApiEntry::shareServer()
{
    QString url = getUrlByCommand("share_server");
    return url;
}

QString WizCommonApiEntry::shareNoteUrl()
{
    QString url = getUrlByCommand("share_url");
    return url;
}

QString WizCommonApiEntry::svgEditorUrl()
{
    QString url = makeUpUrlFromCommand("svg_editor");

#ifdef Q_OS_WIN
    // Official WizNote windows client conflicts WizQTClient,
    // so we pretend to be a macosx client.
    QUrl urlobj(url);
    QUrlQuery query(urlobj.query());
    query.removeQueryItem("plat");
    query.addQueryItem("plat", "macosx");
    urlobj.setQuery(query);
    url = urlobj.toString();
#endif

    return url;
}

QString WizCommonApiEntry::noteplusUrl(
    const QString &kbGUID, const QString &docGUID,
    const QString &userGUID, const QString &displayName)
{
    QUrl url = makeUpUrlFromCommand("note_plus");

    QUrlQuery query(url.query());
    query.addQueryItem("a", QUrl::toPercentEncoding("theme=light&disableVideo=1&top=0").constData());
    query.addQueryItem("skin", "lite");
    // Mimic Android client
    query.addQueryItem("clientType", "win");
    query.addQueryItem("kbGuid", kbGUID);
    query.addQueryItem("docGuid", docGUID);
    query.addQueryItem("userGuid", userGUID);
    query.addQueryItem("displayName", displayName);

    url.setQuery(query);
    return url.toString();
}

QString WizCommonApiEntry::makeUpUrlFromCommand(const QString& strCommand, const QString& strToken)
{
    QString strExt = QString("token=%1").arg(strToken);
    QString strUrl = makeUpUrlFromCommand(strCommand);
    return addExtendedInfo(strUrl, strExt);
}

QString WizCommonApiEntry::makeUpUrlFromCommand(const QString& strCommand, const QString& strToken, const QString& strExtInfo)
{
    QString strExt = QString("token=%1").arg(strToken) + "&" + strExtInfo;
    QString strUrl = makeUpUrlFromCommand(strCommand);
    return addExtendedInfo(strUrl, strExt);
}

QString WizCommonApiEntry::newStandardCommandUrl(const QString& strCommand, const QString& strToken, const QString& strExt)
{
    QString strUrl = makeUpUrlFromCommand(strCommand);
    QString strExtInfo = QString("&token=%1").arg(strToken);
    strExtInfo.append(strExt.isEmpty() ? "" : "&" + strExt);
    return strUrl + strExtInfo;
}

QString WizCommonApiEntry::groupAttributeUrl(const QString& strToken, const QString& strKbGUID)
{
    QString strExt = QString("token=%1&kb_guid=%2").arg(strToken).arg(strKbGUID);
    QString strUrl = makeUpUrlFromCommand(WIZNOTE_API_COMMAND_VIEW_GROUP);
    return addExtendedInfo(strUrl, strExt);
}

QString WizCommonApiEntry::groupUsersUrl(const QString& strToken, const QString& strBizGUID, const QString& strkbGUID)
{
    QUrl url(syncUrl());
    QString strExt = QString("?token=%1&biz_guid=%2&kb_guid=%3&t=%4")
            .arg(strToken)
            .arg(strBizGUID)
            .arg(strkbGUID)
            .arg(QRandomGenerator::global()->generate());

    return url.scheme() + "://" + url.host() + "/wizas/a/biz/user_aliases" + strExt;
}

QString WizCommonApiEntry::appstoreParam(bool useAndSymbol)
{
    QString strParam = "";
#ifdef BUILD4APPSTORE
    if (useAndSymbol) {
        strParam = "&appstore=1";
    } else {
        strParam = "appstore=1";
    }
#endif

    return strParam;
}

QString WizCommonApiEntry::requestUrl(const QString& strCommand)
{
    return requestUrl(strCommand, requestContext());
}

QString WizCommonApiEntry::requestUrl(const QString& strCommand, const RequestContext& context)
{
    QString strRequestUrl= makeUpUrlFromCommand(strCommand, context);
    QString strUrl = _requestUrl(strRequestUrl);
    return strUrl;
}

QString WizOfficialApiEntry::appendSrc(QString url)
{
    QUrl u(url);
    QString host = u.host();
    //
    return url + "&srcHost=" + host;
}


QString WizCommonApiEntry::makeUpUrlFromCommand(const QString& strCommand)
{
    return makeUpUrlFromCommand(strCommand, requestContext());
}

QString WizCommonApiEntry::makeUpUrlFromCommand(const QString& strCommand, const RequestContext& context)
{
    // random seed
    QString strUrl = QString(WIZNOTE_API_ENTRY)
            .arg(context.server)
            .arg(WIZNOTE_API_ARG_PRODUCT)\
            .arg(context.language)\
            .arg(WIZ_CLIENT_VERSION)\
            .arg(strCommand)\
            .arg(QRandomGenerator::global()->generate())\
            .arg(QHostInfo::localHostName())\
            .arg(WIZNOTE_API_ARG_PLATFORM)\
            .arg("false");
    //
    strUrl = WizOfficialApiEntry::appendSrc(strUrl);
    return strUrl;
}

QMap<QString, QString> WizCommonApiEntry::getEndPoints(const RequestContext& context)
{
    // Never hold the global state lock across network I/O / its event loop.
    QString urls = requestUrl("endpoints", context);
#ifdef QT_DEBUG
    qDebug() << "get end points : " << urls;
#endif
    if (urls.isEmpty() || !urls.contains("http"))
        return {};

    Json::Value d;
    Json::Reader reader;
    if (!reader.parse(urls.toUtf8().constData(), d) || !d.isObject())
        return {};
    //
    Json::Value::Members keys = d.getMemberNames();

    QMap<QString, QString> endpoints;
    for(auto keyStr : keys)
    {
        if (!d[keyStr].isString()) continue;
        QString key = QString::fromStdString(keyStr);
        QString url = QString::fromStdString(d[keyStr].asString());
#ifdef QT_DEBUG
        qDebug() << "key: " << key << " url : " << url;
#endif
        endpoints.insert(key, url);
    }
    return endpoints;
}

void WizCommonApiEntry::updateUrlCache(const QString& strCommand, const QString& url, const RequestContext& context)
{
    QMutexLocker locker(&m_mutex);
    if (context.generation == m_generation) m_cacheMap.insert(strCommand, url);
}

QString WizCommonApiEntry::getUrlFromCache(const QString& strCommand, const RequestContext& context)
{
    {
        QMutexLocker locker(&m_mutex);
        if (context.generation == m_generation && !m_cacheMap.isEmpty())
            return m_cacheMap.value(strCommand);
    }
    const auto discovered = getEndPoints(context);
    {
        QMutexLocker locker(&m_mutex);
        if (context.generation == m_generation) {
            // Keep the first successful discovery for this generation, as a
            // serialized cache lookup did. A second overlapping discovery
            // must not overwrite endpoints already supplied in the meantime.
            if (m_cacheMap.isEmpty()) m_cacheMap = discovered;
            return m_cacheMap.value(strCommand);
        }
    }
    // An in-flight request keeps its own origin; stale results must not
    // populate the replacement origin's cache.
    return discovered.value(strCommand);
}

QString WizCommonApiEntry::getUrlByCommand(const QString& strCommand)
{
    const auto context = requestContext();
    QString strUrl = getUrlFromCache(strCommand, context);

    if (strUrl.isEmpty())
    {
        strUrl = requestUrl(strCommand, context);
        if (!strUrl.isEmpty() && strUrl.startsWith("http"))
            updateUrlCache(strCommand, strUrl, context);
    }

    return strUrl;
}

QString WizOfficialApiEntry::analyzerUploadUrl()
{
    QString analyzerUrl = requestUrl("analyzer");
    return analyzerUrl;
}

QString WizOfficialApiEntry::crashReportUrl()
{
    QString strUrl = requestUrl("crash_http");
    return strUrl;
}

QString WizOfficialApiEntry::standardCommandUrl(const QString& strCommand)
{
    QString strUrl = urlFromCommand(strCommand);
    return strUrl;
}

QString WizOfficialApiEntry::standardCommandUrl(const QString& strCommand, const QString& strToken)
{
    QString strExt = QString("token=%1").arg(strToken);
    QString strUrl = urlFromCommand(strCommand);
    return addExtendedInfo(strUrl, strExt);
}

QString WizOfficialApiEntry::standardCommandUrl(const QString& strCommand, const QString& strToken, const QString& strExtInfo)
{
    QString strExt = QString("token=%1").arg(strToken) + "&" + strExtInfo;
    QString strUrl = urlFromCommand(strCommand);
    return addExtendedInfo(strUrl, strExt);
}

QString WizOfficialApiEntry::requestUrl(const QString& strCommand)
{
    QString strRequestUrl= urlFromCommand(strCommand);
    QString strUrl = _requestUrl(strRequestUrl);
    if (!strUrl.startsWith("http"))
    {
        qCritical() << "request url by command error. command : " << strCommand << " return : " << strUrl;
        strUrl.clear();
    }
    return strUrl;
}

QString WizOfficialApiEntry::urlFromCommand(const QString& strCommand)
{
    QString strUrl = QString(WIZNOTE_API_ENTRY)
            .arg(WIZNOTE_API_SERVER)
            .arg(WIZNOTE_API_ARG_PRODUCT)\
            .arg(WizCommonApiEntry::language())\
            .arg(WIZ_CLIENT_VERSION)\
            .arg(strCommand)\
            .arg(QRandomGenerator::global()->generate())\
            .arg(QHostInfo::localHostName())\
            .arg(WIZNOTE_API_ARG_PLATFORM)\
            .arg("false");

    return strUrl;
}
