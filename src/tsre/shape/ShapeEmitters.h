/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEEMITTERS_H
#define SHAPEEMITTERS_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <tsre/trains/CarExhaust.h>

class ComplexShape;
class FileBuffer;
namespace SFileDetail {
struct Node;
}

// A shape's emitters (task shapes 15): its .sd's ESD_TSREEmitter blocks,
// smoke and steam drawn by the exhaust particles (task renderer 41), as a
// vehicle's ENG emitters are: chimneys, stacks, vents on the scenery.
namespace ShapeEmitters {

struct Emitter {
    QString name;
    // The matrix it hangs on, as the shape names it (empty: the root, the
    // first matrix).
    QString hierarchy;
    // Its kind (lower case, as CarExhaust's kinds: exhaust, stack,
    // safetyvalves, heating, steam), its defaults and wetness.
    QString kind = QStringLiteral("steam");
    // In that matrix's space (m), as the shape's vertices, and the way the
    // gas leaves: up (+y) turned by its QDirection or Rotation.
    float position[3] = {0.0f, 0.0f, 0.0f};
    float direction[3] = {0.0f, 1.0f, 0.0f};
    // The nozzle (m).
    float diameter = 0.2f;
    // What it gives out: its kind's values, the block's overriding them
    // (flow m³/s, speed m/s, temperature °C, steam and soot 0-1, beats a
    // second, colour ARGB).
    ExhaustOutput output;
    // Its steam's liquid water (its kind's: exhaustWetness).
    float wetness = 0.0f;
    // Each placed object's flow within +- this share, fixed by its UiD.
    float variation = 0.0f;
    // On ( 0 ): off until something switches it on.
    bool on = true;
};

// An ESD_TSREEmitter block's values, its name read; false for one left out
// (none so far: an unknown Kind takes Steam's). What was taken in place of
// a value (an unknown Kind, both of a pair, a ColorRGBA that is not hex)
// into `problems` (translated).
bool readBlock(FileBuffer *data, Emitter &emitter, QStringList *problems);
// The emitters of a parsed .sd's shape block (SFileComplex's).
QVector<Emitter> read(const SFileDetail::Node &shape, QStringList *problems = nullptr);
// The author messages of a shape's emitters, once a shape: its .sd's
// problems, matrices it does not have.
void report(const ComplexShape &shape);

// A placed object's emitters this frame, into the scenery's particles: its
// shape's, through its parts as posed in `stateId` (taken once a frame for
// a shape and state), `model` its place in the scene's space (the camera's
// tile at the origin), `camera` the camera there; within the emitters'
// distance only. `owner` names it from frame to frame, `uid` fixes its
// Variation. Off the exhaust frames (none drawn) it does nothing.
void giveOut(const ComplexShape &shape, unsigned int stateId, const float *model, const float *camera,
             const void *owner, quint32 uid);
// How far from the camera placed objects emit (m), the trains' distance.
constexpr float Distance = 2000.0f;

}

#endif // SHAPEEMITTERS_H
