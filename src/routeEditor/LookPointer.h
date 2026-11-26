/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LOOKPOINTER_H
#define LOOKPOINTER_H

#include <QPoint>

// The walking look's pointer in the screen's own pixels. Qt's
// device-independent pixels fall between physical ones at fractional
// scaling (250 % on the Steam Deck), so a pointer placed at the view's
// middle lands up to a physical pixel off and every move read against that
// middle carried the error: the look drifted up or down (2026-11-26).
namespace LookPointer {
// The cursor's place in physical screen pixels; false where it is not known
// (not Windows; X11 is a placeholder in the .cpp), and the caller reads
// Qt's event positions instead.
bool position(QPoint *at);
}

#endif
