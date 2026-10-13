#include "WizNotePresentation.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace WizNotePresentation {
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
    // Retired printCss is neither used nor deleted; retain it for recoverability.
    style.enabled = settings->value("NotePresentation/enabled", true).toBool();
    return style;
}
bool saveStyle(QSettings* settings, const Style& style, QString& error)
{
    if (!settings || style.screen.size() > 65536) {
        error = QStringLiteral("CSS 最多 65536 个字符，且需要有效的本机设置。" ); return false;
    }
    const QStringList keys{"NotePresentation/screenCss", "NotePresentation/enabled"};
    QList<QVariant> previous;
    QList<bool> present;
    for (const auto& key : keys) { previous.append(settings->value(key)); present.append(settings->contains(key)); }
    settings->setValue("NotePresentation/screenCss", style.screen);
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
    // One stylesheet for the current page, including the original PDF exporter.
    return style.screen;
}
QString mathBlockStyle()
{
    // Ordinary Markdown blockquotes only. No parser extension, generated labels,
    // source conversion or blanket changes to MathJax's internal fonts.
    return QStringLiteral(R"CSS(/* WizNotePlus math-note blocks */
.wiz-editor-body.markdown-body blockquote {
  margin: 1em 0;
  padding: 0.8em 1em;
  border-left: 3px solid #5b7fa8;
  border-radius: 6px;
  background: #f2f6fb;
  color: #263950;
}
.wiz-editor-body.markdown-body blockquote > p:first-child { margin-top: 0; }
.wiz-editor-body.markdown-body blockquote > p:last-child { margin-bottom: 0; }
.wiz-editor-body.markdown-body blockquote > p:first-child > strong:first-child {
  display: block;
  color: #245387;
  font-weight: 700;
  margin-bottom: 0.4em;
}
.wiz-editor-body.markdown-body blockquote .MathJax_Display { margin: 0.8em 0; }
)CSS");
}
void editStyle(QWidget* parent, QSettings* settings, const std::function<void(const Style&)>& preview)
{
    const Style original = loadStyle(settings);
    QDialog dialog(parent);
    dialog.setObjectName("wiz-note-style-dialog");
    dialog.setWindowTitle(QStringLiteral("笔记正文 CSS"));
    dialog.resize(660, 500);
    auto* box = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QStringLiteral("只改变本机渲染，不修改原始笔记或 Markdown 编辑方式。\n沿用原来的另存为 PDF，同一份 CSS，不另设打印样式。\nCSS 请只使用可信来源；其中的外部资源链接可能联网。"));
    hint->setWordWrap(true); box->addWidget(hint);
    auto* enabled = new QCheckBox(QStringLiteral("启用自定义 CSS"));
    enabled->setObjectName("wiz-note-css-enabled"); enabled->setChecked(original.enabled); box->addWidget(enabled);
    auto* screen = new QPlainTextEdit;
    screen->setObjectName("wiz-note-screen-css");
    for (auto* edit : {screen}) {
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        edit->setTabStopDistance(edit->fontMetrics().horizontalAdvance(' ') * 4);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    screen->setPlainText(original.screen);
    screen->setPlaceholderText(QStringLiteral("/* 阅读/编辑正文的样式，不是整个 App 的主题 */\n.wiz-editor-body { line-height: 1.7; color: #263950; }\n.wiz-editor-body p { font-size: 16px; }"));
    box->addWidget(screen, 1);
    auto* math = new QPushButton(QStringLiteral("插入数学引用块样式"));
    math->setObjectName("wiz-note-css-math-preset");
    math->setToolTip(QStringLiteral("仅为 Markdown 阅读预览中的 > 引用块提供统一样式；首行可用 **定理名称**。只填入 CSS，点预览或保存才应用。不新增定理语法。"));
    box->addWidget(math);
    QObject::connect(math, &QPushButton::clicked, &dialog, [screen, enabled] {
        if (!screen->toPlainText().contains("/* WizNotePlus math-note blocks */"))
            screen->appendPlainText(mathBlockStyle());
        enabled->setChecked(true);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* test = buttons->addButton(QStringLiteral("预览到当前笔记"), QDialogButtonBox::ActionRole);
    auto* reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    test->setObjectName("wiz-note-css-preview"); reset->setObjectName("wiz-note-css-reset");
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); box->addWidget(buttons);
    const auto current = [&] { return Style{screen->toPlainText(), enabled->isChecked()}; };
    const auto valid = [&] {
        if (screen->toPlainText().size() <= 65536) return true;
        QMessageBox::warning(&dialog, QStringLiteral("CSS 太长"), QStringLiteral("每份 CSS 最多 65536 个字符。")); return false;
    };
    QObject::connect(test, &QPushButton::clicked, &dialog, [&] { if (valid()) preview(current()); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
        screen->clear(); enabled->setChecked(true); preview(current());
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
