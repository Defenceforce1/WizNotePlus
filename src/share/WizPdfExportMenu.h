#ifndef WIZ_PDF_EXPORT_MENU_H
#define WIZ_PDF_EXPORT_MENU_H

#include "WizPdfExport.h"
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <functional>

namespace WizPdfExport {
// Each command carries its own mode by value. No remembered setting or shared
// "current mode" can leak across cancellation, another tab or a nested dialog.
inline void attachMenu(QAction* action, QWidget* owner, const std::function<void(Mode)>& exportPdf)
{
    if (!action || !owner || !exportPdf || action->menu()) return;
    auto* menu = new QMenu(owner);
    menu->setObjectName(QStringLiteral("wizPdfPageWidthMenu"));
    const QPointer<QAction> original(action);
    const auto add = [&](const QString& title, const char* name, Mode selected) {
        auto* child = menu->addAction(title);
        child->setObjectName(QString::fromLatin1(name));
        QObject::connect(child, &QAction::triggered, menu, [exportPdf, original, selected] {
            if (!original || !original->isEnabled()) return;
            exportPdf(selected);
        });
    };
    add(QStringLiteral("保持正文宽度（电子阅读）…"), "wizPdfReadingWidth", Mode::ReadingWidth);
    add(QStringLiteral("A4 纸张宽度…"), "wizPdfA4Width", Mode::A4);
    action->setMenu(menu);
}
}
#endif
