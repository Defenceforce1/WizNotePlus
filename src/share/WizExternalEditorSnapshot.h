#pragma once
#include "WizQtCompat.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

namespace WizExternalEditorSnapshot {
struct Snapshot { QString html; QByteArray digest; bool ok=false; };
// Use one successful read for conversion and comparison. The UTF codec/text
// conversion is the existing importer contract, including BOM detection.
inline Snapshot read(const QString& path, bool plainText)
{
    if (!QFileInfo(path).isFile()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text)) return {};
    QString html=WizReadEncodedText(file,"UTF-8");
    if (file.error()!=QFileDevice::NoError) return {};
    if (plainText) {
        html=html.toHtmlEscaped();
        html.replace("\n","<br>");html.replace(" ","&nbsp;");
        html=QString("<!DOCTYPE html><html><head></head><body>%1</body></html>").arg(html);
    }
    return {html,QCryptographicHash::hash(html.toUtf8(),QCryptographicHash::Sha256),true};
}
}
