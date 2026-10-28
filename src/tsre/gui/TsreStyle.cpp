/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/gui/TsreStyle.h>
#include <QAbstractSpinBox>
#include <QEvent>
#include <QStyleFactory>

TsreStyle::TsreStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {
}

int TsreStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                         QStyleHintReturn *returnData) const {
    if (hint == SH_ComboBox_Popup || hint == SH_ComboBox_AllowWheelScrolling)
        return 0;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

// Spin boxes have no style hint for the wheel: the style watches them. Nor
// does the wheel give them focus (a box focused in passing).
void TsreStyle::polish(QWidget *widget) {
    QProxyStyle::polish(widget);
    if (qobject_cast<QAbstractSpinBox *>(widget)) {
        widget->installEventFilter(this);
        if (widget->focusPolicy() == Qt::WheelFocus)
            widget->setFocusPolicy(Qt::StrongFocus);
    }
}

void TsreStyle::unpolish(QWidget *widget) {
    if (qobject_cast<QAbstractSpinBox *>(widget))
        widget->removeEventFilter(this);
    QProxyStyle::unpolish(widget);
}

// Ignored, a wheel event goes on to the box's parent, as QApplication passes
// an ignored one up: the panel behind it scrolls.
bool TsreStyle::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel && qobject_cast<QAbstractSpinBox *>(watched)) {
        event->ignore();
        return true;
    }
    return QProxyStyle::eventFilter(watched, event);
}
