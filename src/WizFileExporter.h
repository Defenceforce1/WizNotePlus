//
// Created by pikachu on 2/16/2022.
//

#ifndef WIZNOTEPLUS_WIZFILEEXPORTER_H
#define WIZNOTEPLUS_WIZFILEEXPORTER_H

#include <QObject>
#include <functional>

class WizDatabaseManager;
struct WIZDOCUMENTDATA;
struct WIZDOCUMENTATTACHMENTDATAEX;

class WizFileExporter : public QObject {

    Q_OBJECT

public:
    explicit WizFileExporter(WizDatabaseManager& dbMgr, QObject *parent = nullptr);

    enum ExportFormat {
        Markdown = 1 << 0,
        HTML     = 1 << 1,
        MHTML    = 1 << 2,
        PDF      = 1 << 3
    };

    bool exportNote(const WIZDOCUMENTDATA &doc,
                    const QString &destFolder,
                    const ExportFormat format,
                    bool compress = false,
                    bool exportMetaInfo = true,
                    bool noTitleFolderIfPossible = false,
                    QString *errorMsg = nullptr);

    // Explicit transient output authority. Empty predicates deny; this is not
    // a password cache or a save receipt. The legacy signature stays available.
    bool exportNote(const WIZDOCUMENTDATA &doc, const QString &destFolder,
                    ExportFormat format, bool compress, bool exportMetaInfo,
                    bool noTitleFolderIfPossible, QString *errorMsg,
                    std::function<bool()> canRead);

    bool exportAttachment(const WIZDOCUMENTATTACHMENTDATAEX &att,
                          const QString &destFolder);
    bool exportAttachment(const WIZDOCUMENTATTACHMENTDATAEX &att,
                          const QString &destFolder, std::function<bool()> canRead);

private:
    bool extractMarkdownToFile(const QString &htmlContent, const QString &outputFile);
    bool writeDocumentInfoToJsonFile(const WIZDOCUMENTDATA &doc, const QString &outputFIle);
    bool compressDocumentFolder(const QString &folder, const std::function<bool()>& canRead,
                                bool removeSource = true);

private:
    WizDatabaseManager& m_dbMgr;
};


#endif //WIZNOTEPLUS_WIZFILEEXPORTER_H
