// Frozen extracted pre-guard control; do not link into the main App.
void ExportHarness::saveAsMarkdown(QString& strIndexFileName, bool bSaveResource)
{
    Q_UNUSED(bSaveResource);
    if (strIndexFileName.isEmpty())
        return;
    //
    if (::WizPathFileExists(strIndexFileName))
    {
        ::WizDeleteFile(strIndexFileName);
    }
    // prepare note data
    const WIZDOCUMENTDATA& doc = view()->note();
    WizDatabase& db = m_dbMgr.db(doc.strKbGUID);
    // 导出HTML和资源文件到目标文件地址
    if (!db.exportToHtmlFile(doc, strIndexFileName)) {
        return;
    }
    // cannot be standard markdown for inner contanined table, todo, picture
    page()->runJavaScript(QString("WizEditor.getMarkdownSrc({unEscapeHtml: true});"), [=](const QVariant& vModified){
        //
        QString source = vModified.toString();
        //
        QString fileTitle = Utils::WizMisc::extractFileTitle(strIndexFileName);

        QString strResFolder = fileTitle.toHtmlEscaped() + "_files/";
        source.replace("index_files/", strResFolder);
        //
        ::WizSaveUnicodeTextToUtf8File(strIndexFileName, source, false);
    });
}



