#include <tsre/tests/CarGangwaysTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <memory>
#include <tsre/characters/CharacterBody.h>
#include <tsre/characters/CollisionMesh.h>
#include <tsre/characters/CollisionWorld.h>
#include <tsre/characters/Gangways.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/ShapeSeats.h>
#include <tsre/trains/CarPlacement.h>

namespace {

using Characters::CharacterBody;
using Characters::CollisionMesh;
using Characters::CollisionWorld;
namespace Gangways = Characters::Gangways;
using ShapeSeats::EndPassage;

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// An .sd with a seat, an aisle and a door at each end, after `axes` (an
// ESD_TSREAxes line or none) and `extra` lines in its seats block.
QByteArray sdText(const char *model, const char *axes, const char *extra = "") {
    QByteArray text = "SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\nshape ( ";
    text += model;
    text += "\r\n\tESD_Detail_Level ( 0 )\r\n";
    text += axes;
    text += "\tESD_TSRESeats (\r\n"
            "\t\tAisle 1 ( 0.1 1.2 -11 11 )\r\n"
            "\t\tSeat 1 ( 0.6 1.6 -3 90 )\r\n"
            "\t\tEndPassage ( 0.1 1.2 11.6 0.8 )\r\n"
            "\t\tEndPassage ( 0.05 1.2 -11.6 0.7 )\r\n";
    text += extra;
    text += "\t)\r\n)\r\n";
    return text;
}

EndPassage door(float x, float y, float z, float width) {
    EndPassage d;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.width = width;
    return d;
}

void transformPoint(const float *m, const float *p, float *out) {
    float r[3];
    for (int i = 0; i < 3; ++i)
        r[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
    std::copy(r, r + 3, out);
}

// A rotation and translation's inverse (column major).
void rigidInverse(const float *m, float *out) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[c * 4 + r] = m[r * 4 + c];
    for (int r = 0; r < 3; ++r)
        out[12 + r] = -(out[r] * m[12] + out[4 + r] * m[13] + out[8 + r] * m[14]);
    out[3] = out[7] = out[11] = 0.0f;
    out[15] = 1.0f;
}

// A level track in track places (x, y, z; as CarPlacement takes them):
// straight along z up to `curveFrom`, then (radius > 0) a circle turning
// toward +x.
struct Track {
    float radius = 0.0f;
    float curveFrom = 1.0e9f;
    void at(double s, float *place, float *way) const {
        std::fill(place, place + 7, 0.0f);
        if (radius <= 0.0f || s <= curveFrom) {
            place[2] = float(s);
            way[0] = 0.0f;
            way[1] = 0.0f;
            way[2] = 1.0f;
            return;
        }
        const double u = (s - curveFrom) / radius;
        place[0] = float(radius - radius * std::cos(u));
        place[2] = float(curveFrom + radius * std::sin(u));
        way[0] = float(std::sin(u));
        way[1] = 0.0f;
        way[2] = float(std::cos(u));
    }
};

constexpr float Length = 24.5f;
constexpr float Coupling = Length / 2.0f;
constexpr float Wall = 11.6f;
constexpr float HalfWidth = 1.4f;
// A car's footprint along it (its definition's size, CarCollision's): its
// body's end, short of its coupling end (buffers), a little past its end
// wall (as the Bhp's: wall 9.81, size 19.75).
constexpr float HalfLength = Wall + 0.1f;

// A car's matrix (shape to world, tile 0 0): its front end at `front` along
// the track, its rear a length behind; on its ends, or on two carriers 8.6 m
// either side of its middle (fitted, as Eng::placeOnCarriers).
void carMatrix(const Track &track, double front, float roll, bool flipped, bool carriers, float *m) {
    float p0[7], p1[7], t0[3], t1[3];
    track.at(front, p0, t0);
    track.at(front - Length, p1, t1);
    Mat4::identity(m);
    const float tile[2] = {0.0f, 0.0f};
    if (carriers) {
        const float back0[3] = {-t0[0], -t0[1], -t0[2]}, back1[3] = {-t1[0], -t1[1], -t1[2]};
        float placedFront[7], placedRear[7];
        std::vector<float> turns;
        if (CarPlacement::onCarriers(p0, p0, back0, p1, back1, roll, flipped, {8.6f, -8.6f}, {}, placedFront,
                                     placedRear, turns)) {
            CarPlacement::placeCar(m, tile, placedFront, placedRear, roll, flipped);
            return;
        }
    }
    CarPlacement::placeCar(m, tile, p0, p1, roll, flipped);
}

// Two coupled cars: A in front, B behind it (`gap` between their coupling
// ends: + stretched, - pushed together), each car's matrix, its end toward
// the other, its door there and its plate.
struct Pair {
    float a[16], ia[16], b[16], ib[16];
    int endA = 0, endB = 0;
    EndPassage doorA, doorB;
    Gangways::Plate plateA, plateB;
    bool open = false;
};

Pair couple(const Track &track, double front, float gap, float rollA, float rollB, bool flipB, bool carriers,
            float floorA = 1.2f, float floorB = 1.2f) {
    Pair p;
    carMatrix(track, front, rollA, false, carriers, p.a);
    carMatrix(track, front - Length - gap, rollB, flipB, carriers, p.b);
    rigidInverse(p.a, p.ia);
    rigidInverse(p.b, p.ib);
    p.endA = Gangways::endTowards(p.ia, p.b);
    p.endB = Gangways::endTowards(p.ib, p.a);
    p.doorA = door(0.0f, floorA, float(p.endA) * Wall, 0.8f);
    p.doorB = door(0.0f, floorB, float(p.endB) * Wall, 0.7f);
    float width = 0.0f;
    p.open = Gangways::matches(p.doorA, Coupling, p.doorB, Coupling, &width);
    if (p.open) {
        p.plateA = Gangways::plateOf(p.doorA, Coupling, width);
        p.plateB = Gangways::plateOf(p.doorB, Coupling, width);
    }
    return p;
}

// Along the straight line from A's door to B's door (as placed): some plate
// under every point; where A hands over, B's plate under it, the two floors
// at most a step apart; and the same walking back.
bool overlapAlong(const Pair &p, QString *why) {
    float from[3] = {p.plateA.x, p.plateA.y, p.plateA.wall}, to[3] = {p.plateB.x, p.plateB.y, p.plateB.wall};
    transformPoint(p.a, from, from);
    transformPoint(p.b, to, to);
    constexpr int Steps = 400;
    int handA = -1, handB = -1;
    for (int k = 0; k <= Steps; ++k) {
        const float f = float(k) / Steps;
        const float w[3] = {from[0] + (to[0] - from[0]) * f, from[1] + (to[1] - from[1]) * f,
                            from[2] + (to[2] - from[2]) * f};
        float la[3], lb[3];
        transformPoint(p.ia, w, la);
        transformPoint(p.ib, w, lb);
        if (!p.plateA.holds(la[0], la[2], Gangways::WallDepth) && !p.plateB.holds(lb[0], lb[2], Gangways::WallDepth)) {
            *why = QString("no plate at %1 of the way (A %2 %3, B %4 %5)").arg(double(f), 0, 'f', 3)
                           .arg(double(la[0]), 0, 'f', 2).arg(double(la[2]), 0, 'f', 2).arg(double(lb[0]), 0, 'f', 2)
                           .arg(double(lb[2]), 0, 'f', 2);
            return false;
        }
        if (handA < 0 && Gangways::handedOver(p.plateA, Coupling, la[2]))
            handA = k;
        if (Gangways::handedOver(p.plateB, Coupling, lb[2]))
            handB = k;
    }
    for (const int k : {handA, handB}) {
        if (k < 0) {
            *why = "never handed over";
            return false;
        }
        const float f = float(k) / Steps;
        const float w[3] = {from[0] + (to[0] - from[0]) * f, from[1] + (to[1] - from[1]) * f,
                            from[2] + (to[2] - from[2]) * f};
        float la[3], lb[3];
        transformPoint(p.ia, w, la);
        transformPoint(p.ib, w, lb);
        const bool fromA = k == handA;
        if (fromA ? !p.plateB.holds(lb[0], lb[2]) : !p.plateA.holds(la[0], la[2])) {
            *why = QString("handed over at %1 of the way off the next plate").arg(double(f), 0, 'f', 3);
            return false;
        }
        float fa[3] = {la[0], p.plateA.y, la[2]}, fb[3] = {lb[0], p.plateB.y, lb[2]};
        transformPoint(p.a, fa, fa);
        transformPoint(p.b, fb, fb);
        if (std::fabs(fa[1] - fb[1]) > ShapeSeats::OneStep + 0.05f) {
            *why = QString("floors %1 m apart where handed over").arg(double(fa[1] - fb[1]), 0, 'f', 3);
            return false;
        }
    }
    return true;
}

void quad(std::vector<float> &t, const float *a, const float *b, const float *c, const float *d) {
    for (const float *q : {a, b, c, a, c, d})
        t.insert(t.end(), q, q + 3);
}

// A car's world in its frame (as CarCollision's): its floor, its end walls
// drawn closed across, its sides; its plate and its door's opening at `end`
// when open (as CarCollision::openEnd).
CollisionWorld carWorld(float floorY, const Gangways::Plate &plate, const EndPassage &door) {
    std::vector<float> t;
    const float x0 = -HalfWidth, x1 = HalfWidth, y0 = floorY, y1 = floorY + 2.4f;
    {
        const float a[3] = {x0, y0, -Wall}, b[3] = {x1, y0, -Wall}, c[3] = {x1, y0, Wall}, d[3] = {x0, y0, Wall};
        quad(t, a, b, c, d);
    }
    for (const float z : {-Wall, Wall}) {
        const float a[3] = {x0, y0, z}, b[3] = {x1, y0, z}, c[3] = {x1, y1, z}, d[3] = {x0, y1, z};
        quad(t, a, b, c, d);
    }
    for (const float x : {x0, x1}) {
        const float a[3] = {x, y0, -Wall}, b[3] = {x, y0, Wall}, c[3] = {x, y1, Wall}, d[3] = {x, y1, -Wall};
        quad(t, a, b, c, d);
    }
    CollisionWorld world;
    float identity[16];
    Mat4::identity(identity);
    world.add(std::make_shared<CollisionMesh>(t), identity);
    if (plate.open()) {
        world.add(std::make_shared<CollisionMesh>(Gangways::triangles(plate)), identity, 0, false);
        float box[6];
        Gangways::opening(door, plate, box);
        world.addOpening(box);
    }
    return world;
}

struct Walked {
    bool reached = false;
    int handovers = 0;
    bool grounded = true;
    float worstRise = 0.0f;
    float worstStep = 0.0f;
    float endZ = 0.0f;
    QString text() const {
        return QString("reached %1, %2 handovers, grounded %3, worst rise %4 m, worst step %5 m, at z %6")
                .arg(reached).arg(handovers).arg(grounded).arg(double(worstRise), 0, 'f', 3)
                .arg(double(worstStep), 0, 'f', 3).arg(double(endZ), 0, 'f', 2);
    }
};

// A body walking 1.4 m/s from 3 m inside one car (A, or B when `back`) to 2
// m inside the other, the train at `speed` from `front` (its cars placed
// anew each frame), the body in its car's frame and handed over as the
// character is (Gangways::handedOver: through the world into the other
// car's frame). A moving car keeps it within its footprint or its plate.
Walked walkAcross(const Track &track, double front, float speed, bool back, bool carriers, float floorA,
                  float floorB, bool closed = false) {
    Walked out;
    Pair p = couple(track, front, 0.0f, 0.0f, 0.0f, false, carriers, floorA, floorB);
    const Gangways::Plate none;
    const CollisionWorld worlds[2] = {carWorld(floorA, closed ? none : p.plateA, p.doorA),
                                      carWorld(floorB, closed ? none : p.plateB, p.doorB)};
    const Gangways::Plate plates[2] = {closed ? none : p.plateA, closed ? none : p.plateB};
    const int ends[2] = {p.endA, p.endB};
    int in = back ? 1 : 0;
    CharacterBody body;
    CharacterBody::Settings settings;
    settings.radius = 0.22f;
    body.setSettings(settings);
    const float start[3] = {0.0f, back ? floorB : floorA, float(ends[in]) * (Wall - 3.0f)};
    body.setPosition(start);
    float lastWorld[3] = {0, 0, 0};
    bool hasLast = false;
    constexpr float Dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 60 * 12; ++frame) {
        const double at = front + double(speed) * double(frame) * Dt;
        const Pair now = couple(track, at, 0.0f, 0.0f, 0.0f, false, carriers, floorA, floorB);
        const float *m[2] = {now.a, now.b}, *inverse[2] = {now.ia, now.ib};
        // Toward the other car in the first, away from it in the second.
        const float vz = (in == (back ? 1 : 0) ? 1.4f : -1.4f) * float(ends[in]);
        const int car = in;
        std::function<bool(float, float)> keep;
        if (speed > 0.0f) {
            const Gangways::Plate &plate = plates[car];
            const Gangways::Plate &front = plate.end > 0 ? plate : none, &rear = plate.end < 0 ? plate : none;
            keep = [&front, &rear](float x, float z) {
                return Gangways::within(HalfWidth, HalfLength, front, rear, x, z, 0.22f);
            };
        }
        body.move(worlds[in], 0.0f, vz, Dt, false, keep);
        if (Gangways::handedOver(plates[in], Coupling, body.position()[2])) {
            float w[3], local[3];
            transformPoint(m[in], body.position(), w);
            in = 1 - in;
            transformPoint(inverse[in], w, local);
            body.setPosition(local);
            ++out.handovers;
        }
        float w[3];
        transformPoint(m[in], body.position(), w);
        if (frame > 12) {
            out.grounded = out.grounded && body.onGround();
            out.worstRise = std::max(out.worstRise, std::fabs(w[1] - lastWorld[1]));
            // Over the ground: at most the train's way and the walk's in a
            // frame.
            const float step = std::hypot(w[0] - lastWorld[0], w[2] - lastWorld[2]);
            out.worstStep = std::max(out.worstStep, step - (speed + 1.4f) * Dt);
        }
        std::copy(w, w + 3, lastWorld);
        hasLast = true;
        out.endZ = body.position()[2];
        if (in != (back ? 1 : 0) && float(ends[in]) * body.position()[2] < Wall - 2.0f) {
            out.reached = true;
            break;
        }
    }
    (void)hasLast;
    return out;
}

} // namespace

namespace TsreTests {

int runCarGangwaysSuite(bool verbose) {
    TokenTest::Suite test{"car-gangways", verbose};

    // Read: an .s's .sd as written, an .e3d's mirrored (MSTS axes) unless
    // it says glTF; drawn mirrors an S file's; glTF extras; a bad line.
    {
        QTemporaryDir dir;
        const QString s = dir.filePath("car.s"), e3d = dir.filePath("car.e3d"), gltf = dir.filePath("glcar.e3d");
        writeFile(s, "x");
        writeFile(e3d, "x");
        writeFile(gltf, "x");
        writeFile(dir.filePath("car.sd"), sdText("car.s", ""));
        writeFile(dir.filePath("glcar.sd"), sdText("glcar.e3d", "\tESD_TSREAxes ( glTF )\r\n"));
        ShapeSeats::Seats onS, onE3d, onGltf;
        const bool read = ShapeSeats::readFor(s, onS) && ShapeSeats::readFor(e3d, onE3d) && ShapeSeats::readFor(gltf, onGltf);
        const EndPassage *front = onS.endPassageAt(1), *rear = onS.endPassageAt(-1);
        test.check(read && onS.endPassages.size() == 2 && front != nullptr && rear != nullptr
                           && near(front->position[0], 0.1f) && near(front->position[1], 1.2f)
                           && near(front->position[2], 11.6f) && near(front->width, 0.8f) && near(rear->width, 0.7f)
                           && near(rear->position[2], -11.6f),
                   "EndPassage read: one at each end, by its z's sign");
        test.check(onE3d.endPassageAt(1) != nullptr && near(onE3d.endPassageAt(1)->position[0], -0.1f)
                           && onGltf.endPassageAt(1) != nullptr && near(onGltf.endPassageAt(1)->position[0], 0.1f),
                   "beside an .e3d: an MSTS .sd mirrored in x, a glTF one as written (task shapes 20)");
        const ShapeSeats::Seats drawn = ShapeSeats::drawn(onS, s);
        test.check(drawn.endPassageAt(1) != nullptr && near(drawn.endPassageAt(1)->position[0], -0.1f)
                           && near(drawn.endPassageAt(1)->position[2], 11.6f),
                   "drawn: an S file's mirrored in x as its other places, its end kept");
        QJsonObject block;
        block.insert("seats", QJsonArray{QJsonArray{0.6, 1.6, -3, 90}});
        block.insert("aisles", QJsonArray{QJsonArray{0, 1.2, -11, 11}});
        block.insert("endPassages", QJsonArray{QJsonArray{0, 1.1, -11.7, 0.75}});
        QJsonObject extras;
        extras.insert("TSRE_seats", block);
        const ShapeSeats::Seats fromGltf = ShapeSeats::fromExtras(extras);
        test.check(fromGltf.endPassages.size() == 1 && fromGltf.endPassageAt(-1) != nullptr
                           && fromGltf.endPassageAt(1) == nullptr && near(fromGltf.endPassages[0].width, 0.75f),
                   "a glTF's extras.TSRE_seats endPassages read");
        writeFile(dir.filePath("bad.s"), "x");
        writeFile(dir.filePath("bad.sd"), sdText("bad.s", "", "\t\tEndPassage ( 1 2 )\r\n\t\tGangway ( 0 1 11 1 )\r\n"));
        ShapeSeats::Seats bad;
        QStringList problems;
        ShapeSeats::readFor(dir.filePath("bad.s"), bad, &problems);
        test.check(bad.endPassages.size() == 2 && problems.size() == 2 && problems[0].contains("EndPassage")
                           && problems[1].contains("Gangway") && problems[1].contains("EndPassage"),
                   QString("a short EndPassage left out, an unknown line's message names EndPassage (%1)")
                           .arg(problems.join("; ")));
    }

    // When two facing doors open a gangway.
    {
        float width = 0.0f;
        test.check(Gangways::matches(door(0, 1.2f, 11.6f, 0.8f), Coupling, door(0, 1.2f, -11.6f, 0.7f), Coupling, &width)
                           && near(width, 0.7f),
                   "two doors at one height, near their coupling ends: open, the narrower's width");
        test.check(Gangways::matches(door(0, 1.2f, 11.6f, 0.8f), Coupling, door(0, 1.45f, 11.6f, 0.8f), Coupling)
                           && !Gangways::matches(door(0, 1.2f, 11.6f, 0.8f), Coupling, door(0, 1.5f, 11.6f, 0.8f),
                                                 Coupling),
                   "floors 0.25 m apart open (a step, OneStep), 0.3 m do not");
        test.check(!Gangways::matches(door(0, 1.2f, 9.5f, 0.8f), Coupling, door(0, 1.2f, 11.6f, 0.8f), Coupling)
                           && Gangways::matches(door(0, 1.2f, 10.75f, 0.8f), Coupling, door(0, 1.2f, 11.6f, 0.8f),
                                                Coupling),
                   "a wall 2.75 m from its coupling end (a cab's partition) closed, 1.5 m (MaxReach) open");
        test.check(!Gangways::matches(door(0, 1.2f, 12.5f, 0.8f), Coupling, door(0, 1.2f, 11.6f, 0.8f), Coupling)
                           && !Gangways::reaches(door(0, 1.2f, 11.6f, 0.8f), 0.0f),
                   "a wall past its coupling end, or a car without a length: closed");
    }

    // Which end faces which car: the other car's middle in its frame.
    {
        const Track straight;
        const Pair same = couple(straight, 100.0, 0.0f, 0.0f, 0.0f, false, false);
        const Pair flipped = couple(straight, 100.0, 0.0f, 0.0f, 0.0f, true, false);
        test.check(same.endA == -same.endB && flipped.endA == same.endA && flipped.endB == -same.endB,
                   QString("facing ends: opposite for two cars the same way round, the same for a flipped one "
                           "(A's %1, B's %2, flipped B's %3)").arg(same.endA).arg(same.endB).arg(flipped.endB));
        // The faced end's coupling point is where the other car's is.
        float a[3] = {0.0f, 0.0f, float(same.endA) * Coupling}, b[3] = {0.0f, 0.0f, float(same.endB) * Coupling},
              c[3] = {0.0f, 0.0f, float(flipped.endB) * Coupling};
        transformPoint(same.a, a, a);
        transformPoint(same.b, b, b);
        transformPoint(flipped.b, c, c);
        test.check(std::hypot(a[0] - b[0], a[2] - b[2]) < 0.01f && std::hypot(a[0] - c[0], a[2] - c[2]) < 0.01f,
                   "the facing ends' coupling points meet, flipped or not");
        if (verbose)
            qInfo().noquote() << "car-gangways: an unflipped car's end toward the car behind it:"
                              << (same.endA > 0 ? "+z" : "-z");
    }

    // The plates overlap however the cars stand.
    {
        Track straight, curve;
        curve.radius = 150.0f;
        curve.curveFrom = 0.0f;
        const float cant = std::asin(0.15f / 1.5f);
        struct Case {
            const char *name;
            const Track *track;
            float gap, rollA, rollB;
            bool carriers;
        };
        const Case cases[] = {{"straight", &straight, 0.0f, 0.0f, 0.0f, false},
                              {"a 150 m curve", &curve, 0.0f, 0.0f, 0.0f, false},
                              {"a 150 m curve on carriers", &curve, 0.0f, 0.0f, 0.0f, true},
                              {"the curve's start between them", &curve, 0.0f, 0.0f, 0.0f, true},
                              {"0.15 m cant on one car only", &curve, 0.0f, cant, 0.0f, true},
                              {"slack: 0.2 m apart", &curve, 0.2f, 0.0f, 0.0f, true},
                              {"slack: 0.2 m pushed together", &curve, -0.2f, 0.0f, 0.0f, true}};
        for (const Case &c : cases) {
            const double front = QByteArray(c.name).contains("start") ? Length : 200.0;
            const Pair p = couple(*c.track, front, c.gap, c.rollA, c.rollB, false, c.carriers);
            QString why;
            test.check(p.open && overlapAlong(p, &why), QString("plates overlap: %1 %2").arg(c.name, why));
        }
    }

    // Walking across between two cars' worlds.
    {
        Track straight, curve;
        curve.radius = 150.0f;
        curve.curveFrom = 200.0f;
        const Walked standing = walkAcross(straight, 100.0, 0.0f, false, false, 1.2f, 1.2f);
        test.check(standing.reached && standing.handovers == 1 && standing.grounded && standing.worstRise < 0.01f
                           && standing.worstStep < 0.02f,
                   "standing: walked from car to car, handed over once, on a floor all the way: " + standing.text());
        const Walked stepUp = walkAcross(straight, 100.0, 0.0f, false, false, 1.2f, 1.4f);
        test.check(stepUp.reached && stepUp.handovers == 1 && stepUp.grounded && stepUp.worstRise < 0.26f,
                   "standing: up a 0.2 m step into the next car: " + stepUp.text());
        // The coupling ends reach the curve's start about 2 s in.
        const double front = 200.0 + Length - 20.0;
        const Walked moving = walkAcross(curve, front, 10.0f, false, true, 1.2f, 1.2f);
        test.check(moving.reached && moving.handovers == 1 && moving.grounded && moving.worstStep < 0.05f,
                   "at 10 m/s into a 150 m curve: across, handed over once, no jump: " + moving.text());
        const Walked movingBack = walkAcross(curve, front, 10.0f, true, true, 1.2f, 1.2f);
        test.check(movingBack.reached && movingBack.handovers == 1 && movingBack.grounded
                           && movingBack.worstStep < 0.05f,
                   "at 10 m/s, walking back to the front car: " + movingBack.text());
        const Walked closed = walkAcross(straight, 100.0, 0.0f, false, false, 1.2f, 1.2f, true);
        test.check(!closed.reached && closed.handovers == 0 && closed.endZ * float(couple(straight, 100.0, 0.0f, 0.0f,
                                                                                         0.0f, false, false).endA)
                                                                    < Wall,
                   "a closed end: its wall stops it: " + closed.text());
    }

    // A joint walked over: the cars either side of it are next to each
    // other.
    {
        const std::vector<float> lengths = {16.0f, 24.5f, 0.2f, 24.5f, 0.2f, 0.3f};
        test.check(Gangways::nextCar(lengths, 1, 1) == 3 && Gangways::nextCar(lengths, 3, -1) == 1
                           && Gangways::nextCar(lengths, 1, -1) == 0 && Gangways::nextCar(lengths, 3, 1) == -1
                           && Gangways::nextCar(lengths, 0, -1) == -1,
                   "next cars: a 0.2 m joint (an articulated unit's bogie) walked over; none past the train's ends");
        // Plates over a 0.2 m joint: as slack 0.2 m apart.
        Track straight;
        const Pair p = couple(straight, 100.0, 0.2f, 0.0f, 0.0f, false, false);
        QString why;
        test.check(p.open && overlapAlong(p, &why), "plates overlap over a 0.2 m joint " + why);
    }

    // Back inside from an end closing.
    {
        const Gangways::Plate plate = Gangways::plateOf(door(0.1f, 1.2f, -11.6f, 0.8f), Coupling, 0.7f);
        float out[2];
        const float onPlate[3] = {0.2f, 1.2f, -12.3f}, inDoor[3] = {0.0f, 1.2f, -11.4f}, inside[3] = {0.0f, 1.2f, -10.0f};
        test.check(Gangways::insidePlace(plate, onPlate, out) && near(out[0], 0.1f) && near(out[1], -11.1f)
                           && Gangways::insidePlace(plate, inDoor, out) && !Gangways::insidePlace(plate, inside, out),
                   "an end closing: one on its plate or in its door goes half a metre inside, one inside stays");
        // Its far end: 12.25 + 0.5 m.
        // A moving car's footprint ends short of the wall by the body's
        // radius: the plate's zone reaches into the car (found live: a
        // character stopped 0.16 m short of the Bhp's end wall).
        const Gangways::Plate none;
        test.check(Gangways::within(HalfWidth, HalfLength, none, plate, 0.0f, -11.55f, 0.22f)
                           && Gangways::within(HalfWidth, HalfLength, none, plate, 0.0f, -12.6f, 0.22f)
                           && !Gangways::within(HalfWidth, HalfLength, none, none, 0.0f, -11.55f, 0.22f)
                           && !Gangways::within(HalfWidth, HalfLength, none, plate, 0.6f, -12.6f, 0.22f),
                   "a moving car's footprint and its open plate meet: no gap at the wall");
        const float far[3] = {0.0f, 1.2f, -12.7f};
        test.check(plate.holds(far[0], far[2]) && !plate.holds(far[0], far[2] - 0.1f)
                           && !plate.holds(0.5f, -12.0f) && Gangways::handedOver(plate, Coupling, -12.41f)
                           && !Gangways::handedOver(plate, Coupling, -12.39f),
                   "a plate: to its coupling end and 0.5 m past it, its width across; handed over 0.15 m past");
    }

    return test.finish();
}

}
