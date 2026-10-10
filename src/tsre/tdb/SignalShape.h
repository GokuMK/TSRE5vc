/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIGNALSHAPE_H
#define	SIGNALSHAPE_H

#include <QMap>
#include <QString>

// A signal shape of sigcfg.dat and its sub-objects; read by SigCfg.
class SignalShape {
public:
    enum SigSubType {
        UNDEFINED = 0,
        SIGNAL_HEAD = 1,
        NUMBER_PLATE = 2,
        GRADIENT_PLATE = 4,
        USER1 = 5,
        USER2 = 6,
        USER3 = 7,
        USER4 = 8,
        DECOR = 9
    };

    // Upper-case names.
    static QMap< QString, SigSubType > SigSubTypeStringToId;

    struct SubObj {
        // Matrix name in the shape.
        QString type;
        QString desc;
        // Upper case.
        QString sigSubType;
        int* sigSubJnLinkIf = nullptr;
        int iLink = 0;
        // Signal type name as written (track items store it).
        QString sigSubSType;
        bool isJnLink = false;
        bool optional = false;
        bool defaultt = false;
        bool backFacing = false;
        int sigSubTypeId = 0;
        // Index among the front-facing or the back-facing sub-objects.
        int faceidx = 0;
    };
    int listId = 0;
    int iSubObj = 0;
    SubObj* subObj = nullptr;
    QString name;
    QString desc;

    SignalShape();
    SignalShape(const SignalShape &) = delete;
    SignalShape &operator=(const SignalShape &) = delete;
    virtual ~SignalShape();
};

#endif	/* SIGNALSHAPE_H */
