/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeClock.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeAnimation.h>

#include <cmath>
#include <cstdint>

namespace ShapeClock {

bool handOf(const QString &name, Hand &hand) {
    static const struct {
        const char *prefix;
        Hand hand;
    } names[] = {{"orts_hhand_clock", Hand::Hour},
                 {"orts_mhand_clock", Hand::Minute},
                 {"orts_shand_clock", Hand::Second},
                 {"orts_chand_clock", Hand::SweepSecond}};
    for (const auto &n : names)
        if (name.startsWith(QLatin1String(n.prefix), Qt::CaseInsensitive)) {
            hand = n.hand;
            return true;
        }
    return false;
}

std::vector<HandPart> hands(const ComplexShape &shape) {
    std::vector<HandPart> found;
    if (!shape.isLoaded() || shape.animationFrames() <= 0.0f)
        return found;
    for (int part = 0; part < shape.partCount(); ++part) {
        HandPart hand;
        if (handOf(shape.partName(part), hand.hand)) {
            hand.part = part;
            found.push_back(hand);
        }
    }
    return found;
}

void quarter(Hand hand, double seconds, int &q, float &amount) {
    // Open Rails: the game time in whole hundredths of a second.
    double day = std::fmod(seconds, 86400.0);
    if (day < 0.0)
        day += 86400.0;
    std::int64_t time = std::int64_t(std::floor(day * 100.0));
    const int hour = int(time / 360000 % 24);
    time %= 360000;
    const int minute = int(time / 6000);
    time %= 6000;
    const int second = int(time / 100);
    const int centi = int(time - second * 100);
    switch (hand) {
    case Hand::SweepSecond:
        q = second / 15;
        amount = float(second - q * 15) / 15 + float(centi) / 100 / 15;
        break;
    case Hand::Second:
        q = second / 15;
        amount = float(second - q * 15) / 15;
        break;
    case Hand::Minute:
        q = minute / 15;
        amount = float(minute - q * 15) / 15;
        break;
    case Hand::Hour: {
        const int twelve = hour % 12;
        q = twelve / 3;
        amount = float(twelve - q * 3) / 3 + (1.0f / 3) * (float(minute) / 60);
        break;
    }
    }
}

float frame(const ComplexShape &shape, int part, Hand hand, double seconds) {
    const ShapeAnimation::Channel *channel = shape.partChannel(part);
    if (channel == nullptr)
        return -1.0f;
    // The keys picked by index: the rotation's, else the position's.
    const bool rotations = !channel->rotations.empty();
    const int count = int(rotations ? channel->rotations.size() : channel->positions.size());
    if (count < 2)
        return -1.0f;
    const auto keyFrame = [&](int k) {
        return rotations ? channel->rotations[size_t(k)].frame : channel->positions[size_t(k)].frame;
    };
    int q = 0;
    float amount = 0.0f;
    quarter(hand, seconds, q, amount);
    if (q < 0 || q + 1 > count - 1)
        q = 0;
    return keyFrame(q) + (keyFrame(q + 1) - keyFrame(q)) * amount;
}

}
