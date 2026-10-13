#ifndef WIZNOTEPRESENTATION_H
#define WIZNOTEPRESENTATION_H

#include <QString>
#include <functional>

class QSettings;
class QWidget;

namespace WizNotePresentation {
struct Style {
    QString screen;
    bool enabled = true;
};
Style loadStyle(QSettings* settings);
bool saveStyle(QSettings* settings, const Style& style, QString& error);
QString styleSheet(const Style& style);
QString jsonString(const QString& text);
QString mathBlockStyle();
// A live preview callback affects rendering only. Cancel restores the old style.
void editStyle(QWidget* parent, QSettings* settings,
               const std::function<void(const Style&)>& preview);
}

#endif
