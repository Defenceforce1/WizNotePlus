bool WizDocumentWebView::acceptsLoadResult(const QString& kbGUID, const QString& guid, QPointer<QObject> requester, quint64 sequence) const
{
    return m_loadPending && m_loadSequence.accepts(this, requester, sequence)
        && m_loadDocument.strKbGUID == kbGUID && m_loadDocument.strGUID == guid
        && m_loadRequestCurrent && m_loadRequestCurrent() && m_loadReadPermit.current();
}

bool WizDocumentWebView::takeLoadResult(const QString& kbGUID, const QString& guid, QPointer<QObject> requester, quint64 sequence)
{
    if (!acceptsLoadResult(kbGUID, guid, requester, sequence)) return false;
    m_loadPending = false;
    return true;
}

void WizDocumentWebView::onDocumentReady(const QString kbGUID, const QString strGUID, const QString strFileName, WizEditorMode editorMode,
                                       QPointer<QObject> requester, quint64 sequence)
{
    if (!takeLoadResult(kbGUID, strGUID, requester, sequence)) return;
    const QPointer<WizDocumentWebView> self(this);
    const auto read = m_loadReadPermit;
    const auto current = m_loadRequestCurrent;
    const QPointer<WizDocumentView> host(view());
    const QPointer<QObject> source(page());
    const auto access = host->noteReadTicket();
    const auto opening = host->noteOpenSequence();
    const auto document = m_loadDocument;
    WIZDOCUMENTDATA doc;
    if (!m_dbMgr.db(kbGUID).documentFromGuid(strGUID, doc)) return;
    if (!self || !current || !current() || !read.current()
        || !m_loadSequence.accepts(this, requester, sequence)) return;
    m_mapFile.insert(strGUID, strFileName);
    WizDocumentCallbackEpoch::Ticket navigation;
    const bool dispatched = loadDocumentInWeb(editorMode, &navigation);
    // After navigation dispatch the DOM belongs to the renderer, even when a
    // synchronous loadStarted callback hides/relocks it. Never reload that DOM.
    if (dispatched || !self || !host || !source || view() != host || page() != source
        || !m_loadSequence.accepts(this, requester, sequence)
        || host->noteOpenSequence() != opening || host->note().strGUID != document.strGUID
        || host->note().strKbGUID != document.strKbGUID
        || !WizDocumentCallbackEpoch::current(navigation)
        || WizDocumentCallbackEpoch::current(access)) return;
    // Only an authorization interruption before dispatch is retryable here.
    // Keep the surviving navigation context; unrelated navigation cannot adopt it.
    if (!bindDocumentLoadOrigin(document, navigation)) return;
    m_loadReadPermit = {};
    m_loadPending = true;
    m_loadSequence.next();
    if (host->isVisible()) resumePendingDocumentLoad();
}
