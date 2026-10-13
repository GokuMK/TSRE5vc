/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRACKSOUND_H
#define TRACKSOUND_H

#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

class SoundSource;
struct SoundValues;

// A car's wheels on the track: the route's track types (ttype.dat) each name
// a sound definition heard outside the car and one heard inside it; the
// type in use changes at the track's sound regions (TDB sound region
// items, each naming a type). Type 0 is the default track.
class TrackSound {
public:
    // car: the vehicle (the camera may ride it); inside: it has a cab or
    // interior, whose inside sound is heard riding it.
    TrackSound(const void *car, bool inside);
    ~TrackSound();

    void setType(int type);
    int type() const { return current; }
    void update(const SoundValues &values, float cutoff, int tileX, int tileZ, const float *position);

    // The route's track types: (inside, outside) .sms names in ttype.dat's
    // order.
    static QVector<QPair<QString, QString>> types();
    // Open Rails' choice from the sound regions around the leading car:
    // between two regions of one type, that type; next to a region of
    // another type ahead (within 10 m), the current one; otherwise the
    // default; with only one region near, its type; none, the current one.
    // Distances below 0: none found.
    static int regionType(int current, float behind, int behindType, float ahead, int aheadType);

private:
    const void *car;
    bool hasInside;
    int current = -1;
    std::unique_ptr<SoundSource> outside;
    std::unique_ptr<SoundSource> inside;
};

#endif
