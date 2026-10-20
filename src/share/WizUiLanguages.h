#ifndef WIZ_UI_LANGUAGES_H
#define WIZ_UI_LANGUAGES_H
#include <QLocale>
#include <QTranslator>
#include <cstring>

inline QString wizSupportedUiLocale(const QString& locale)
{
    // Retain Chinese document/font support. Only the application UI catalog is
    // simplified; old Traditional UI preferences fall back without rewriting it.
    return QLocale(locale).language() == QLocale::Chinese ? QStringLiteral("zh_CN") : QStringLiteral("en_US");
}

class WizSettingsMenuTranslator final : public QTranslator {
public:
    QString locale;
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char* = nullptr, int = -1) const override
    {
        // Cocoa's PreferencesRole uses Qt's own title, not QAction::text().
        // Override that one application menu string, not the system language.
        if (context && source && !std::strcmp(context, "QMenuBar") && !std::strcmp(source, "Preferences..."))
            return wizSupportedUiLocale(locale) == "zh_CN" ? QStringLiteral("设置…") : QStringLiteral("Settings…");
        return {};
    }
};
#endif
