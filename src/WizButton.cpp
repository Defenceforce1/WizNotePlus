#include "WizButton.h"

#include <QStyleOptionToolButton>
#include <QPainter>
#include <QStyle>

#include "WizDef.h"
#include "share/WizMisc.h"
#include "share/WizSettings.h"


WizButton::WizButton(QWidget* parent)
    : QToolButton(parent)
{
}


QSize WizButton::sizeHint() const
{
    return QSize(16, 16);
}


void WizButton::setAction(QAction* action)
{
    setDefaultAction(action);
}

void WizButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QStyleOptionToolButton opt;
    initStyleOption(&opt);

    QPainter p(this);

    QIcon::Mode mode = opt.state & QStyle::State_Enabled ? QIcon::Normal : QIcon::Disabled;
    if (mode == QIcon::Normal && (opt.state & QStyle::State_HasFocus || opt.state & QStyle::State_Sunken))
        mode = QIcon::Active;
    QIcon::State state = QIcon::Off;
    if (opt.state & QStyle::State_On)
        state = QIcon::On;

    const QRect iconRect = QStyle::alignedRect(opt.direction, Qt::AlignCenter, opt.iconSize, opt.rect);
    opt.icon.paint(&p, iconRect, Qt::AlignCenter, mode, state);
}
