#ifndef WIZNOTEPRESENTATION_H
#define WIZNOTEPRESENTATION_H

#include <QDialog>
#include <QPageLayout>
#include <QTemporaryDir>
#include <QUrl>
#include <functional>

class QSettings;
class QWebEngineView;
class QLabel;
class QPushButton;
class WizUserSettings;

namespace WizNotePresentation {
struct Style {
    QString screen;
    QString print;
    bool enabled = true;
};
Style loadStyle(QSettings* settings);
bool saveStyle(QSettings* settings, const Style& style, QString& error);
QString styleSheet(const Style& style);
QString jsonString(const QString& text);
QString choosePdfPath(QWidget* parent, const QString& suggestedName);
QPageLayout printLayout(WizUserSettings& settings);
// A live preview callback affects rendering only. Cancel restores the old style.
void editStyle(QWidget* parent, QSettings* settings,
               const std::function<void(const Style&)>& preview);
}

// Renders a separate copy; it never toggles or saves the active editor's mode.
class WizNotePrintDialog : public QDialog
{
public:
    WizNotePrintDialog(const QString& html, const QString& noteType, const QUrl& baseUrl,
                       const WizNotePresentation::Style& style, const QPageLayout& layout,
                       QWidget* parent = nullptr);
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    QWebEngineView* webView() const { return m_web; }
    QPageLayout pageLayout() const { return m_layout; }
    void writePdf(const QString& path, const std::function<void(bool, const QString&)>& done);
    void reject() override;
private:
    void choosePdf();
    void printNote();
    void setBusy(bool busy);
    QTemporaryDir m_copy;
    QWebEngineView* m_web;
    QLabel* m_status;
    QPushButton* m_pdf;
    QPushButton* m_print;
    QPageLayout m_layout;
    bool m_ready = false;
    bool m_busy = false;
    QString m_error;
};
#endif
