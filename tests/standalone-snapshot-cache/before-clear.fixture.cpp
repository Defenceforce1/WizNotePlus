void WizDocumentWebView::clear()
{
    const QPointer<WizDocumentWebView> self(this);
    m_editorPageCallbacks.invalidate();
    m_editorPageResume = {};
    m_editorPageRestart = {};
    m_loadPending = false;
    m_loadRequestCurrent = {};
    m_loadOriginCurrent = {};
    m_loadReadPermit = {};
    m_loadSequence.next();
    const auto opening = invalidateDocumentCallbacks();
    if (!self || !WizDocumentCallbackEpoch::current(opening)) return;
    load(QUrl("about:blank"));
}
