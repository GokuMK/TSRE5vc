/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPELIGHTS_H
#define SHAPELIGHTS_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <array>
#include <vector>

class ComplexShape;
class FileBuffer;
class RenderItem;
class RenderQueue;
namespace SFileDetail {
struct Node;
}

// A shape's lights (task shapes 14): its .sd's ESD_TSRELight blocks (S
// shapes), or its glTF punctual lights, which light the scene as glTF's do.
namespace ShapeLights {

struct Light {
    enum class Type { Point, Spot };
    // Its name: the .sd's Name, a glTF light's name (Open Rails'
    // OPENRAILS_light_name when given).
    QString name;
    // The name an ENG light binds it by (its ShapeHierarchy): the .sd's
    // Name; a glTF light's OPENRAILS_light_name only, as Open Rails binds,
    // so an ENG binds the same light in both.
    QString bindName;
    // The matrix it hangs on, as the shape names it (empty: the root, the
    // first matrix; glTF lights are in the shape's space).
    QString hierarchy;
    Type type = Type::Point;
    // In that matrix's space (m), as the shape's vertices, and the way it
    // shines: -z turned by its QDirection or Rotation, as glTF lights shine
    // along their node's -Z.
    float position[3] = {0.0f, 0.0f, 0.0f};
    float direction[3] = {0.0f, 0.0f, -1.0f};
    // Linear, as glTF's colour; candela (ColorRGBA's alpha taken in).
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
    // m; 0 for none (where its light fades out).
    float range = 0.0f;
    // m: the lamp's size, softening surfaces close to it; its disc's radius.
    float radius = 0.05f;
    // Spot cone half-angles (radians).
    float innerCone = 0.0f;
    float outerCone = 0.785398f;
    // On ( 0 ): off until something switches it on (an ENG light bound to
    // it); glTF: extras.TSRE_on.
    bool on = true;
};

// An ESD_TSRELight block's values, its name read: false for one left out
// (no Intensity above 0, an unknown Type). Why it was left out, and what
// was taken in its place (both of a pair, a cone out of range), into
// `problems` (translated).
bool readBlock(FileBuffer *data, Light &light, QStringList *problems);
// The lights of a parsed .sd's shape block (SFileComplex's); problems as
// readBlock's.
QVector<Light> read(const SFileDetail::Node &shape, QStringList *problems = nullptr);
// The shape's part a light hangs on: its ShapeHierarchy's, else the root
// (the first matrix, through which an MSTS shape's vertices are drawn, x
// mirrored); -1 for a shape without parts (glTF: lights in its space).
int partOf(const ComplexShape &shape, const Light &light);
// The light an ENG light's ShapeHierarchy names (by its bindName, any
// case); -1 for none.
int bound(const ComplexShape &shape, const QString &name);
// The author messages of a shape's lights, once per shape: its .sd's
// problems, matrices it does not have.
void report(const ComplexShape &shape);

// A shape's lights drawn, per state: a packet without a mesh holding the
// lights of each matrix with lights, its msMatrix the posed part's (as a
// display square's), so lights follow animation; where the scene has no
// local light (the OpenGL renderer, QRhi with local lights off) a plain
// disc per light, in its colour, Radius wide, facing its way, as signal and
// train lamps there. A state's packets are shared by the objects drawing
// it; its matrices taken once a frame (Renderer::frameNumber).
class Drawn {
public:
    // Lights alone: a car's seen from its cab.
    enum class What { LightsAndDiscs, Lights };

    Drawn() = default;
    Drawn(const Drawn &) = delete;
    Drawn &operator=(const Drawn &) = delete;
    ~Drawn();

    void push(RenderQueue &queue, const ComplexShape &shape, unsigned int stateId, quint32 selectionId,
              What what = What::LightsAndDiscs);
    // Retires every packet (the next push makes them again).
    void clear();
    // A light's strength in a state, 0 (off) to 1 (its full light), set by
    // what switches it (an ENG light bound to it); below 0 its own (On).
    void setStrength(const ComplexShape &shape, unsigned int stateId, int light, float strength);
    // A state released: its lights back to their own.
    void resetState(unsigned int stateId);

private:
    struct Group {
        // The part, -1 for the root.
        int part = -1;
        std::array<float, 16> matrix{};
        // Its lights in the part's space.
        RenderItem *lights = nullptr;
        // Indices into the shape's lights, their light at full strength,
        // and their discs.
        std::vector<int> members;
        std::vector<float> intensity;
        std::vector<RenderItem *> discs;
        std::vector<std::array<float, 16>> discMatrices;
    };
    struct State {
        quint64 frame = ~quint64(0);
        bool discs = false;
        std::vector<Group> groups;
        // Each light's strength (the shape's order); changed since shone.
        std::vector<float> strength;
        bool changed = true;
    };
    // Its packets dropped when the shape's lights are others (reloaded).
    void sync(const ComplexShape &shape);
    State &stateOf(unsigned int stateId);
    void build(State &state, const ComplexShape &shape, bool discs);
    void place(State &state, const ComplexShape &shape, unsigned int stateId);
    // The strengths into the packets.
    void shine(State &state);
    static void retire(State &state);

    // The lights its packets were made from: a shape reloaded has others.
    // Read with at() only: a non-const [] would detach it from the shape's.
    QVector<Light> built;
    // Its author messages given (report() keeps them to one a shape).
    bool reported = false;
    std::vector<State> states;
};

}

#endif // SHAPELIGHTS_H
