#ifndef WIZLEGACYLIGHTAPPEARANCE_H
#define WIZLEGACYLIGHTAPPEARANCE_H

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

// The bundled skin has fixed light backgrounds and dark icons. Do not combine
// it with a system dark palette. This changes only this app, not macOS settings.
inline void WizApplyLegacyLightAppearance()
{
#if defined(Q_OS_MAC) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qApp->styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, QColor("#f5f5f5"));
    palette.setColor(QPalette::WindowText, QColor("#333333"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f3f3f3"));
    palette.setColor(QPalette::Text, QColor("#333333"));
    palette.setColor(QPalette::Button, QColor("#f0f0f0"));
    palette.setColor(QPalette::ButtonText, QColor("#333333"));
    palette.setColor(QPalette::Light, Qt::white);
    palette.setColor(QPalette::Midlight, QColor("#dfdfdf"));
    palette.setColor(QPalette::Mid, QColor("#b8b8b8"));
    palette.setColor(QPalette::Dark, QColor("#9f9f9f"));
    palette.setColor(QPalette::Shadow, QColor("#666666"));
    palette.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
    palette.setColor(QPalette::ToolTipText, QColor("#333333"));
    palette.setColor(QPalette::Highlight, QColor("#3377cc"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, QColor("#225fa8"));
    palette.setColor(QPalette::LinkVisited, QColor("#7040a0"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    palette.setColor(QPalette::PlaceholderText, QColor("#707070"));
#endif
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#767676"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#767676"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#767676"));
    qApp->setPalette(palette);
}
#endif
