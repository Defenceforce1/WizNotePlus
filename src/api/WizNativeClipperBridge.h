#pragma once

#include <QObject>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <functional>

class WizDatabaseManager;
class QTcpSocket;

// A narrow native-only loopback protocol. It exposes no database objects,
// filesystem paths, script evaluation, shell execution or cloud credentials.
class WizNativeClipperBridge : public QObject
{
    Q_OBJECT
public:
    WizNativeClipperBridge(WizDatabaseManager& database, QString tokenFile,
        std::function<bool()> unavailable, QObject* parent = nullptr);
    bool start(quint16 port = 8849);
    quint16 port() const { return m_server.serverPort(); }
    bool isSaving() const { return m_saving; }
    QString createPairingCode();
    static bool sanitizeHtml(const QString& html, const QString& sourceUrl,
        QString& clean, bool& remoteImages, QString& error);
    QJsonObject accountStatus() const;
signals:
    void noteSaved(QString guid);
private:
    WizDatabaseManager& m_database;
    QString m_tokenFile, m_token, m_pairingCode;
    std::function<bool()> m_unavailable;
    QTcpServer m_server;
    QElapsedTimer m_pairingDeadline;
    int m_failedPairings = 0, m_clients = 0;
    bool m_saving = false;
    void acceptConnections();
    void readRequest(QTcpSocket* socket);
    void dispatch(QTcpSocket* socket, const QByteArray& path,
        const QByteArray& authorization, const QJsonObject& body);
    void respond(QPointer<QTcpSocket> socket, int status, QJsonObject body);
    bool persistToken(const QString& token);
    QJsonObject saveClip(const QJsonObject& body, int& status);
};
