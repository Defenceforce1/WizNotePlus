#pragma once

#include "share/WizMarkdownExportRecovery.h"
#include <QDialog>

class QPlainTextEdit;
class QPushButton;
class QShowEvent;

// Dormant, single-use decision UI only. No filesystem, recovery, audit or
// network calls. Use open()/show(), not a nested exec(); callers must still
// validate the owner/document generation and actual user consent before work.
class WizMarkdownExportRecoveryPrompt final : public QDialog
{
    Q_OBJECT
public:
    explicit WizMarkdownExportRecoveryPrompt(
        const WizMarkdownExportRecovery::Inspection& inspection,
        WizMarkdownExportRecovery::LockState lock, QWidget* parent = nullptr);

    WizMarkdownExportRecovery::Decision decision() const { return m_decision; }
    bool hasDecision() const { return m_settled; }

    // UI-side hook for a pending prompt only. Does no I/O and does not cancel
    // work that has already received a published confirmation. The host still
    // owns asynchronous lifetime and actual save/quit coordination.
    void retainForExit();
    void accept() override;
    void reject() override;
    void done(int result) override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void settle(WizMarkdownExportRecovery::Decision decision);
    void toggleDetails();

    QPushButton* m_retain = nullptr;
    QPushButton* m_confirm = nullptr;
    QPushButton* m_detailButton = nullptr;
    QPlainTextEdit* m_details = nullptr;
    QString m_detailText;
    WizMarkdownExportRecovery::Decision m_decision = WizMarkdownExportRecovery::Decision::Retain;
    bool m_confirmAllowed = false;
    bool m_settled = false;
    bool m_published = false;
    bool m_exitRequested = false;
};
