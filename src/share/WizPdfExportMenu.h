#ifndef WIZ_PDF_EXPORT_MENU_H
#define WIZ_PDF_EXPORT_MENU_H

#include "WizPdfExport.h"
#include <QAction>
#include <QCoreApplication>
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
    menu->setToolTipsVisible(true);
    const QPointer<QAction> original(action);
    const auto add = [&](const QString& title, const QString& description, const char* name, Mode selected) {
        auto* child = menu->addAction(title);
        child->setToolTip(description);
        child->setObjectName(QString::fromLatin1(name));
        QObject::connect(child, &QAction::triggered, menu, [exportPdf, original, selected] {
            if (!original || !original->isEnabled()) return;
            exportPdf(selected);
        });
    };
    add(QCoreApplication::translate("WizPdfExport", "Reading Layout…"),
        QCoreApplication::translate("WizPdfExport", "Preserve the current reading width; add page margins outside the content."),
        "wizPdfReadingWidth", Mode::ReadingWidth);
    add(QCoreApplication::translate("WizPdfExport", "A4 Layout…"),
        QCoreApplication::translate("WizPdfExport", "Use A4 paper; page margins determine the available content area."),
        "wizPdfA4Width", Mode::A4);
    action->setMenu(menu);
}
}
#endif
