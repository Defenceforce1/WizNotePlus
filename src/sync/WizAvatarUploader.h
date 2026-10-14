#ifndef WIZSERVICE_AVATARUPLOADER_H
#define WIZSERVICE_AVATARUPLOADER_H

#include <QObject>
#include <QByteArray>

class QString;
class QNetworkAccessManager;
class QNetworkReply;


class WizAvatarUploader : public QObject
{
    Q_OBJECT

public:
    explicit WizAvatarUploader(QObject *parent);
    void upload(const QString& strFileName);
    QString lastErrorMessage() const { return m_strError; }

private:
    QNetworkAccessManager* m_net;
    QString m_strUrl;
    QString m_strError;

    QByteArray convert2Avatar(const QString& strFileName);
    void upload_impl(const QString& strUrl, const QString& strToken, const QByteArray& avatar);
    void fail(const QString& message);
#ifdef WIZNOTE_NATIVE_TRANSFER_TESTS
    friend int wizNetworkSecuritySelfTest(int, char**);
#endif

Q_SIGNALS:
    void uploaded(bool ok);
};


#endif // WIZSERVICE_AVATARUPLOADER_H
