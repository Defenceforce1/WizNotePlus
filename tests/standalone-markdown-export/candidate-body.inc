// Unaccepted source-flow prototype. Never linked into the main application.
void ExportHarness::saveAsMarkdown(QString& strIndexFileName, bool bSaveResource)
{
    Q_UNUSED(bSaveResource);
    if (strIndexFileName.isEmpty()) return;
    const WIZDOCUMENTDATA doc = view()->note();
    const auto ticket = m_documentCallbacks.ticket();
    const QPointer<ExportHarness> self(this);
    const QPointer<QWebEnginePage> sourcePage(page());
    if (!sourcePage) return;
    sourcePage->runJavaScript(QString("WizEditor.getMarkdownSrc({unEscapeHtml: true});"),
        [self, sourcePage, ticket, doc, strIndexFileName](const QVariant& result) {
        // The epoch is canceled before base QObject/page teardown begins.
        if (!WizDocumentCallbackEpoch::current(ticket) || !self || !sourcePage
            || self->page() != sourcePage || !self->acceptsDocumentCallback(ticket, doc)
            || result.userType() != QMetaType::QString) return;
        QString source = result.toString(); // A valid empty string is allowed.
        const QString fileTitle = Utils::WizMisc::extractFileTitle(strIndexFileName);
        source.replace("index_files/", fileTitle.toHtmlEscaped() + "_files/");
        // Keep the legacy valid-output deletion policy, including symlinks;
        // this prototype only changes when an invalid query may touch output.
        if (::WizPathFileExists(strIndexFileName)) ::WizDeleteFile(strIndexFileName);
        WizDatabase& db = self->m_dbMgr.db(doc.strKbGUID);
        if (!db.exportToHtmlFile(doc, strIndexFileName)) return;
        // No owner access after a collaborator that could run a nested loop.
        ::WizSaveUnicodeTextToUtf8File(strIndexFileName, source, false);
    });
}
