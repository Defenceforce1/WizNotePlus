#include "WizMarkdownExportRecoveryApp.h"
#include "WizMarkdownExportRecoverySession.h"

#include <QCoreApplication>
#include <QThread>
#include <QVariant>
#include <QWidget>

namespace {
constexpr const char* quitDepth = "wizMarkdownRecoveryQuitDepth";
constexpr const char* quitCommitted = "wizMarkdownRecoveryQuitCommitted";
bool onApplicationThread()
{
    return QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}
}

namespace WizMarkdownExportRecoveryApp {

bool isQuitting()
{
    return !onApplicationThread()
        || QCoreApplication::instance()->property(quitDepth).toInt() > 0
        || QCoreApplication::instance()->property(quitCommitted).toBool();
}

WizMarkdownExportRecoverySession* open(QWidget* owner,
    WizDocumentCallbackEpoch::Ticket document, const QString& destination,
    std::function<void()> noSiteContinuation,
    std::function<void(const WizMarkdownExportRecovery::Result&)> result)
{
    if (isQuitting() || !owner || owner->thread() != QThread::currentThread()
        || !WizDocumentCallbackEpoch::current(document) || destination.isEmpty())
        return nullptr;
    auto* session = new WizMarkdownExportRecoverySession(owner, document, destination);
    const QPointer<QWidget> guard(owner);
    QObject::connect(session, &WizMarkdownExportRecoverySession::discovered, session,
        [guard, document, action = std::move(noSiteContinuation)](
            const WizMarkdownExportRecovery::Inspection& inspection,
            const WizMarkdownExportRecovery::LockInspection& lock) {
            using namespace WizMarkdownExportRecovery;
            if (guard && WizDocumentCallbackEpoch::current(document) && !isQuitting()
                && inspection.availability == Availability::None && lock.state == LockState::Available
                && action)
                action(); // May destroy owner/session. No later object access.
        });
    QObject::connect(session, &WizMarkdownExportRecoverySession::resultReady, session,
        [guard, document, action = std::move(result)](const WizMarkdownExportRecovery::Result& value) {
            if (guard && WizDocumentCallbackEpoch::current(document) && !isQuitting() && action)
                action(value);
        });
    QObject::connect(session, &WizMarkdownExportRecoverySession::settled, session, &QObject::deleteLater);
    return session; // Caller stores its weak handle before show can settle.
}

QuitScope::QuitScope()
{
    if (!onApplicationThread())
        return;
    m_application = QCoreApplication::instance();
    m_application->setProperty(quitDepth, m_application->property(quitDepth).toInt() + 1);
    QList<QPointer<WizMarkdownExportRecoverySession>> sessions;
    for (auto* session : m_application->findChildren<WizMarkdownExportRecoverySession*>(
             QString(), Qt::FindDirectChildrenOnly))
        sessions.append(session);
    for (const auto& session : sessions)
        if (session)
            session->beginQuit(); // A settled receiver may delete another session.
}

QuitScope::~QuitScope()
{
    if (m_application)
        m_application->setProperty(quitDepth, qMax(0, m_application->property(quitDepth).toInt() - 1));
}

void QuitScope::commit()
{
    if (m_application)
        m_application->setProperty(quitCommitted, true);
}

}
