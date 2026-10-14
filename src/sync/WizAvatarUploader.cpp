#include "WizAvatarUploader.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QImageReader>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include "WizApiEntry.h"
#include "WizToken.h"
#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"

WizAvatarUploader::WizAvatarUploader(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{}

void WizAvatarUploader::fail(const QString& message)
{
    m_strError = message;
    Q_EMIT uploaded(false);
}

QByteArray WizAvatarUploader::convert2Avatar(const QString& fileName)
{
    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.width() > 100 || source.height() > 100)
        reader.setScaledSize(source.scaled(100, 100, Qt::KeepAspectRatioByExpanding));
    QImage image = reader.read();
    if (image.isNull()) return {};
    image = image.scaled(100, 100, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    image = image.copy((image.width() - 100) / 2, (image.height() - 100) / 2, 100, 100);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return bytes;
}

void WizAvatarUploader::upload(const QString& fileName)
{
    m_strError.clear();
    const QByteArray avatar = convert2Avatar(fileName);
    if (avatar.isEmpty()) {
        fail(QStringLiteral("无法读取头像图片，请选择有效的 PNG 或 JPEG 图片。"));
        return;
    }
    m_strUrl = WizCommonApiEntry::avatarUploadUrl();
    if (m_strUrl.isEmpty()) {
        fail(QStringLiteral("无法取得头像上传接口，请检查服务器地址与网络连接。"));
        return;
    }
    const QString token = WizToken::token();
    if (token.isEmpty()) {
        fail(QStringLiteral("无法取得登录凭据，请确认账号仍处于登录状态。"));
        return;
    }
    upload_impl(m_strUrl, token, avatar);
}

void WizAvatarUploader::upload_impl(const QString& url, const QString& token, const QByteArray& avatar)
{
    m_strError.clear();
    if (avatar.isEmpty() || token.isEmpty()) {
        fail(QStringLiteral("头像图片或登录凭据为空，未发送上传请求。"));
        return;
    }
    QNetworkRequest request{QUrl(url)};
    if (!WizNetworkSecurity::prepareAccountRequest(request)) {
        fail(QStringLiteral("头像接口必须使用有效的 HTTPS 地址，未发送上传请求。"));
        return;
    }
    // Never replay credentials or silently turn an avatar POST into a GET.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(20000);
#endif
    QHttpPart credential;
    credential.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    credential.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"token\"");
    credential.setBody(token.toUtf8());
    QHttpPart image;
    image.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
    image.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"avatar\"; filename=\"avatar.png\"");
    image.setBody(avatar);
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(credential); multipart->append(image);
    auto* reply = m_net->post(request, multipart);
    multipart->setParent(reply);
    WizAutoTimeOutEventLoop loop(reply);
    loop.setTimeoutWaitSeconds(20);
    loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = loop.error();
    if (loop.timeOut() || error == QNetworkReply::TimeoutError) {
        fail(QStringLiteral("头像上传超时。服务器是否已接收尚未确认，请稍后检查头像后再重试。"));
        return;
    }
    if (status >= 300 && status < 400) {
        fail(QStringLiteral("头像接口返回重定向（HTTP %1）。为保护登录凭据，未继续发送，请检查服务器接口。")
             .arg(status));
        return;
    }
    if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
        if (status >= 500) {
            fail(QStringLiteral("头像服务器或网关暂时无法处理请求（HTTP %1，网络错误码 %2）。"
                                "未更改本地头像；请稍后重试。")
                 .arg(status).arg(int(error)));
        } else if (status == 401 || status == 403) {
            fail(QStringLiteral("服务器拒绝头像上传权限（HTTP %1）。请检查登录状态或账号权限。")
                 .arg(status));
        } else if (error == QNetworkReply::SslHandshakeFailedError) {
            fail(QStringLiteral("头像服务器的 HTTPS 证书验证失败，未关闭安全验证。"));
        } else {
            fail(QStringLiteral("头像上传失败（HTTP %1，网络错误码 %2）。请检查网络和服务器接口。")
                 .arg(status > 0 ? QString::number(status) : QStringLiteral("未取得状态"))
                 .arg(int(error)));
        }
        return;
    }
    const QByteArray response = loop.result().trimmed();
    const auto document = QJsonDocument::fromJson(response);
    const auto code = document.object().value(QStringLiteral("returnCode"));
    // Standard Wiz JSON result plus the exact legacy plain-text success code.
    // An unrelated "200" substring is not proof that an avatar was saved.
    if (response != "200" && (!document.isObject() || !code.isDouble() || code.toDouble() != 200)) {
        fail(code.isDouble()
             ? QStringLiteral("头像服务拒绝保存图片（服务返回码 %1，HTTP %2）。").arg(code.toDouble()).arg(status)
             : QStringLiteral("头像服务未返回可确认的成功结果（HTTP %1）。本地头像未改动。")
               .arg(status));
        return;
    }
    Q_EMIT uploaded(true);
}
