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
#include <QStyleFactory>

TsreStyle::TsreStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {
}

int TsreStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                         QStyleHintReturn *returnData) const {
    if (hint == SH_ComboBox_Popup || hint == SH_ComboBox_AllowWheelScrolling)
        return 0;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
