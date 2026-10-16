/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TSRESTYLE_H
#define TSRESTYLE_H

#include <QProxyStyle>

// TSRE's application style: Fusion, its combo boxes listing their items
// below them (what `combobox-popup: 0` set per box before) and not changed
// by the mouse wheel, which scrolls what is behind them.
class TsreStyle : public QProxyStyle {
public:
    TsreStyle();
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
};

#endif
