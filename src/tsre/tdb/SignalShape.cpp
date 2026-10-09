/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/SignalShape.h>

SignalShape::SignalShape() {
}

SignalShape::~SignalShape() {
    if (subObj != nullptr)
        for (int i = 0; i < iSubObj; i++)
            delete[] subObj[i].sigSubJnLinkIf;
    delete[] subObj;
}

QMap< QString, SignalShape::SigSubType > SignalShape::SigSubTypeStringToId = {
    { "UNDEFINED" , SignalShape::UNDEFINED },
    { "SIGNAL_HEAD" , SignalShape::SIGNAL_HEAD },
    { "NUMBER_PLATE" , SignalShape::NUMBER_PLATE },
    { "GRADIENT_PLATE" , SignalShape::GRADIENT_PLATE },
    { "USER1" , SignalShape::USER1 },
    { "USER2" , SignalShape::USER2 },
    { "USER3" , SignalShape::USER3 },
    { "USER4" , SignalShape::USER4 },
    { "DECOR" , SignalShape::DECOR }
};
