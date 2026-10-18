#pragma once
// Unit-only transport dependency. Never compiled into the normal account path:
// no URLs, sockets, credentials, stores or account discovery are available here.
#include <QString>
#include <QMetaType>
#include <atomic>
#include <functional>

struct WIZUSERINFO { QString strToken; };
Q_DECLARE_METATYPE(WIZUSERINFO)
namespace SyntheticAsyncApiTransport {
inline std::atomic<int> calls{0};
inline std::atomic_bool emptyToken{false};
// Set only while no request is running. The real lifetime logic remains in
// WizAsyncApi.cpp; only the server/token dependency is substituted.
inline std::function<bool(const QString&)> handler=[](const QString&){return false;};
inline bool request(const QString& operation) { ++calls; return handler(operation); }
}
class WizKMAccountsServer {
public:
    bool createAccount(const QString&,const QString&,const QString&,const QString&,const QString&)
        { return SyntheticAsyncApiTransport::request("register"); }
    bool setMessageReadStatus(const QString&,bool) { return SyntheticAsyncApiTransport::request("read"); }
    bool setMessageDeleteStatus(const QString&,bool) { return SyntheticAsyncApiTransport::request("delete"); }
    void setUserInfo(const WIZUSERINFO&) {}
    int getLastErrorCode() const { return 703; }
    QString getLastErrorMessage() const { return "synthetic transport failure"; }
};
class WizToken {
public:
    static QString token() { return SyntheticAsyncApiTransport::emptyToken ? QString() : QStringLiteral("synthetic-token"); }
    static WIZUSERINFO userInfo() { return {QStringLiteral("synthetic-token")}; }
};
