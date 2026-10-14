#include "WizAvatarHost.h"
#include "WizAvatarHost_p.h"

#include <QThread>
#include <QImage>
#include <QTimer>
#include <QMutexLocker>
#include <QDebug>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QBuffer>
#include <QImageReader>
#include <QUrlQuery>
#include <QPixmap>
#include <QPixmapCache>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QBitmap>
#include <QApplication>
#include <algorithm>

#include "WizApiEntry.h"
#include "../utils/WizPathResolve.h"
#include "../utils/WizStyleHelper.h"

#include "../share/WizMisc.h"
#include "share/WizThreads.h"
#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"

/* ----------------------- AvatarDownloader ----------------------- */
WizAvatarDownloader::WizAvatarDownloader(QObject* parent)
    : QObject(parent)
    , m_net(nullptr)
{
}

void WizAvatarDownloader::download(const QString& userID, bool isSystemAvatar, const QString& userGUID)
{
    m_strCurrentUser = userID;
    const QString encodedID = QString::fromLatin1(QUrl::toPercentEncoding(userID));
    const QString strUrl = isSystemAvatar ? WizCommonApiEntry::systemAvatarUrl(encodedID)
        : !userGUID.isEmpty() ? WizCommonApiEntry::avatarDownloadUrlByGuid(userGUID)
                             : WizCommonApiEntry::avatarDownloadUrl(encodedID);
    if (strUrl.isEmpty()) {
        fetchUserAvatarEnd(false, QStringLiteral("无法获取头像下载地址；原头像已保留。"));
        return;
    }
    queryUserAvatar(strUrl);
}

void WizAvatarDownloader::queryUserAvatar(const QString& strUrl)
{
    if (!m_net) m_net = std::make_shared<QNetworkAccessManager>();
    QUrl url(strUrl);
    // Ignore stale intermediaries after a web-side change; no credentials here.
    QUrlQuery query(url);
    query.addQueryItem("_wizAvatarRefresh", QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    QNetworkRequest request(url);
    if (!WizNetworkSecurity::prepareAccountRequest(request, true)) {
        fetchUserAvatarEnd(false, QStringLiteral("头像下载地址不安全；原头像已保留。"));
        return;
    }
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Cache-Control", "no-cache");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(20000);
#endif
    QNetworkReply* reply = m_net->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 4 * 1024 * 1024 || total > 4 * 1024 * 1024) reply->abort();
    });
    WizAutoTimeOutEventLoop loop(reply);
    loop.setTimeoutWaitSeconds(20);
    loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (loop.error() != QNetworkReply::NoError || status != 200) {
        const QString detail = status ? QString("HTTP %1，网络错误码 %2").arg(status).arg(int(loop.error()))
                                      : QString("网络错误码 %1").arg(int(loop.error()));
        fetchUserAvatarEnd(false, QString("头像下载失败（%1）；原头像已保留。").arg(detail));
        return;
    }
    if (!save(m_strCurrentUser, loop.result())) {
        fetchUserAvatarEnd(false, QStringLiteral("头像服务未返回有效图片，或本地缓存无法写入；原头像已保留。"));
        return;
    }
    fetchUserAvatarEnd(true);
}

void WizAvatarDownloader::fetchUserAvatarEnd(bool bSucceed, const QString& error)
{
    if (!bSucceed) Q_EMIT failed(m_strCurrentUser, error);
    Q_EMIT downloaded(m_strCurrentUser, bSucceed);
}

bool WizAvatarDownloader::save(const QString& strUserGUID, const QByteArray& bytes)
{
    if (strUserGUID.isEmpty() || strUserGUID.contains('/') || strUserGUID.contains('\\')
        || strUserGUID.contains(QChar::Null) || bytes.size() > 4 * 1024 * 1024) return false;
    QBuffer buffer;
    buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096) return false;
    const QImage image = reader.read();
    if (image.isNull()) return false;
    QSaveFile file(Utils::WizPathResolve::avatarPath() + strUserGUID + ".png");
    return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
}


/* --------------------- AvatarHostPrivate --------------------- */

WizAvatarHostPrivate::WizAvatarHostPrivate(WizAvatarHost* avatarHost)
    : q(avatarHost)
    , m_downloader(new WizAvatarDownloader(this))
{
    connect(m_downloader, SIGNAL(downloaded(QString, bool)),
            SLOT(on_downloaded(QString, bool)));
    connect(m_downloader, &WizAvatarDownloader::failed, q, &WizAvatarHost::loadFailed);
    loadCacheDefault();
}

bool WizAvatarHostPrivate::isLoaded(const QString& strUserID)
{
    QPixmap pm;
    bool ret = QPixmapCache::find(keyFromUserID(strUserID), &pm);
    qDebug() << "[AvatarHost]search: " << keyFromUserID(strUserID) << "result:" << ret;
    return ret;
}

bool WizAvatarHostPrivate::isFileExists(const QString& strUserID)
{
    QString strFile = Utils::WizPathResolve::avatarPath() + strUserID + ".png";
    return QFile::exists(strFile);
}

bool WizAvatarHostPrivate::loadCache(const QString& strUserID)
{
    QString strFilePath = Utils::WizPathResolve::avatarPath() + strUserID + ".png";
    return loadCacheFromFile(keyFromUserID(strUserID), strFilePath);
}


QPixmap WizAvatarHostPrivate::loadOrg(const QString& strUserID)
{
    QString strFilePath = Utils::WizPathResolve::avatarPath() + strUserID + ".png";

    QPixmap ret(strFilePath);
    if (!ret.isNull())
        return ret;

    QString defaultFilePath = Utils::WizPathResolve::skinResourcesPath("default") + "avatar_default.png";
    return QPixmap(defaultFilePath);
}

void WizAvatarHostPrivate::addToDownloadList(const QString& strUserID, bool isSystem)
{
    appendUserID(strUserID, isSystem);

    download_impl();
}

bool WizAvatarHostPrivate::customSizeAvatar(const QString& strUserID, int width, int height, QString& strFilePath)
{
    strFilePath = Utils::WizPathResolve::tempPath() + strUserID + QString::number(width) + "x" + QString::number(height) + ".png";
    if (QFile::exists(strFilePath))
        return true;

    QPixmap orgPix = loadOrg(strUserID);
    if (orgPix.isNull())
        return false;

    QPixmap customPix = orgPix.scaled(width, height);
    return customPix.save(strFilePath);
}

void WizAvatarHostPrivate::loadCacheDefault()
{
    loadCacheFromFile(defaultKey(), Utils::WizPathResolve::skinResourcesPath("default") + "avatar_default.png");
}

bool WizAvatarHostPrivate::loadCacheFromFile(const QString& key, const QString& strFilePath)
{
    QFileInfo imageFile(strFilePath);

    if(!imageFile.exists()) {
        qDebug() << "[AvatarHost]file does not exist: " << strFilePath;
        return false;
    }


    QSize sz = Utils::WizStyleHelper::avatarSize();
    QPixmap pixmap = WizAvatarHost::circleImage(strFilePath, sz.width(), sz.height());

    if (pixmap.isNull())
        return false;

    Q_ASSERT(!pixmap.isNull());

    if (!QPixmapCache::insert(key, pixmap)) {
        qDebug() << "[AvatarHost]failed to insert cache: " << strFilePath;
        return false;
    }

    return true;
}

QString WizAvatarHostPrivate::keyFromUserID(const QString& strUserID) const
{
    if (strUserID.isEmpty())
        return defaultKey();

    return "Avatar::" + strUserID;
}

QString WizAvatarHostPrivate::defaultKey() const
{
    return "Avatar::Default";
}

bool WizAvatarHostPrivate::deleteAvatar(const QString& strUserID)
{
    qDebug() << "[AvatarHost]remove user avatar: " << strUserID;
    QPixmapCache::remove(keyFromUserID(strUserID));
    QString strAvatarPath = Utils::WizPathResolve::avatarPath();
    return WizDeleteFile(strAvatarPath + strUserID + ".png");
}

bool WizAvatarHostPrivate::avatar(const QString& strUserID, QPixmap* pixmap)
{
    if (QPixmapCache::find(keyFromUserID(strUserID), pixmap)) {
        return true;
    }

    if (!strUserID.isEmpty()) {
        load(strUserID, false);
    }

    if (QPixmapCache::find(defaultKey(), pixmap)) {
        return true;
    } else {
        loadCacheDefault();
    }

    if (QPixmapCache::find(defaultKey(), pixmap)) {
        return true;
    }

    Q_ASSERT(0);
    return false;
}

bool WizAvatarHostPrivate::systemAvatar(const QString& avatarName, QPixmap* pixmap)
{
    if (QPixmapCache::find(keyFromUserID(avatarName), pixmap)) {
        return true;
    }

    if (!avatarName.isEmpty()) {
        load(avatarName, true);
    }

    if (QPixmapCache::find(defaultKey(), pixmap)) {
        return true;
    } else {
        loadCacheDefault();
    }

    if (QPixmapCache::find(defaultKey(), pixmap)) {
        return true;
    }

    Q_ASSERT(0);
    return false;
}

QPixmap WizAvatarHostPrivate::orgAvatar(const QString& strUserID)
{
//    return loadOrg(strUserID, false);
    return loadOrg(strUserID);
}

QString WizAvatarHostPrivate::avatarFileName(const QString& strUserID)
{
    QString strFilePath = Utils::WizPathResolve::avatarPath() + strUserID + ".png";
    QFileInfo ret(strFilePath);
    if (ret.exists())
        return ret.absoluteFilePath();

    QString defaultFilePath = Utils::WizPathResolve::skinResourcesPath("default") + "avatar_default.png";
    return defaultFilePath;
}


//QPixmap AvatarHostPrivate::loadOrg(const QString& strUserID, bool bForce)
//{
//    if (bForce) {
//        if (!m_listUser.contains(strUserID) && strUserID != m_strCurrentDownloadingUser) {
//            m_listUser.append(strUserID);
//            m_thread->start(QThread::IdlePriority);
//        }
//    }
//    return loadOrg(strUserID);
//}

void WizAvatarHostPrivate::load(const QString& strUserID, bool isSystem)
{
    //
    QPixmap pm;
    if (!QPixmapCache::find(keyFromUserID(strUserID), &pm))
    {
        if (loadCache(strUserID))
        {
            Q_EMIT q->loaded(strUserID);
        }
        else
        {
            QString defaultFilePath = Utils::WizPathResolve::skinResourcesPath("default") + "avatar_default.png";
            loadCacheFromFile(keyFromUserID(strUserID), defaultFilePath);
            Q_EMIT q->loaded(strUserID);            

            // can find item, download from server
            addToDownloadList(strUserID, isSystem);
        }
    }
}

void WizAvatarHostPrivate::reload(const QString& strUserID)
{    
    addToDownloadList(strUserID, false);
}

void WizAvatarHostPrivate::setUserGuid(const QString& userID, const QString& userGUID)
{
    QMutexLocker locker(&m_mutex);
    if (!userID.isEmpty() && !userGUID.isEmpty()) m_userGuids.insert(userID, userGUID);
}

void WizAvatarHostPrivate::download_impl()
{
    if (!m_currentDownloadingUser.userID.isEmpty())
        return;

    peekUserID(m_currentDownloadingUser);

    if (m_currentDownloadingUser.userID.isEmpty())
    {
        qDebug() << "[AvatarHost]download pool is clean, thread: "
                 << QThread::currentThreadId();

        return;
    }

    const DownloadingUser user = m_currentDownloadingUser;
    QString guid;
    {
        QMutexLocker locker(&m_mutex);
        guid = m_userGuids.value(user.userID);
    }
    WizExecuteOnThread(WIZ_THREAD_NETWORK, [=](){
        m_downloader->download(user.userID, user.isSystemAvatar, guid);
    });
}

void WizAvatarHostPrivate::appendUserID(const QString& strUserID, bool isSystem)
{
    if (strUserID.isEmpty() || m_currentDownloadingUser.userID == strUserID) return;
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    foreach (DownloadingUser user, m_listUser)
    {
        if (user.userID == strUserID)
            return;
    }


    DownloadingUser user;
    user.userID = strUserID;
    user.isSystemAvatar = isSystem;
    m_listUser.append(user);
}

void WizAvatarHostPrivate::peekUserID(DownloadingUser& user)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    if (!m_listUser.isEmpty())
    {
        DownloadingUser topUser = m_listUser.takeFirst();
        user.userID = topUser.userID;
        user.isSystemAvatar = topUser.isSystemAvatar;
    }
}

void WizAvatarHostPrivate::on_downloaded(QString strUserID, bool bSucceed)
{
    if (bSucceed)
    {
        if (loadCache(strUserID)) Q_EMIT q->loaded(strUserID);
        else Q_EMIT q->loadFailed(strUserID, QStringLiteral("头像缓存无法显示，请稍后重试。"));
    }

    //  下载列表中的下一个头像
    m_currentDownloadingUser.userID.clear();
    download_impl();
}

/* --------------------- AvatarHost --------------------- */

static WizAvatarHostPrivate* d = 0;
static WizAvatarHost* m_instance = 0;

WizAvatarHost::WizAvatarHost()
{
    Q_ASSERT(!m_instance);

    m_instance = this;
    d = new WizAvatarHostPrivate(this);
}

WizAvatarHost::~WizAvatarHost()
{
    delete d;
    d = 0;
}

WizAvatarHost* WizAvatarHost::instance()
{
    return m_instance;
}

void WizAvatarHost::load(const QString& strUserID, bool isSystem)
{
    d->load(strUserID, isSystem);
}

void WizAvatarHost::reload(const QString& strUserID)
{
    if (QThread::currentThread() == d->thread()) d->reload(strUserID);
    else QMetaObject::invokeMethod(d, [strUserID] { d->reload(strUserID); }, Qt::QueuedConnection);
}

void WizAvatarHost::setUserGuid(const QString& userID, const QString& userGUID)
{
    d->setUserGuid(userID, userGUID);
}

// retrieve pixmap from cache, return default avatar if not exist
bool WizAvatarHost::avatar(const QString& strUserID, QPixmap* pixmap)
{
    return d->avatar(strUserID, pixmap);
}

bool WizAvatarHost::systemAvatar(const QString& avatarName, QPixmap* pixmap)
{
    return d->systemAvatar(avatarName, pixmap);
}

bool WizAvatarHost::deleteAvatar(const QString& strUserID)
{
    return d->deleteAvatar(strUserID);
}

QPixmap WizAvatarHost::orgAvatar(const QString& strUserID)
{
    return d->orgAvatar(strUserID);
}

QString WizAvatarHost::avatarFileName(const QString& strUserID)
{
    return d->avatarFileName(strUserID);
}

bool WizAvatarHost::isLoaded(const QString& strUserID)
{
    return d->isLoaded(strUserID);
}

bool WizAvatarHost::isFileExists(const QString& strUserID)
{
    return d->isFileExists(strUserID);
}

// For user want to retrive avatar from global pixmap cache
QString WizAvatarHost::keyFromUserID(const QString& strUserID)
{
    return d->keyFromUserID(strUserID);
}

// the default avatar's key for fallback drawing
QString WizAvatarHost::defaultKey()
{
    return d->defaultKey();
}

bool WizAvatarHost::customSizeAvatar(const QString& strUserID, int width, int height, QString& strFileName)
{
    return d->customSizeAvatar(strUserID, width, height, strFileName);
}

/*!
    Cut the image into a square
 */
QPixmap WizAvatarHost::corpImage(const QPixmap& org)
{
    // 将头像裁剪成正方形
    if (org.isNull())
        return org;

    QSize sz = org.size();

    int width = sz.width();
    int height = sz.height();
    if (width == height)
        return org;

    if (width > height)
    {
        int xOffset = (width - height) / 2;
        return org.copy(xOffset, 0, height, height);
    }
    else
    {
        int yOffset = (height - width) / 2;
        return org.copy(0, yOffset, width, width);
    }
}

/*!
    Crop the image \a src into a circle.

    Copied from \l {https://stefan.sofa-rockers.org
        /2018/05/04/how-to-mask-an-image-with-a-smooth-circle-in-pyqt5/}
    {How to mask an image with a smooth circle in PyQt5}
 */
QPixmap WizAvatarHost::circleImage(const QString& fileName, int width, int height)
{
    // Load image
    QImage image(fileName);

    if (image.isNull()) {
        qDebug() << "[AvatarHost]failed to load image: " << fileName;
        return QPixmap();
    }

    // Convert to 32-bit ARGB (adds an alpha channel):
    image.convertToFormat(QImage::Format_ARGB32);

    // Crop image to a square:
    int imgsize = std::min(image.width(), image.height());
    QRect rect = QRect(
        (image.width() - imgsize) / 2,
        (image.height() - imgsize) / 2,
        imgsize, imgsize
    );
    image = image.copy(rect);

    // Create the output image with the same dimensions and an alpha channel
    // and make it completely transparent:
    QImage out_img(imgsize, imgsize, QImage::Format_ARGB32);
    out_img.fill(Qt::transparent);

    // Create a texture brush and paint a circle with the original image onto
    // the output image:
    QBrush brush(image);                // Create texture brush
    QPainter painter(&out_img);         // Paint the output image
    painter.setBrush(brush);            // Use the image texture brush
    painter.setPen(Qt::NoPen);          // Don't draw an outline
    painter.setRenderHint(
        QPainter::Antialiasing, true);  // Use AA
    painter.drawEllipse(
        0, 0, imgsize, imgsize);        // Actually draw the circle
    painter.end();                      // We are done (segfault if you forget this)

    // Convert the image to a pixmap and rescale it.  Take pixel ratio into
    // account to get a sharp image on retina displays:
    auto pr = qApp->devicePixelRatio();
    auto pm = QPixmap::fromImage(out_img);
    pm.setDevicePixelRatio(pr);
    width *= pr;
    height *= pr;
    pm = pm.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return pm;
}

