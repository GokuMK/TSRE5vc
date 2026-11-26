/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPECLOCK_H
#define SHAPECLOCK_H

#include <QString>
#include <vector>

class ComplexShape;

// Open Rails' analog clocks (task shapes 22; Open Rails: Scenery.cs and
// Shapes.cs AnalogClockShape). A shape whose first animation has a part
// named ORTS_HHand_Clock, ORTS_MHand_Clock, ORTS_SHand_Clock or
// ORTS_CHand_Clock (any case, any suffix: ORTS_HHand_Clock01, ...02 for a
// second face) shows the time of day with those parts. Each hand has five
// keys a quarter turn apart, from 12 o'clock round to 12 again; the time
// picks two neighbouring keys and how far between them.
namespace ShapeClock {

enum class Hand {
    Hour,        // ORTS_HHand_Clock: the hour and the minutes in it
    Minute,      // ORTS_MHand_Clock: a step each minute
    Second,      // ORTS_SHand_Clock: a step each second
    SweepSecond  // ORTS_CHand_Clock: the seconds and their hundredths
};

struct HandPart {
    int part = -1;
    Hand hand = Hand::Hour;
};

// The hand a part's name makes it; false for none.
bool handOf(const QString &name, Hand &hand);

// A loaded shape's hands, empty when it is no clock. A shape without an
// animation (frames) has none: Open Rails leaves such a clock at rest.
std::vector<HandPart> hands(const ComplexShape &shape);

// Open Rails' quarter of a hand's turn (its keys q and q + 1) and how far
// into it (0 to 1) at a time of day, seconds after midnight (any day).
void quarter(Hand hand, double seconds, int &q, float &amount);

// The frame of the part's keys that shows the time: key q's frame on to key
// q + 1's (its rotation keys, else its position keys; key 0 on when the hand
// has no key q + 1, as in Open Rails). -1 for a part with fewer than two
// keys.
float frame(const ComplexShape &shape, int part, Hand hand, double seconds);

}

#endif
