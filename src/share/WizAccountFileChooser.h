#ifndef WIZ_ACCOUNT_FILE_CHOOSER_H
#define WIZ_ACCOUNT_FILE_CHOOSER_H
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>

// Narrow account-web upload compatibility path. The website still receives
// files through Chromium's normal chooser contract, never by assigning an input
// value, injecting a file, or relaxing browser filesystem permissions.
inline QStringList wizAccountUploadFiles(QObject* requestOwner, QWidget* parent, bool multiple,
                                         const QStringList& acceptedTypes)
{
    QPointer<QObject> owner(requestOwner);
    QPointer<QFileDialog> dialog(new QFileDialog(parent, QObject::tr("Select an image")));
    dialog->setObjectName("wiz-account-file-picker");
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->setFileMode(multiple ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
    QStringList patterns;
    QMimeDatabase mime;
    for (const auto& type : acceptedTypes) {
        if (type == "image/*") {
            for (const auto& supported : QImageReader::supportedMimeTypes())
                patterns.append(mime.mimeTypeForName(QString::fromLatin1(supported)).globPatterns());
        } else if (type.startsWith('.') && !type.contains('/') && !type.contains(';') && !type.contains(' ')) {
            patterns.append('*' + type);
        } else {
            patterns.append(mime.mimeTypeForName(type).globPatterns());
        }
    }
    patterns.removeDuplicates();
    if (!patterns.isEmpty()) dialog->setNameFilter(QObject::tr("Supported files (%1)").arg(patterns.join(' ')));
    QObject::connect(requestOwner, &QObject::destroyed, dialog, &QDialog::reject);
    const int result = dialog->exec();
    QStringList files;
    if (owner && dialog && result == QDialog::Accepted) {
        for (const auto& selected : dialog->selectedFiles()) {
            const QFileInfo file(selected);
            if (file.isAbsolute() && file.isFile() && file.isReadable()) files.append(file.absoluteFilePath());
        }
        if (!multiple && files.size() > 1) files.clear();
    }
    if (dialog) delete dialog;
    return files;
}
#endif
