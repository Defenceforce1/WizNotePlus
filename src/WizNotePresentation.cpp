#include "WizNotePresentation.h"
#include "share/WizSettings.h"
#include "share/WizWebEngineView.h"
#include "utils/WizPathResolve.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <cmath>
#include <memory>

namespace WizNotePresentation {
QString choosePdfPath(QWidget* parent, const QString& suggestedName)
{
    QFileDialog dialog(parent, QStringLiteral("保存 PDF"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("PDF (*.pdf)"));
    // The dialog confirms overwriting the final suffixed name, not an earlier path.
    dialog.setDefaultSuffix(QStringLiteral("pdf"));
    dialog.selectFile(suggestedName);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().first();
}
QString jsonString(const QString& text)
{
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    // Also safe when placed inside an HTML script element.
    QString result = QString::fromUtf8(array.mid(1, array.size() - 2));
    result.replace('<', "\\u003c");
    result.replace(QChar(0x2028), "\\u2028"); result.replace(QChar(0x2029), "\\u2029");
    return result;
}
Style loadStyle(QSettings* settings)
{
    Style style;
    if (!settings) return style;
    style.screen = settings->value("NotePresentation/screenCss").toString();
    style.print = settings->value("NotePresentation/printCss").toString();
    style.enabled = settings->value("NotePresentation/enabled", true).toBool();
    return style;
}
bool saveStyle(QSettings* settings, const Style& style, QString& error)
{
    if (!settings || style.screen.size() > 65536 || style.print.size() > 65536) {
        error = QStringLiteral("每份 CSS 最多 65536 个字符，且需要有效的本机设置。" ); return false;
    }
    const QStringList keys{"NotePresentation/screenCss", "NotePresentation/printCss", "NotePresentation/enabled"};
    QList<QVariant> previous;
    QList<bool> present;
    for (const auto& key : keys) { previous.append(settings->value(key)); present.append(settings->contains(key)); }
    settings->setValue("NotePresentation/screenCss", style.screen);
    settings->setValue("NotePresentation/printCss", style.print);
    settings->setValue("NotePresentation/enabled", style.enabled);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        // Restore the process-local values too; a failed save must not apply on
        // switching notes. No note/database setting is involved.
        for (int i = 0; i < keys.size(); ++i) {
            if (present[i]) settings->setValue(keys[i], previous[i]);
            else settings->remove(keys[i]);
        }
        error = QStringLiteral("CSS 设置写入失败。" ); return false;
    }
    return true;
}
QString styleSheet(const Style& style)
{
    if (!style.enabled) return QString();
    return "@media screen {\n" + style.screen + "\n}\n@media print {\n" + style.print + "\n}";
}
QPageLayout printLayout(WizUserSettings& settings)
{
    // Legacy preferences use QPrinter::Unit. The former PDF route ignored it.
    const int unit = settings.printMarginUnit();
    const double factors[] = {1.0, 25.4 / 72.0, 25.4, 25.4 / 6.0, 0.3759, 4.511};
    const double factor = unit >= 0 && unit < 6 ? factors[unit] : 1.0;
    const auto margin = [&](WizPositionType side) {
        const double value = settings.printMarginValue(side) * factor;
        return std::isfinite(value) && value >= 0 && value <= 80 ? value : 5.0;
    };
    return QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
        QMarginsF(margin(wizPositionLeft), margin(wizPositionTop), margin(wizPositionRight), margin(wizPositionBottom)),
        QPageLayout::Millimeter);
}
void editStyle(QWidget* parent, QSettings* settings, const std::function<void(const Style&)>& preview)
{
    const Style original = loadStyle(settings);
    QDialog dialog(parent);
    dialog.setObjectName("wiz-note-style-dialog");
    dialog.setWindowTitle(QStringLiteral("笔记显示与打印 CSS"));
    dialog.resize(660, 500);
    auto* box = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QStringLiteral("只改变本机渲染，不修改原始笔记或 Markdown 编辑方式。\nCSS 请只使用可信来源；其中的外部资源链接可能联网。"));
    hint->setWordWrap(true); box->addWidget(hint);
    auto* enabled = new QCheckBox(QStringLiteral("启用自定义 CSS"));
    enabled->setObjectName("wiz-note-css-enabled"); enabled->setChecked(original.enabled); box->addWidget(enabled);
    auto* tabs = new QTabWidget;
    auto* screen = new QPlainTextEdit; auto* print = new QPlainTextEdit;
    screen->setObjectName("wiz-note-screen-css"); print->setObjectName("wiz-note-print-css");
    for (auto* edit : {screen, print}) {
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        edit->setTabStopDistance(edit->fontMetrics().horizontalAdvance(' ') * 4);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    screen->setPlainText(original.screen); print->setPlainText(original.print);
    screen->setPlaceholderText(QStringLiteral("/* 阅读/编辑正文的样式，不是整个 App 的主题 */\nbody { line-height: 1.7; }\n.markdown-body { max-width: 900px; margin: auto; }"));
    print->setPlaceholderText(QStringLiteral("/* 自动用于打印和 PDF */\nbody { font-size: 11pt; line-height: 1.5; }\nh1, h2, h3 { break-after: avoid; }\n.MathJax_Display { break-inside: avoid; }"));
    tabs->addTab(screen, QStringLiteral("屏幕 CSS")); tabs->addTab(print, QStringLiteral("打印 CSS")); box->addWidget(tabs, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* test = buttons->addButton(QStringLiteral("预览到当前笔记"), QDialogButtonBox::ActionRole);
    auto* reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    test->setObjectName("wiz-note-css-preview"); reset->setObjectName("wiz-note-css-reset");
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); box->addWidget(buttons);
    const auto current = [&] { return Style{screen->toPlainText(), print->toPlainText(), enabled->isChecked()}; };
    const auto valid = [&] {
        if (screen->toPlainText().size() <= 65536 && print->toPlainText().size() <= 65536) return true;
        QMessageBox::warning(&dialog, QStringLiteral("CSS 太长"), QStringLiteral("每份 CSS 最多 65536 个字符。")); return false;
    };
    QObject::connect(test, &QPushButton::clicked, &dialog, [&] { if (valid()) preview(current()); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
        screen->clear(); print->clear(); enabled->setChecked(true); preview(current());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!valid()) return;
        QString error;
        if (!saveStyle(settings, current(), error)) { QMessageBox::warning(&dialog, QStringLiteral("未保存"), error); return; }
        dialog.accept();
    });
    const bool saved = dialog.exec() == QDialog::Accepted;
    preview(saved ? loadStyle(settings) : original);
}
}

WizNotePrintDialog::WizNotePrintDialog(const QString& html, const QString& noteType, const QUrl& baseUrl,
    const WizNotePresentation::Style& style, const QPageLayout& layout, QWidget* parent)
    : QDialog(parent), m_web(new QWebEngineView(this)), m_status(new QLabel(this)),
      m_pdf(new QPushButton(QStringLiteral("保存 PDF…"), this)),
      m_print(new QPushButton(QStringLiteral("打印…"), this)), m_layout(layout)
{
    setObjectName("wiz-note-print-dialog"); setWindowTitle(QStringLiteral("笔记打印 / PDF")); resize(850, 720);
    auto* box = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("这是独立渲染副本，不会切换编辑模式或改写笔记。预览不分页，最终分页以 PDF / 打印结果为准。"));
    hint->setWordWrap(true); box->addWidget(hint);
    m_web->setPage(new QWebEnginePage(WizWebEngineProfile(), m_web));
    box->addWidget(m_web, 1); box->addWidget(m_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    m_pdf->setObjectName("wiz-note-print-pdf"); m_print->setObjectName("wiz-note-print-native");
    buttons->addButton(m_pdf, QDialogButtonBox::ActionRole); buttons->addButton(m_print, QDialogButtonBox::ActionRole);
    box->addWidget(buttons); setBusy(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &WizNotePrintDialog::reject);
    connect(m_pdf, &QPushButton::clicked, this, &WizNotePrintDialog::choosePdf);
    connect(m_print, &QPushButton::clicked, this, &WizNotePrintDialog::printNote);
    m_status->setText(QStringLiteral("正在渲染公式和图片…"));

    const QString resource = Utils::WizPathResolve::resourcesPath() + "files/wizeditor/";
    QString copy = html;
    QRegularExpression head("<head[^>]*>", QRegularExpression::CaseInsensitiveOption);
    const auto match = head.match(copy);
    if (!match.hasMatch() || !m_copy.isValid()) { m_error = QStringLiteral("无法创建笔记打印副本。" ); m_status->setText(m_error); return; }
    copy.insert(match.capturedEnd(), "<base href=\"" + baseUrl.toString().toHtmlEscaped() + "\">");
    const QString boot = "<script src=\"" + QUrl::fromLocalFile(resource + "wizEditorForMac.js").toString().toHtmlEscaped()
        + "\"></script><script>window.WizPrintConfig={type:" + WizNotePresentation::jsonString(noteType)
        + ",dependency:" + WizNotePresentation::jsonString(QUrl::fromLocalFile(resource + "dependency").toString())
        + ",css:" + WizNotePresentation::jsonString(WizNotePresentation::styleSheet(style)) + "};</script>"
        + "<script src=\"" + QUrl::fromLocalFile(resource + "printPreview.js").toString().toHtmlEscaped() + "\"></script>";
    const int end = copy.lastIndexOf("</body>", -1, Qt::CaseInsensitive);
    if (end < 0) { m_error = QStringLiteral("笔记正文无效，不能打印。" ); m_status->setText(m_error); return; }
    copy.insert(end, boot);
    const QString path = m_copy.filePath("print.html");
    QSaveFile file(path);
    const QByteArray data = copy.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        m_error = QStringLiteral("打印副本写入失败。" ); m_status->setText(m_error); return;
    }
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
    const QPointer<WizNotePrintDialog> guard(this);
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, guard] {
        if (elapsed->elapsed() > 60000) {
            timer->stop(); m_error = QStringLiteral("公式或图片渲染超时，尚未打印。" ); m_status->setText(m_error); return;
        }
        m_web->page()->runJavaScript("({ready:!!window.WizPrintReady,error:window.WizPrintError||''})", [guard, timer](const QVariant& value) {
            if (!guard) return;
            const QVariantMap state = value.toMap();
            if (!state.value("error").toString().isEmpty()) {
                timer->stop(); guard->m_error = state.value("error").toString(); guard->m_status->setText(guard->m_error);
            } else if (state.value("ready").toBool()) {
                timer->stop(); guard->m_ready = true; guard->setBusy(false);
                guard->m_status->setText(QStringLiteral("副本已就绪。默认 A4；打印对话框可选择纸张、方向和打印机。"));
            }
        });
    });
    connect(m_web, &QWebEngineView::loadFinished, this, [this, timer](bool ok) {
        if (!ok) { timer->stop(); m_error = QStringLiteral("打印副本加载失败。" ); m_status->setText(m_error); }
    });
    m_web->load(QUrl::fromLocalFile(path)); timer->start(100);
}
void WizNotePrintDialog::setBusy(bool busy)
{
    m_busy = busy; m_pdf->setEnabled(m_ready && !busy); m_print->setEnabled(m_ready && !busy);
}
void WizNotePrintDialog::reject() { if (!m_busy) QDialog::reject(); }
void WizNotePrintDialog::writePdf(const QString& path, const std::function<void(bool, const QString&)>& done)
{
    if (!m_ready || m_busy) { done(false, QStringLiteral("渲染未完成或已有打印任务。")); return; }
    setBusy(true); m_status->setText(QStringLiteral("正在生成 PDF…"));
    const QPointer<WizNotePrintDialog> guard(this);
    m_web->page()->printToPdf([guard, path, done](const QByteArray& bytes) {
        if (!guard) { done(false, QStringLiteral("打印窗口已关闭。")); return; }
        bool ok = bytes.startsWith("%PDF-"); QString error;
        if (ok) {
            QSaveFile file(path);
            ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
        }
        if (!ok) error = QStringLiteral("PDF 生成或写入失败；未预先删除目标文件。" );
        guard->setBusy(false); guard->m_status->setText(ok ? QStringLiteral("PDF 已保存：") + path : error);
        done(ok, error);
    }, m_layout);
}
void WizNotePrintDialog::choosePdf()
{
    const QString path = WizNotePresentation::choosePdfPath(this, QStringLiteral("笔记.pdf"));
    if (path.isEmpty()) return;
    const QPointer<WizNotePrintDialog> guard(this);
    writePdf(path, [guard](bool ok, const QString& error) { if (guard && !ok) QMessageBox::warning(guard, QStringLiteral("未保存"), error); });
}
void WizNotePrintDialog::printNote()
{
    auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    printer->setPageLayout(m_layout);
    QPrintDialog dialog(printer.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    setBusy(true); m_status->setText(QStringLiteral("正在提交打印任务…"));
    const QPointer<WizNotePrintDialog> guard(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_web, &QWebEngineView::printFinished, m_web, [guard, printer, connection](bool ok) {
        QObject::disconnect(*connection);
        if (!guard) return;
        guard->setBusy(false); guard->m_status->setText(ok ? QStringLiteral("打印任务已提交。") : QStringLiteral("打印失败，请检查打印机设置。"));
    });
    m_web->print(printer.get());
#else
    m_web->page()->print(printer.get(), [guard, printer](bool ok) {
        if (!guard) return;
        guard->setBusy(false); guard->m_status->setText(ok ? QStringLiteral("打印任务已提交。") : QStringLiteral("打印失败，请检查打印机设置。"));
    });
#endif
}
