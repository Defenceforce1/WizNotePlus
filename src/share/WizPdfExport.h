#ifndef WIZPDFEXPORT_H
#define WIZPDFEXPORT_H

#include <QtGui/QPageLayout>
#include <QtGui/QPageSize>
#include <QtCore/QSaveFile>
#include <QtCore/QRectF>
#include <cmath>

namespace WizPdfExport {
enum class Mode { ReadingWidth = 0, A4 = 1 };
// Existing preference indexes: mm, point, inch, pica, didot, cicero.
inline QPageLayout pageLayout(int index, const QMarginsF& margins)
{
    if (index < 0 || index > 5 || !std::isfinite(margins.left()) || !std::isfinite(margins.top())
        || !std::isfinite(margins.right()) || !std::isfinite(margins.bottom())) return {};
    const QPageLayout::Unit units[] = {QPageLayout::Millimeter, QPageLayout::Point, QPageLayout::Inch,
        QPageLayout::Pica, QPageLayout::Didot, QPageLayout::Cicero};
    // Retain Qt's handling of negative margins, but reject empty printable areas.
    QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, margins, units[index]);
    if (layout.paintRect().width() <= 0 || layout.paintRect().height() <= 0) return {};
    return layout;
}

inline QPageLayout pageLayout(int index, const QMarginsF& margins, Mode selected, double cssWidth)
{
    auto layout = pageLayout(index, margins);
    if (!layout.isValid() || selected == Mode::A4) return layout;
    // Chromium's layout coordinates are CSS pixels (96/in), not device pixels.
    // Keep the reading viewport as the *printable* width, plus physical margins;
    // the sidebar/window chrome and devicePixelRatio must not enter this value.
    if (!std::isfinite(cssWidth) || cssWidth <= 0) return {};
    const auto points = layout.margins(QPageLayout::Point);
    const double width = cssWidth * 72.0 / 96.0 + points.left() + points.right();
    // Chromium PDF paper dimensions are limited to 200 inches. Refuse invalid
    // widths rather than silently clamp or fall back to a different mode.
    if (width > 14400) return {};
    const double height = layout.fullRect(QPageLayout::Point).height();
    layout.setPageSize(QPageSize(QSizeF(width, height), QPageSize::Point,
        QStringLiteral("Reading width"), QPageSize::ExactMatch));
    if (!layout.isValid() || layout.paintRect().width() <= 0 || layout.paintRect().height() <= 0) return {};
    return layout;
}

inline bool writeFile(const QString& destination, const QByteArray& pdf, QString& error)
{
    error.clear();
    // Sanity-check renderer completion, not a replacement for a PDF validator.
    if (pdf.size() < 16 || !pdf.startsWith("%PDF-") || !pdf.right(1024).trimmed().endsWith("%%EOF")) {
        error = QStringLiteral("未生成完整的 PDF 内容，原文件未被替换。");
        return false;
    }
    QSaveFile output(destination);
    output.setDirectWriteFallback(false); // Never truncate the old PDF in place.
    if (!output.open(QIODevice::WriteOnly)) { error = output.errorString(); return false; }
    if (output.write(pdf) != pdf.size()) {
        error = output.errorString(); output.cancelWriting(); return false;
    }
    if (!output.commit()) { error = output.errorString(); return false; }
    return true;
}
}
#endif
