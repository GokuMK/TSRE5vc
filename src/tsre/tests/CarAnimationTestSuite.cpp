#include "CarAnimationTestSuite.h"
#include "TokenTestSupport.h"
#include <QMap>
#include <cmath>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/trains/CarAnimation.h>
#include <tsre/trains/CarPlacement.h>

namespace {

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

// A car's parts: names, parents, rest places and last key frames (a key
// moving the part 1 m up a frame); what is posed recorded.
class PartsShape : public ComplexShape {
public:
    struct Part {
        QString name;
        int parent;
        float at[3];
        float lastFrame;
    };
    std::vector<Part> parts;
    QMap<int, float> frames;
    QMap<int, std::array<float, 16>> transforms;
    QString path = "car-animation-test";
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    const QString &getPathId() const override { return path; }
    bool isLoaded() const override { return true; }
    float getSize() const override { return 1; }
    const float *getBound() const override { return bounds; }
    bool getBoxPoints(QVector<float> &) override { return false; }
    void load() override {}
    void reload() override {}
    unsigned int newState() override { return 1; }
    void setAnimated(unsigned int, bool) override {}
    void updateSim(float, unsigned int) override {}
    void pushRenderItem(RenderQueue &) override {}
    void pushRenderItem(RenderQueue &, quint32, unsigned int) override {}
    void invalidateRenderState(bool) override {}
    int partCount() const override { return int(parts.size()); }
    QString partName(int part) const override { return parts[part].name; }
    int partParent(int part) const override { return parts[part].parent; }
    bool partRest(int part, float *out) const override {
        Mat4::identity(out);
        std::copy(parts[part].at, parts[part].at + 3, out + 12);
        return true;
    }
    float partLastFrame(int part) const override { return parts[part].lastFrame; }
    bool partAtFrame(int part, float frame, float *out) const override {
        partRest(part, out);
        if (parts[part].lastFrame >= 0)
            out[13] += std::clamp(frame, 0.0f, parts[part].lastFrame);
        return true;
    }
    float animationRate() const override { return 30.0f; }
    void setPartFrame(unsigned int, int part, float frame) override { frames[part] = frame; }
    void setPartTransform(unsigned int, int part, const float *local) override {
        std::array<float, 16> m;
        std::copy(local, local + 16, m.begin());
        transforms[part] = m;
    }
    void clearPose(unsigned int) override {
        frames.clear();
        transforms.clear();
    }
};

// A track bending to the left (radius 150 m) as the track gives places:
// x, y, z; along it `a` metres, its way there.
const float Radius = 150.0f;
void onCurve(float a, float *at, float *way) {
    const float angle = a / Radius;
    at[0] = Radius * std::cos(angle) - Radius;
    at[1] = 0.02f * a;
    at[2] = Radius * std::sin(angle);
    way[0] = -std::sin(angle);
    way[1] = 0.02f;
    way[2] = std::cos(angle);
    const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
    for (int i = 0; i < 3; ++i)
        way[i] /= length;
}

// The car's bogies on the curve, unflipped or flipped: their pivots where
// the track is and their way along it.
void checkCurve(TokenTest::Suite &test, bool flipped) {
    const float length = 20.0f, pivot = 7.0f;
    // Its front end at 30 m along, its rear at 10 m.
    float p0[3], w0[3], p1[3], w1[3];
    onCurve(30.0f, p0, w0);
    onCurve(10.0f, p1, w1);
    // Toward the rear at both ends.
    for (int i = 0; i < 3; ++i) {
        w0[i] = -w0[i];
        w1[i] = -w1[i];
    }
    const float front[7] = {p0[0], p0[1], p0[2], 0, 0, 0, 0};
    std::vector<CarPlacement::Bogie> bogies(2);
    bogies[0].z = pivot;
    bogies[1].z = -pivot;
    for (CarPlacement::Bogie &b : bogies) {
        Mat4::identity(b.rest);
        b.rest[14] = b.z;
    }
    float placedFront[7], placedRear[7];
    std::vector<float> turns;
    const bool placed = CarPlacement::onCarriers(front, p0, w0, p1, w1, 0.0f, flipped, {-pivot, pivot}, bogies,
                                                 placedFront, placedRear, turns);
    test.check(placed && turns.size() == 32, QString("%1: placed on its carriers").arg(flipped ? "flipped" : "unflipped"));
    if (!placed)
        return;
    float body[16];
    Mat4::identity(body);
    const float tile[2] = {0, 0};
    CarPlacement::placeCar(body, tile, placedFront, placedRear, 0.0f, flipped);
    float worstPlace = 0, worstAngle = 0;
    for (int b = 0; b < 2; ++b) {
        // Its pivot along the track: the car's front end less its distance
        // from the front (a flipped car's shape front at its rear).
        const float shapeZ = bogies[size_t(b)].z;
        const float fromFront = length / 2 - (flipped ? -shapeZ : shapeZ);
        float track[3], way[3];
        onCurve(30.0f - fromFront, track, way);
        // The bogie in the world: body * root mirror * its turn.
        float mirror[16];
        Mat4::identity(mirror);
        mirror[0] = -1;
        float m[16];
        Mat4::multiply(m, body, mirror);
        Mat4::multiply(m, m, turns.data() + b * 16);
        const float expected[3] = {track[0], track[1] + 0.28f, -track[2]};
        float place = 0;
        for (int i = 0; i < 3; ++i)
            place = std::max(place, std::fabs(m[12 + i] - expected[i]));
        worstPlace = std::max(worstPlace, place);
        // Its z axis (its front) along the track's way toward the shape's
        // front, in the world (z negated).
        float along[3] = {m[8], m[9], m[10]};
        float l = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        float trackWay[3] = {way[0], way[1], -way[2]};
        if (along[0] * trackWay[0] + along[1] * trackWay[1] + along[2] * trackWay[2] < 0)
            for (float &c : trackWay)
                c = -c;
        const float dot = (along[0] * trackWay[0] + along[1] * trackWay[1] + along[2] * trackWay[2]) / l;
        worstAngle = std::max(worstAngle, float(std::acos(std::min(1.0f, dot)) * 180.0 / M_PI));
    }
    qInfo().noquote() << "[tests:car-animation]" << (flipped ? "flipped:" : "unflipped:") << "pivots off the track by"
                      << worstPlace << "m, bogies off its way by" << worstAngle << "degrees";
    test.check(worstPlace < 0.01f, QString("%1: both pivots on the track (within 1 cm)").arg(flipped ? "flipped" : "unflipped"));
    test.check(worstAngle < 0.05f, QString("%1: both bogies along the track (within 0.05 degrees)").arg(flipped ? "flipped" : "unflipped"));
}

}

int TsreTests::runCarAnimationSuite(bool verbose) {
    TokenTest::Suite test{"[tests:car-animation]", verbose};
    PartsShape car;
    car.parts = {
        {"MAIN", -1, {0, 0, 0}, -1},
        {"BOGIE1", 0, {0, 0.5f, 7}, -1},
        {"WHEELS11", 1, {0, 0, 1}, -1},
        {"WHEELS12", 1, {0, 0, -1}, -1},
        {"BOGIE2", 0, {0, 0.5f, -7}, -1},
        {"WHEELS21", 4, {0, 0, 1}, -1},
        {"WHEELS22", 4, {0, 0, -1}, -1},
        {"PANTOGRAPHBOTTOM1A", 0, {0, 4, 5}, 1},
        {"PANTOGRAPHMIDDLE1A", 7, {0, 1, 0}, 1},
        {"PANTOGRAPHBOTTOM2A", 0, {0, 4, -5}, 1},
        {"DOOR_A1", 0, {1.4f, 1, 3}, 2},
        {"DOOR_D1", 0, {-1.4f, 1, 3}, 2},
        {"ROD1", 0, {1, 1, 0}, 8},
        {"WHEELS1", 0, {0, 1, 2}, 8},
        {"MIRRORLEFT1", 0, {-1.5f, 3, 9}, 1},
        {"FAN", 0, {0, 4, 0}, -1},
    };
    CarAnimation animation(car, true, false, 0.0f, 0.5f);
    test.check(animation.wheelCount() == 4 && animation.bogies().size() == 2 && animation.runningGearCount() == 2,
               "wheels, bogies and running gear (keyed ROD1 and WHEELS1) by Open Rails' names");
    test.check(animation.carriers() == std::vector<float>({-7.0f, 7.0f}), "carried on its bogies' pivots");
    test.check(animation.groupParts(CarAnimation::Pantograph1) == 2 && animation.groupParts(CarAnimation::Pantograph2) == 1
                   && animation.groupParts(CarAnimation::DoorRight) == 1 && animation.groupParts(CarAnimation::DoorLeft) == 1
                   && animation.groupParts(CarAnimation::Mirrors) == 1,
               "pantographs (with their children), doors A right and D left, mirrors");

    // Two-state parts: at once the first time, then a key frame a second.
    animation.set(CarAnimation::DoorRight, true);
    animation.update(0.0f, 0.0f);
    animation.pose(car, 1, nullptr);
    const bool open = car.frames.value(10, -1) == 2.0f && !car.frames.contains(11) && car.transforms.isEmpty();
    animation.set(CarAnimation::DoorRight, false);
    animation.update(0.0f, 0.5f);
    animation.pose(car, 1, nullptr);
    const bool closing = near(car.frames.value(10, -1), 1.5f);
    animation.update(0.0f, 5.0f);
    animation.pose(car, 1, nullptr);
    test.check(open && closing && !car.frames.contains(10),
               "a door open at once, closing a key frame a second, at rest when closed");

    animation.setPantograph(1, true, false);
    const bool swapped = animation.isSet(CarAnimation::Pantograph2) && !animation.isSet(CarAnimation::Pantograph1);
    animation.setPantograph(1, false, false);
    animation.setPantograph(1, true, true);
    test.check(swapped && animation.isSet(CarAnimation::Pantograph1),
               "the simulator's pantograph 1: the shape's 2 on an unflipped car with both, its 1 flipped");
    animation.setPantograph(1, false, true);

    // A quarter turn of the wheels (radius 0.5 m) and the running gear.
    animation.update(float(M_PI) * 0.5f * 0.5f, 0.0f);
    animation.pose(car, 1, nullptr);
    const auto wheel = car.transforms.value(2);
    test.check(car.transforms.size() == 4 && near(wheel[6], 1.0f, 1e-3f) && near(wheel[14], 1.0f),
               "wheels turned about x, their tops toward the shape's front, in place");
    test.check(near(car.frames.value(12, -1), 2.0f, 1e-3f) && near(car.frames.value(13, -1), 2.0f, 1e-3f),
               "running gear: a revolution its last key frame (a quarter: frame 2 of 8)");
    animation.update(float(M_PI) * 0.5f * 1.5f, 0.0f);
    animation.pose(car, 1, nullptr);
    test.check(car.transforms.isEmpty() && !car.frames.contains(12), "a whole turn: back at rest, nothing posed");

    checkCurve(test, false);
    checkCurve(test, true);
    return test.finish();
}
