/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNALAMPS_H
#define MASZYNALAMPS_H

#include <QString>
#include <QVector>
#include <memory>
#include <tsre/trains/EngLights.h>

struct E3dModel;

// MaSzyna's outside lamps (task maszyna 07): its model's lamp parts, each a
// pair `<lamp>_on` and `<lamp>_off` (DynObj.cpp:2338-2370 at 66a940d4:
// headlamp11-13 the front's upper, right and left head lamps, headlamp21-23
// the rear's, highbeam12, 13, 22, 23, red markers endsignal12, 13, 22, 23 or
// endsignals1, 2 both at once, plates endtab1, 2), lit by TSRE's train
// rules or the driven loco's cab, and the lights they make for its car as an
// ENG's Lights block would (glows at the lamps, a cone per lit end), drawn
// by CarLights: a beam out of each head lamp.
namespace MaszynaLamps {

// MaSzyna's light bits (MOVER.h `light`).
enum Bit : quint32 {
    HeadLeft = 1u << 0,
    MarkerLeft = 1u << 1,
    HeadUpper = 1u << 2,
    HeadRight = 1u << 4,
    MarkerRight = 1u << 5,
    EndSignals = 1u << 6,
    HighLeft = 1u << 9,
    HighRight = 1u << 10,
};

struct Lamp {
    enum Kind { Head, HighBeam, Marker, Plate };
    QString name;       // its part's name without _on, _off ("headlamp11")
    int end = 1;        // 1 the vehicle's front, 2 its rear
    quint32 bits = 0;   // its light bits (a plate: EndSignals)
    Kind kind = Head;
    // Its centre in the car shape's space (MSTS: x right, y up, z forward).
    float position[3] = {0.0f, 0.0f, 0.0f};
};

struct Lamps {
    QVector<Lamp> lamps;    // at most 60
    // The .fiz's Headlights: its colour (ARGB) and its strengths.
    quint32 colour = 0xffffffffu;
    float dim = 0.6f, normal = 1.0f, high = 2.8f, highDim = 2.5f;
    // Which ends have markers that are lamps (plates then stay hidden).
    bool markerLamps[2] = {false, false};
};

// What the driven loco's cab says (its script's values, maszyna.lights):
// each end's lit bits, the bits it works (the others by the train's rules)
// and the dimming.
struct Cab {
    quint32 ends[2] = {0, 0};
    quint32 controlled = 0;
    bool dim = false;
    bool operator==(const Cab &o) const {
        return ends[0] == o.ends[0] && ends[1] == o.ends[1] && controlled == o.controlled && dim == o.dim;
    }
};

// The model's lamps; colour and strengths from the vehicle's .fiz
// Headlights: values (LampRed, LampGreen, LampBlue 0 to 255; negative: not
// given).
Lamps find(const E3dModel &model, const int rgb[3], float dim, float normal, float high, float highDim);

// The lit lamps, a bit each by their index, bit 63 dimmed: an end's head
// lamps lit on the first vehicle at its leading end with the headlights on,
// its red markers (plates without them) on the last at its trailing end,
// high beams never; the bits the cab works as it says.
quint64 lit(const Lamps &lamps, const EngLights::Train &train, const Cab &cab);
constexpr quint64 Dimmed = quint64(1) << 63;

// The car's lights for a lit set: a glow at each lit lamp, white or red,
// facing out of its end; a cone for each end with lit head lamps or high
// beams from their middle, the leading end's first (CarLights shines the
// first lit cone's beam out of each lit head lamp), its reach by the
// strength (high beams, dimmed, normal).
std::shared_ptr<EngLights> lights(const Lamps &lamps, quint64 lit, bool reversed);

// Whether lamp `index` is lit in a set.
inline bool isLit(quint64 set, int index) { return (set >> index) & 1u; }

}

#endif // MASZYNALAMPS_H
