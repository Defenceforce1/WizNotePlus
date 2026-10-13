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
QString scientificMathStyle()
{
    // Adapted from the user's Scientific AMS Theorems stylesheet. Notebooks-only
    // directives and its MathJax 3/4 script are intentionally not executed.
    return QStringLiteral(R"CSS(/* WizNotePlus scientific math environments v1 */
.wiz-editor-body.markdown-body {
  --math-blue: #405bb0;
  --math-blue-background: rgba(64,91,176,0.08);
  --math-blue-border: #4b69c6;
  --math-green: #287659;
  --math-green-background: rgba(40,118,89,0.08);
  --math-green-border: #33936f;
  --math-orange: #a05a18;
  --math-orange-background: rgba(160,90,24,0.08);
  --math-orange-border: #c4782c;
  --math-gray: #666666;
  --math-gray-background: rgba(100,100,100,0.07);
  --math-gray-border: #999999;
  --math-environment-radius: 6px;
  --math-environment-padding: 0.9em 1em;
  --math-environment-margin: 1.2em 0;
  counter-reset: theorem-counter definition-counter example-counter;
}
.wiz-editor-body.markdown-body :is(.theorem,.lemma,.proposition,.corollary,.definition,.example,.remark,.proof) {
  display: block; box-sizing: border-box;
  margin: var(--math-environment-margin);
  padding: var(--math-environment-padding);
  border-left: 4px solid var(--math-blue-border);
  border-radius: var(--math-environment-radius);
  background: var(--math-blue-background);
  color: #263950; line-height: 1.65;
  break-inside: avoid-page;
}
.wiz-editor-body.markdown-body :is(.theorem,.lemma,.proposition,.corollary) { counter-increment: theorem-counter; }
.wiz-editor-body.markdown-body .definition {
  counter-increment: definition-counter;
  border-color: var(--math-green-border); background: var(--math-green-background);
}
.wiz-editor-body.markdown-body .example {
  counter-increment: example-counter;
  border-color: var(--math-orange-border); background: var(--math-orange-background);
}
.wiz-editor-body.markdown-body :is(.remark,.proof) {
  border-left: 3px solid var(--math-gray-border); background: var(--math-gray-background);
}
.wiz-editor-body.markdown-body .proof { padding: 0.7em 0.9em; margin: 1em 0; background: transparent; }
.wiz-editor-body.markdown-body :is(.theorem,.lemma,.proposition,.corollary,.definition,.example,.remark,.proof)::before {
  display: block; font-weight: 700; color: var(--math-blue); margin-bottom: 0.45em;
}
.wiz-editor-body.markdown-body .theorem::before { content: "定理 " counter(theorem-counter) ". " attr(data-wiz-math-title); }
.wiz-editor-body.markdown-body .lemma::before { content: "引理 " counter(theorem-counter) ". " attr(data-wiz-math-title); }
.wiz-editor-body.markdown-body .proposition::before { content: "命题 " counter(theorem-counter) ". " attr(data-wiz-math-title); }
.wiz-editor-body.markdown-body .corollary::before { content: "推论 " counter(theorem-counter) ". " attr(data-wiz-math-title); }
.wiz-editor-body.markdown-body .definition::before {
  content: "定义 " counter(definition-counter) ". " attr(data-wiz-math-title); color: var(--math-green);
}
.wiz-editor-body.markdown-body .example::before {
  content: "例 " counter(example-counter) ". " attr(data-wiz-math-title); color: var(--math-orange);
}
.wiz-editor-body.markdown-body .remark::before { content: "注. " attr(data-wiz-math-title); color: var(--math-gray); }
.wiz-editor-body.markdown-body .proof::before { content: "证明. " attr(data-wiz-math-title); color: #263950; font-style: italic; }
.wiz-editor-body.markdown-body .proof::after { content: "□"; display: block; text-align: right; margin-top: 0.4em; line-height: 1; }
.wiz-editor-body.markdown-body .wiz-math-source-heading { display: none; }
.wiz-editor-body.markdown-body .wiz-math-block::before { content: attr(data-wiz-math-heading); }
.wiz-editor-body.markdown-body :is(.theorem,.lemma,.proposition,.corollary,.definition,.example,.remark,.proof) > :first-child { margin-top: 0; }
.wiz-editor-body.markdown-body :is(.theorem,.lemma,.proposition,.corollary,.definition,.example,.remark,.proof) > :last-child { margin-bottom: 0; }
.wiz-editor-body.markdown-body .MathJax_Display { max-width: 100%; margin: 0.8em 0; }
@media screen {
  .wiz-editor-body.markdown-body .MathJax_Display { overflow-x: auto; overflow-y: hidden; padding: 0.25em 0 0.4em; }
}
.wiz-editor-body.markdown-body a[id] { scroll-margin-top: 2em; }
.wiz-editor-body.markdown-body a { text-decoration-thickness: 0.07em; text-underline-offset: 0.14em; }
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
    auto* scientific = new QPushButton(QStringLiteral("插入分类数学样式（定理／定义／证明）"));
    scientific->setObjectName("wiz-note-css-scientific-preset");
    scientific->setToolTip(QStringLiteral("按类型设置颜色、边框和编号。追加到已有 CSS 后，点预览或保存才应用，不执行外部脚本。"));
    box->addWidget(scientific);
    QObject::connect(scientific, &QPushButton::clicked, &dialog, [screen, enabled] {
        if (!screen->toPlainText().contains("/* WizNotePlus scientific math environments v1 */"))
            screen->appendPlainText(scientificMathStyle());
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
