#include "WizSearchReplaceWidget.h"
#include "ui_WizSearchReplaceWidget.h"

#include <QStyle>
#include <QPointer>

WizSearchReplaceWidget::WizSearchReplaceWidget(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::WizSearchReplaceWidget)
{
    ui->setupUi(this);

    // Disable auto default button
    ui->btn_pre->setAutoDefault(false);
    ui->btn_next->setAutoDefault(false);
    ui->btn_replace->setAutoDefault(false);
    ui->btn_replaceAll->setAutoDefault(false);
    ui->btn_close->setAutoDefault(false);

    //setWindowFlags(Qt::WindowStaysOnTopHint);  //could cause window fullscreen on mac
}

WizSearchReplaceWidget::~WizSearchReplaceWidget()
{
    emit findNext("", false);
    delete ui;
}

void WizSearchReplaceWidget::showInEditor(const QRect& rcEditor)
{
    showInEditor(rcEditor, [] { return true; });
}

void WizSearchReplaceWidget::showInEditor(const QRect& rcEditor, const std::function<bool()>& canShow)
{
    const QPointer<WizSearchReplaceWidget> self(this);
    const auto current = [self, canShow] {
        if (!self || !canShow) return false;
        const bool allowed = canShow();
        return self && allowed;
    };
    if (!current()) return;
    const QSize hint = sizeHint();
    if (!current()) return;
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignHCenter | Qt::AlignTop,
            hint,
            rcEditor
        )
    );
    if (!current()) return;

    setParent(parentWidget());
    if (!current()) return;
    show();
    if (!current()) return;
    setWindowState(windowState() & ~Qt::WindowFullScreen | Qt::WindowActive);
    if (!current()) return;
    setFixedSize(size());
    if (!current()) return;
    activateWindow();
    if (!current()) return;
    raise();
    //ui->lineEdit_source->setFocus();
}

void WizSearchReplaceWidget::setSourceText(const QString& text)
{
    ui->lineEdit_source->setText(text);
}

void WizSearchReplaceWidget::closeEvent(QCloseEvent* event)
{
    clearAllText();
    QWidget::closeEvent(event);
}

void WizSearchReplaceWidget::on_btn_pre_clicked()
{
    emit findPre(ui->lineEdit_source->text(), ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::on_btn_next_clicked()
{
    emit findNext(ui->lineEdit_source->text(), ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::on_btn_replace_clicked()
{
    emit replaceAndFindNext(ui->lineEdit_source->text(), ui->lineEdit_repalce->text(),
                            ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::on_btn_replaceAll_clicked()
{
    emit replaceAll(ui->lineEdit_source->text(), ui->lineEdit_repalce->text(),
                    ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::on_btn_close_clicked()
{
    close();
}

void WizSearchReplaceWidget::on_lineEdit_source_returnPressed()
{
    emit findNext(ui->lineEdit_source->text(), ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::on_lineEdit_source_textChanged(const QString &text)
{
    emit findNext(text, ui->checkBox_casesenitive->checkState() == Qt::Checked);
}

void WizSearchReplaceWidget::clearAllText()
{
    emit findNext("", false);
    ui->lineEdit_repalce->clear();
    ui->lineEdit_source->clear();
}
