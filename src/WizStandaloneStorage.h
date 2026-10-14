#ifndef WIZSTANDALONESTORAGE_H
#define WIZSTANDALONESTORAGE_H

#include <QString>
#include <memory>
class QLockFile;
class QWidget;

namespace WizStandaloneStorage {
QString defaultRoot(const QString& home);
bool validateRoot(const QString& home, const QString& candidate, QString& normalized, QString& error);
QString profileStorage(const QString& root, const QString& profile);
QString profileCache(const QString& root, const QString& profile);

// Own the lock until the application, database and browser profile are closed.
class Session {
public:
    Session();
    ~Session();
    bool prepare(const QString& home, const QString& overrideRoot, QString& error);
    const QString& root() const { return m_root; }
    bool hasConsent() const;
    bool saveConsent(QString& error);
    void release();
private:
    QString m_root;
    std::unique_ptr<QLockFile> m_lock;
};
bool confirmFirstLaunch(Session& session, QWidget* parent = nullptr);
}
#endif
