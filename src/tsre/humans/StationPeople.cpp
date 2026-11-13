/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/humans/StationPeople.h>

#include <DetourCommon.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <limits>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/humans/CarDoors.h>
#include <tsre/humans/Crowd.h>
#include <tsre/humans/HumanSet.h>
#include <tsre/humans/StationWalkAreas.h>
#include <tsre/humans/WalkArea.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeSeats.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/CarAnimation.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/world/objects/WorldObj.h>

namespace Humans {

namespace {

// People come over the ten minutes before a booked arrival, the last a
// minute before it; without a booked time over two minutes from when the
// train heads for the stop.
constexpr double Lead = 600.0;
constexpr double LastBefore = 60.0;
constexpr double UnbookedSpread = 120.0;
// A train this near its stop (m along its route) draws its people to the
// edge zone; most of them (the rest stay where they are).
constexpr double ApproachDistance = 500.0;
constexpr float Approaching = 0.7f;
// Not seen: off the screen or this far (m; a person a few pixels high).
constexpr float SeenDistance = 200.0f;
// The most people at a station (each a crowd agent); room kept under it for
// those getting off.
constexpr int MostAtStation = 250;
constexpr int KeptForAlighting = 40;
// Seconds between people getting off one door; between a train's doors
// opening and its people boarding when none get off.
constexpr double AlightGap = 1.1;
constexpr double BoardAfter = 1.0;
// Getting on after the last one off has had time to step clear; one
// person into a door every this many seconds.
constexpr double AfterLastOff = 2.5;
constexpr double BoardGap = 0.7;
// A door closing: those this near it (m) still get in.
constexpr float StillGetIn = 4.0f;
// The steps the step clips are made for (humans-gen's steps.py): 0.35 m
// each, 1 to 3 (steps_up_1 .. steps_down_3).
constexpr float StepRise = 0.35f;
constexpr int MostCarSteps = 3;
// A door without door parts: the train's floor taken this far above the
// rail (a coach's).
constexpr float AssumedFloor = 1.1f;
// A car's place taken as current while drawn within the last this many ms.
constexpr qint64 DrawnLately = 1500;
// The longest step of the people's crowds (s), the most steps a frame.
constexpr float LongestStep = 0.1f;
constexpr int MostSteps = 5;
// Waiting people coming onto a platform: this share of them about its
// middle (the rest anywhere along it).
constexpr float MiddleShare = 0.6f;
// People in cars with seats (task humans 09): a seat picked among this many
// free ones nearest the door; the seat a sitting clip is made for, against
// the pelvis's drop in it (the mannequin's: 0.44 m against 0.335 m; our
// humans' thighs, sitting, 0.47 m up for the man's 0.355 m drop, 0.36 m for
// the young one's 0.279 m); the pelvis sitting this far behind the seat's
// place (a cushion's middle: its back, 0.08 to 0.15 m behind the pelvis,
// at a 0.45 m cushion's back); a car
// filled with its train's people as it comes into sight this far off (m),
// or at once on the first frame; its people gone once it has not been drawn
// for this long (ms).
constexpr int SeatChoice = 6;
constexpr float SeatPerDrop = 0.44f / 0.335f;
constexpr float SitBack = 0.1f;
constexpr float FillFarther = 60.0f;
constexpr qint64 CarGone = 4000;
// Cars filled within this reach of the camera (m), emptied beyond it (and
// the margin); filled anywhere just as they come into view (ms).
constexpr float RiderReach = 800.0f;
constexpr float RiderReachMargin = 100.0f;
constexpr qint64 JustShown = 600;
// A car's people walk to their door this long before it opens, and wait
// this far apart along the aisle by it (m).
constexpr float QueueGap = 0.55f;

// A repeatable random number in [0, 1) from a seed and what it is for.
float unitRandom(quint32 seed, quint32 salt) {
    quint32 x = seed * 0x9E3779B9u + salt * 0x85EBCA6Bu + 0x68E31DA4u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

quint32 mix(quint32 a, quint32 b) {
    return (a * 0x9E3779B1u) ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
}

// Detour's random numbers (findRandomPointAroundCircle) from our seeds.
quint32 detourSeed = 1;
float detourRandom() {
    detourSeed = mix(detourSeed, 0x2545F491u);
    return unitRandom(detourSeed, 7);
}

float distance2D(const float *a, const float *b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

// A person's heading turned toward the one wanted: eased (this part of the
// rest each second), never faster than TurnRate (rad/s); the user's review
// (2026-11-13): turns were harsh (snapped at a scripted walk's corners,
// following an agent's every swerve).
constexpr float TurnRate = 3.0f;
constexpr float TurnEase = 5.0f;
float turned(float way, float want, float dt, float ease = TurnEase) {
    const float delta = std::remainder(want - way, 2.0f * float(M_PI));
    const float most = TurnRate * std::max(dt, 0.0f);
    return way + std::clamp(delta * std::min(1.0f, dt * ease), -most, most);
}

void transformPoint(const float *m, const float *p, float *out) {
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

// A car's seats (task humans 09), read on a worker: its model's .sd (an S
// file's, a MaSzyna interior's) or a glTF's extras, where the car is drawn;
// null while being read, empty seats without them.
QMutex &seatsLock() {
    static QMutex mutex;
    return mutex;
}

std::shared_ptr<const ShapeSeats::Seats> carSeats(const QString &modelPath) {
    static QHash<QString, std::shared_ptr<const ShapeSeats::Seats>> read;
    static QSet<QString> reading;
    {
        QMutexLocker locked(&seatsLock());
        const auto found = read.constFind(modelPath);
        if (found != read.constEnd())
            return found.value();
        if (reading.contains(modelPath))
            return nullptr;
        reading.insert(modelPath);
    }
    QThreadPool::globalInstance()->start([modelPath]() {
        ShapeSeats::Seats seats;
        QStringList problems;
        ShapeSeats::readFor(modelPath, seats, &problems);
        if (!problems.isEmpty())
            qWarning().noquote() << "People: seats of" << modelPath << problems.join("; ");
        auto drawn = std::make_shared<const ShapeSeats::Seats>(seats.empty() ? ShapeSeats::Seats()
                                                                             : ShapeSeats::drawn(seats, modelPath));
        QMutexLocker locked(&seatsLock());
        reading.remove(modelPath);
        read.insert(modelPath, drawn);
    });
    return nullptr;
}

// A car's seats from all its models (the user, 2026-11-13: a car's
// interior may be a shape of its own): its shape's, then each freight
// animation's (an interior attached as FreightAnimStatic; a MaSzyna car's
// interior and attachments), moved where the car draws it (its offset, x
// and z mirrored, as Eng draws it); null while any is being read. The model
// holding the first seat into `model` (for the log).
std::shared_ptr<const ShapeSeats::Seats> carSeatsOf(const Eng &eng, QString *model = nullptr) {
    const VehicleDefinition &def = *eng.definition;
    if (def.freightAnims.isEmpty()) {
        if (model != nullptr)
            *model = def.shapePath;
        return carSeats(def.shapePath);
    }
    struct Part {
        std::shared_ptr<const ShapeSeats::Seats> seats;
        QString path;
        float offset[3];
    };
    std::vector<Part> parts;
    QString key = def.shapePath;
    auto add = [&](const QString &path, const float *offset) -> bool {
        Part part{carSeats(path), path, {-offset[0], offset[1], -offset[2]}};
        if (!part.seats)
            return false;
        key += QStringLiteral("|%1 %2 %3 %4").arg(path).arg(offset[0]).arg(offset[1]).arg(offset[2]);
        parts.push_back(std::move(part));
        return true;
    };
    const float none[3] = {0.0f, 0.0f, 0.0f};
    bool ready = add(def.shapePath, none);
    for (const VehicleDefinition::FreightAnim &anim : def.freightAnims)
        if (anim.shape.length() > 1)
            ready = add(anim.path, anim.offset) && ready;
    if (!ready)
        return nullptr;
    for (const Part &part : parts)
        if (!part.seats->empty()) {
            if (model != nullptr)
                *model = part.path;
            break;
        }
    static QHash<QString, std::shared_ptr<const ShapeSeats::Seats>> merged;
    QMutexLocker locked(&seatsLock());
    const auto found = merged.constFind(key);
    if (found != merged.constEnd())
        return found.value();
    auto out = std::make_shared<ShapeSeats::Seats>();
    for (const Part &part : parts) {
        if (part.seats->empty())
            continue;
        const float *o = part.offset;
        for (ShapeSeats::Seat seat : part.seats->seats) {
            for (int k = 0; k < 3; ++k)
                seat.position[k] += o[k];
            out->seats.push_back(seat);
        }
        for (ShapeSeats::Aisle aisle : part.seats->aisles) {
            aisle.x += o[0];
            aisle.y += o[1];
            aisle.z0 += o[2];
            aisle.z1 += o[2];
            out->aisles.push_back(aisle);
        }
        for (ShapeSeats::Passage passage : part.seats->passages) {
            for (int k = 0; k < 3; ++k)
                passage.position[k] += o[k];
            out->passages.push_back(passage);
        }
    }
    merged.insert(key, out);
    return out;
}

struct DtCrowdDeleter {
    void operator()(dtCrowd *crowd) const { dtFreeCrowd(crowd); }
};
struct DtQueryDeleter {
    void operator()(dtNavMeshQuery *query) const { dtFreeNavMeshQuery(query); }
};

}

struct StationPeople::Impl {
    // A station with people: its walk area's queries and crowd.
    struct Place {
        int station = -1;
        bool active = false;
        std::shared_ptr<const WalkArea::Built> built;
        std::unique_ptr<dtNavMeshQuery, DtQueryDeleter> query;
        std::unique_ptr<dtCrowd, DtCrowdDeleter> crowd;
        // Its frame's origin (global editor coordinates).
        double origin[3] = {0.0, 0.0, 0.0};
        // Per walk platform (side): its seeds on floor or ground, in order
        // along it, and each one's place along it (0 at its first, 1 at its
        // last, by the distance between them).
        std::vector<std::vector<int>> seeds;
        std::vector<std::vector<float>> seedAlong;
        int people = 0;
        // Just come near: its people due made at once.
        bool fresh = false;
    };
    // A train's people at one stop.
    struct Group {
        int train = -1;
        int stop = -1;
        int place = -1;
        int side = -1;
        int count = 0;
        double appear = 0.0;
        double spread = 0.0;
        int made = 0;
        quint32 seed = 0;
        bool approached = false;
        bool done = false;
        std::vector<int> people;
    };
    // A door of a standing train, in its station's frame: the car's middle
    // at the door (over the track), the threshold, and the platform's spot
    // in front of it.
    struct Door {
        int car = -1;
        // CarAnimation's group; -1 either (a door near a carriage's end).
        int group = -1;
        float inside[3] = {0, 0, 0};
        float threshold[3] = {0, 0, 0};
        float outside[3] = {0, 0, 0};
        dtPolyRef outsideRef = 0;
        // How far the walk area's mesh stood above the floor there (m; the
        // outside put on the floor, as those walking to it are).
        float outsideLift = 0.0f;
        bool open = false;
        bool wasOpen = false;
        int alight = 0;
        int alightMade = 0;
        double nextAlight = 0.0;
        double lastAlight = -1.0;
        double openedAt = -1.0;
        double nextBoard = 0.0;
        int assigned = 0;
        // In the car's space (where it is drawn): its middle along the car
        // and its threshold's height (NaN: none known).
        float localZ = 0.0f;
        float localBottom = std::numeric_limits<float>::quiet_NaN();
        // Its car's people getting off here, sent to it (task humans 09).
        int ridersOff = 0;
    };
    // A train standing at a stop of a station with people.
    struct Stand {
        int train = -1;
        int stop = -1;
        int place = -1;
        int side = -1;
        bool doorsKnown = false;
        double since = 0.0;
        double opened = -1.0;
        std::vector<Door> doors;
        // Those sent to get on so far, and when the next may go (the
        // simulation's pace: SecondsAPerson a door of its doors).
        int sentOn = 0;
        double nextOn = 0.0;
        // Its cars' people getting off sent to their doors (task humans 09).
        bool sentOff = false;
    };
    struct Person {
        bool used = false;
        int crowdId = -1;
        int place = -1;
        int side = -1;
        int group = -1;
        // AtDoor: waiting beside its door of a standing train (walking
        // there first); ToDoor: stepping up to it to get in.
        enum class State { Waiting, Approach, AtDoor, ToDoor, Entering, Exiting, Leaving } state = State::Waiting;
        int agent = -1;
        float pos[3] = {0, 0, 0};
        float way = 0.0f;
        float speed = 0.0f;
        float walkSpeed = 1.1f;
        // Its agent's velocity smoothed (x, z): the way it is turned to.
        float going[2] = {0.0f, 0.0f};
        bool moving = false;
        float target[3] = {0, 0, 0};
        // Scripted (in a door, in a car): points walked through, in order.
        std::vector<float> script;
        size_t leg = 0;
        // Its door (index in its stand's doors) and stand.
        int stand = -1;
        int door = -1;
        double nextIdea = 0.0;
        double slowSince = -1.0;
        quint32 seed = 0;
        bool faceTrack = false;
        float faceWay = 0.0f;
        // Left behind by a train (the simulation's count): waiting for the
        // next one at its platform side, since then.
        bool carried = false;
        double carriedSince = 0.0;
        // A step clip on its script's leg `clipLeg` (from its point there to
        // the next): the clip, its length, when it began (-1 not yet), the
        // rise it makes up beside the clip's own; playing now, how far in.
        int clipLeg = -1;
        int clip = -1;
        float clipLength = 0.0f;
        double clipStart = -1.0;
        float clipRise = 0.0f;
        bool playing = false;
        float playTime = 0.0f;
    };

    // A car with seats and its people (task humans 09), in its own space;
    // placed each frame where it was drawn.
    struct Car {
        int train = -1;
        int car = -1;
        const Eng *eng = nullptr;
        QString model;
        std::shared_ptr<const ShapeSeats::Seats> seats;
        // The rider on each seat, -1 free.
        std::vector<int> taken;
        bool filled = false;
        bool drawn = false;
        // When it last came into view (wall ms).
        qint64 shownAt = 0;
        float matrix[16];
        int tile[2] = {0, 0};
        qint64 lastDrawn = 0;
        int riders = 0;
    };
    struct Rider {
        bool used = false;
        int crowdId = -1;
        int car = -1;
        int seat = -1;
        enum class State { ToSeat, SittingDown, Sitting, StandingUp, ToDoor, AtDoor } state = State::ToSeat;
        // In the car's space; its way about +y (atan2 x, z).
        float pos[3] = {0, 0, 0};
        float way = 0.0f;
        float speed = 0.0f;
        float walkSpeed = 1.1f;
        std::vector<float> script;
        size_t leg = 0;
        // The seat's: where it stands to sit (its feet stay there), the floor
        // there, how low its place goes sitting, the way it faces sitting.
        float standAt[3] = {0, 0, 0};
        float sunk = 0.0f;
        float seatWay = 0.0f;
        // Its clip now (sit_down, sitting or stand_up) and when it began.
        int clip = -1;
        double clipStart = -1.0;
        float clipLength = 0.0f;
        int sittingClip = -1;
        float sittingLength = 1.0f;
        float phase = 0.0f;
        quint32 seed = 0;
        // Getting off: its stand and door there.
        int stand = -1;
        int door = -1;
    };

    Crowd crowd;
    HumanSet humans;
    std::vector<int> kinds;
    std::vector<float> kindWeights;
    const void *humansFor = nullptr;
    bool humansLoaded = false;
    int mode = -1;
    int quality = -1;
    bool settingsWatched = false;

    std::vector<Place> places;
    std::vector<Group> groups;
    QHash<qint64, int> groupOf;
    std::vector<Stand> stands;
    std::vector<Person> people;
    std::vector<int> freePeople;
    std::vector<Car> cars;
    // People stood on the floor under the walk area's mesh (WalkArea::
    // floorAt): how far the mesh stood above it since the last log line
    // (m), and where no floor was found.
    float meshLiftSum = 0.0f, meshLiftMax = 0.0f, doorLiftSum = 0.0f, doorLiftMax = 0.0f;
    int meshLifts = 0, floorMisses = 0, doorLifts = 0;
    std::vector<Rider> riders;
    std::vector<int> freeRiders;
    QHash<int, QPair<int, int>> sideOfItem;
    std::vector<const void *> sidesFor;

    const void *inputFor = nullptr;
    unsigned int runFor = 0;
    double lastTime = -1.0;
    bool firstFrame = true;
    // Cars filled wherever they are until then (wall ms): after a jump in
    // time or a reset, their seats read on workers by then.
    qint64 fillAnywhereUntil = 0;
    double now = 0.0;
    Activity *activity = nullptr;
    std::shared_ptr<const Sim::SimulationInput> in;
    std::shared_ptr<const Sim::SimulationFrame> simFrame;
    float playerT[2] = {0, 0};
    float playerW[3] = {0, 0, 0};
    const float *viewMatrix = nullptr;
    qint64 wallNow = 0;
    Stats figures;
    QElapsedTimer logClock;

    void readSettings();
    void reset();
    void loadHumans();
    void mapSides();
    void updatePlaces();
    void updateGroups();
    void updateStands();
    void makePeople();
    void stepPeople(float dt);
    void steerCrowd();

    bool seen(const Place &place, const float *local) const;
    bool spotFor(Place &place, int side, quint32 seed, float *out, dtPolyRef *ref);
    bool nearSeedSpot(Place &place, int side, const float *from, float inward, quint32 seed, float *out, dtPolyRef *ref);
    bool farEndSpot(Place &place, int side, const float *from, quint32 seed, float *out, dtPolyRef *ref);
    int newPerson(int place, int side, int group, const float *pos, quint32 seed, bool agent, int crowdId = -1);
    void removePerson(int index);
    bool addAgent(Person &p, int filter);
    void dropAgent(Person &p);
    void moveTo(Person &p, const float *target, dtPolyRef ref, int filter, float hurry = 1.0f);
    void leave(Person &p);
    void stayBehind(Person &p);
    void doorScript(Person &p, const Door &door, bool boarding);
    float railTopNear(const Place &place, int side, const float *pos) const;
    void adoptCarried();
    bool standing(int train, int stop) const;
    bool trainPast(int train, int stop) const;
    int kindFor(quint32 seed) const;
    void readDoors(Stand &stand);
    const StationWalkAreas::Station::Side *sideOf(const Place &place, int side) const;
    float trackWay(const Place &place, int side, const float *pos) const;
    bool onSide(const Place &place, int side, const float *pos) const;
    float alongOf(const Place &place, int side, const float *pos, float *length) const;
    bool pointAlong(const Place &place, int side, float along, float *out) const;
    void approach(Person &p, const Group &g);
    // People in cars (task humans 09).
    int carFor(int train, int carIndex);
    bool seatPerson(int personIndex);
    int newRider(int carIndex, quint32 seed, int crowdId);
    void removeRider(int index, bool keepCrowdPerson = false);
    bool sitPlan(Rider &r, const Car &car, int seat);
    void fillCars();
    void sendOff(int standIndex);
    int riderAtDoor(int standIndex, int doorIndex);
    void stepRiders(float dt);
    void steerRiders();
    void localToCrowd(const Car &car, const float *local, float way, float *at, float *worldWay) const;
};

StationPeople &StationPeople::instance() {
    static StationPeople people;
    return people;
}

StationPeople::StationPeople() : d(std::make_unique<Impl>()) {}

StationPeople::~StationPeople() = default;

const StationPeople::Stats &StationPeople::stats() const {
    return d->figures;
}

void StationPeople::clear() {
    d->reset();
    d->crowd.clear();
    d->kinds.clear();
    d->kindWeights.clear();
    d->humansFor = nullptr;
    d->humansLoaded = false;
    d->places.clear();
    d->sideOfItem.clear();
    d->sidesFor.clear();
    forgetCarDoors();
}

void StationPeople::Impl::readSettings() {
    mode = Settings::variant("core.content.people", SettingType::Enum).toInt();
    quality = Settings::variant("core.rendering.humanQuality", SettingType::Enum).toInt();
    crowd.setTuning(Crowd::quality(quality));
    if (settingsWatched)
        return;
    settingsWatched = true;
    QObject::connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged,
                     [this](const QStringList &keys) {
        if (!keys.contains("core.content.people") && !keys.contains("core.rendering.humanQuality"))
            return;
        // Everybody made again with the new humans or quality.
        StationPeople::instance().clear();
        mode = -1;
    });
}

void StationPeople::Impl::reset() {
    for (Person &p : people)
        if (p.used && p.crowdId >= 0)
            crowd.removePerson(p.crowdId);
    people.clear();
    freePeople.clear();
    for (Rider &r : riders)
        if (r.used && r.crowdId >= 0)
            crowd.removePerson(r.crowdId);
    riders.clear();
    freeRiders.clear();
    cars.clear();
    groups.clear();
    groupOf.clear();
    stands.clear();
    for (Place &place : places) {
        place.crowd.reset();
        place.people = 0;
        place.active = false;
    }
    lastTime = -1.0;
    firstFrame = true;
    fillAnywhereUntil = WorldObj::clock() + 3000;
    figures = Stats();
}

void StationPeople::Impl::loadHumans() {
    if (humansFor == Game::currentRoute)
        return;
    humansFor = Game::currentRoute;
    reset();
    places.clear();
    sideOfItem.clear();
    sidesFor.clear();
    crowd.clear();
    kinds.clear();
    kindWeights.clear();
    humansLoaded = false;
    const QString route = Game::root + "/ROUTES/" + Game::route;
    humans = HumanSet::forRoute(route, mode, HumanSet::assetsHumansFolder(), HumanSet::basicHumansFolder());
    for (const QString &problem : humans.problems)
        qWarning().noquote() << "People:" << problem;
    if (humans.empty()) {
        qInfo().noquote() << "People: no humans for" << Game::route
                          << (mode == HumanSet::Never ? "(setting: never)" : "(no humans.dat found)");
        return;
    }
    if (Game::currentShapeLib == nullptr)
        return;
    const HumanSet::List &list = humans.lists.front();
    ShapeLoadOptions options;
    options.insert(QString::fromLatin1(ShapeLoadOption::SkinnedTriangles), true);
    for (const HumanSet::Human &human : list.humans) {
        const int id = Game::currentShapeLib->addShape(human.path, humans.textures, options);
        const auto found = Game::currentShapeLib->shape.find(id);
        GltfShape *shape = found != Game::currentShapeLib->shape.end() ? dynamic_cast<GltfShape *>(found->second) : nullptr;
        if (shape == nullptr) {
            qWarning().noquote() << "People:" << human.path << "is not a glTF human";
            continue;
        }
        kinds.push_back(crowd.addKind(shape, list.clips));
        kindWeights.push_back(human.weight);
    }
    humansLoaded = !kinds.empty();
    qInfo().noquote() << QString("People: %1 humans from %2 (%3)")
                         .arg(kinds.size()).arg(humans.file)
                         .arg(humans.source == HumanSet::Source::Route ? "the route's"
                              : humans.source == HumanSet::Source::Assets ? "TSRE's from assets" : "TSRE's basic set");
}

int StationPeople::Impl::kindFor(quint32 seed) const {
    float total = 0.0f;
    for (float w : kindWeights)
        total += w;
    float pick = unitRandom(seed, 11) * total;
    for (size_t k = 0; k < kinds.size(); ++k) {
        pick -= kindWeights[k];
        if (pick < 0.0f)
            return kinds[k];
    }
    return kinds.back();
}

// Which station and walk platform a platform's track item is at.
void StationPeople::Impl::mapSides() {
    const std::vector<StationWalkAreas::Station> &stations = StationWalkAreas::instance().stations();
    // Again whenever a walk area is built, rebuilt or dropped.
    std::vector<const void *> built;
    for (const StationWalkAreas::Station &s : stations)
        built.push_back(s.state == StationWalkAreas::Station::State::Built ? s.built.get() : nullptr);
    if (built == sidesFor)
        return;
    sidesFor = built;
    sideOfItem.clear();
    for (size_t s = 0; s < stations.size(); ++s) {
        if (stations[s].state != StationWalkAreas::Station::State::Built)
            continue;
        for (size_t k = 0; k < stations[s].sides.size(); ++k) {
            const StationWalkAreas::Station::Side &side = stations[s].sides[k];
            // A platform with both sides: the first side found (a train
            // stands on one track; its people take its platform side by
            // where its doors open, the other side's people stay).
            if (!sideOfItem.contains(side.start))
                sideOfItem.insert(side.start, qMakePair(int(s), int(k)));
            if (!sideOfItem.contains(side.end))
                sideOfItem.insert(side.end, qMakePair(int(s), int(k)));
        }
    }
}

// The stations with built walk areas near the camera: their crowds made,
// those gone far away emptied.
void StationPeople::Impl::updatePlaces() {
    const std::vector<StationWalkAreas::Station> &stations = StationWalkAreas::instance().stations();
    if (places.size() != stations.size()) {
        reset();
        places.clear();
        places.resize(stations.size());
        for (size_t s = 0; s < stations.size(); ++s)
            places[s].station = int(s);
    }
    const double cx = double(playerT[0]) * 2048.0 + playerW[0], cz = double(playerT[1]) * 2048.0 + playerW[2];
    for (size_t s = 0; s < stations.size(); ++s) {
        Place &place = places[s];
        const StationWalkAreas::Station &station = stations[s];
        const double dist = std::hypot(station.middle[0] - cx, station.middle[1] - cz);
        const bool built = station.state == StationWalkAreas::Station::State::Built && station.built
                && station.built->mesh && station.built->reached > 0;
        // Its walk area dropped or built again: its people go too.
        const bool same = !place.active || station.built == place.built;
        const bool near = built && same && dist <= double(StationWalkAreas::Reach) + (place.active ? 100.0 : 0.0);
        if (place.active && !near) {
            // Out of reach: its people gone (made again when it is near).
            for (size_t i = 0; i < people.size(); ++i)
                if (people[i].used && people[i].place == int(s))
                    removePerson(int(i));
            for (Group &g : groups)
                if (g.place == int(s)) {
                    g.made = 0;
                    g.people.clear();
                    g.approached = false;
                }
            stands.erase(std::remove_if(stands.begin(), stands.end(), [&](const Stand &st) { return st.place == int(s); }),
                         stands.end());
            place.crowd.reset();
            place.active = false;
            continue;
        }
        if (!near || place.active)
            continue;
        place.built = station.built;
        std::copy(station.origin, station.origin + 3, place.origin);
        dtNavMesh *mesh = place.built->mesh.get();
        place.query.reset(dtAllocNavMeshQuery());
        if (!place.query || dtStatusFailed(place.query->init(mesh, 2048)))
            continue;
        place.crowd.reset(dtAllocCrowd());
        if (!place.crowd || !place.crowd->init(MostAtStation, 0.6f, mesh))
            continue;
        // Filter 0, waiting and walking about: the edge zone avoided;
        // filter 1, to and from the doors: the edge zone like the rest.
        for (int f = 0; f < 2; ++f) {
            dtQueryFilter *filter = place.crowd->getEditableFilter(f);
            filter->setIncludeFlags(WalkArea::FlagWalk);
            filter->setExcludeFlags(0);
            filter->setAreaCost(WalkArea::AreaEdge, f == 0 ? 20.0f : 1.0f);
        }
        place.seeds.assign(station.sides.size(), {});
        for (size_t i = 0; i < place.built->seeds.size(); ++i) {
            const WalkArea::Seed &seed = place.built->seeds[i];
            if (seed.kind != WalkArea::Seed::Missed && seed.platform >= 0 && size_t(seed.platform) < place.seeds.size())
                place.seeds[size_t(seed.platform)].push_back(int(i));
        }
        place.seedAlong.assign(place.seeds.size(), {});
        for (size_t side = 0; side < place.seeds.size(); ++side) {
            const std::vector<int> &list = place.seeds[side];
            std::vector<float> &along = place.seedAlong[side];
            float sum = 0.0f;
            for (size_t k = 0; k < list.size(); ++k) {
                if (k > 0)
                    sum += distance2D(place.built->seeds[size_t(list[k - 1])].position,
                                      place.built->seeds[size_t(list[k])].position);
                along.push_back(sum);
            }
            for (float &a : along)
                a = sum > 0.0f ? a / sum : 0.5f;
        }
        place.active = true;
        place.fresh = true;
        static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
        if (log) {
            // Where it is, as a capture view's tile and place take it.
            const int tx = int(std::floor((place.origin[0] + 1024.0) / 2048.0));
            const int tz = int(std::floor((place.origin[2] + 1024.0) / 2048.0));
            qInfo().noquote() << QString("People: %1 near, its frame at tile [%2, %3] pos [%4, %5, %6]")
                                 .arg(station.name).arg(tx).arg(tz).arg(place.origin[0] - tx * 2048.0, 0, 'f', 1)
                                 .arg(place.origin[1], 0, 'f', 1).arg(place.origin[2] - tz * 2048.0, 0, 'f', 1);
        }
    }
}

bool StationPeople::Impl::seen(const Place &place, const float *local) const {
    const float x = float(place.origin[0] - double(playerT[0]) * 2048.0) + local[0];
    const float y = float(place.origin[1]) + local[1] + 1.0f;
    const float z = float(place.origin[2] - double(playerT[1]) * 2048.0) + local[2];
    if (std::hypot(x - playerW[0], z - playerW[2]) > SeenDistance)
        return false;
    if (viewMatrix == nullptr)
        return true;
    const float *m = viewMatrix;
    const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (cw <= 0.0f)
        return false;
    // A person's width beyond the screen's edge still shows.
    return std::fabs(cx) <= 1.15f * cw && std::fabs(cy) <= 1.15f * cw;
}

// A place's walk platform, while its station's walk area is the one its
// people walk on; null else.
const StationWalkAreas::Station::Side *StationPeople::Impl::sideOf(const Place &place, int side) const {
    const std::vector<StationWalkAreas::Station> &stations = StationWalkAreas::instance().stations();
    if (place.station < 0 || size_t(place.station) >= stations.size())
        return nullptr;
    const StationWalkAreas::Station &station = stations[size_t(place.station)];
    if (station.built != place.built || side < 0 || size_t(side) >= station.sides.size())
        return nullptr;
    return &station.sides[size_t(side)];
}

// The way from a place to the nearest point of its walk platform's track.
float StationPeople::Impl::trackWay(const Place &place, int side, const float *pos) const {
    const StationWalkAreas::Station::Side *walk = sideOf(place, side);
    if (walk == nullptr)
        return 0.0f;
    const StationWalkAreas::Station::Side &w = *walk;
    const std::vector<float> &line = w.line;
    float best = std::numeric_limits<float>::max(), way = 0.0f;
    for (size_t k = 0; k + 2 < line.size(); k += 3) {
        const float d = distance2D(&line[k], pos);
        if (d < best) {
            best = d;
            way = std::atan2(line[k] - pos[0], line[k + 2] - pos[2]);
        }
    }
    return way;
}

// Whether a place is on a walk platform's side of its track (a train
// between two platforms has doors toward both).
bool StationPeople::Impl::onSide(const Place &place, int side, const float *pos) const {
    const StationWalkAreas::Station::Side *found = sideOf(place, side);
    if (found == nullptr)
        return false;
    const StationWalkAreas::Station::Side &walk = *found;
    float best = std::numeric_limits<float>::max(), across = 0.0f;
    for (size_t i = 0; i + 5 < walk.line.size(); i += 3) {
        const float *a = &walk.line[i], *b = &walk.line[i + 3];
        const float dx = b[0] - a[0], dz = b[2] - a[2];
        const float length = dx * dx + dz * dz;
        if (length <= 0.0f)
            continue;
        const float t = std::clamp(((pos[0] - a[0]) * dx + (pos[2] - a[2]) * dz) / length, 0.0f, 1.0f);
        const float px = a[0] + t * dx - pos[0], pz = a[2] + t * dz - pos[2];
        const float d = px * px + pz * pz;
        if (d < best) {
            best = d;
            // + toward (-dz, dx) from the line's way, as WalkArea's side.
            across = ((pos[0] - a[0]) * -dz + (pos[2] - a[2]) * dx) * walk.side;
        }
    }
    return across > 0.0f;
}

// A random place on a walk platform behind its edge zone: by a random seed
// of it, a random way in from the platform's edge.
bool StationPeople::Impl::spotFor(Place &place, int side, quint32 seed, float *out, dtPolyRef *ref) {
    if (side < 0 || size_t(side) >= place.seeds.size() || place.seeds[size_t(side)].empty())
        return false;
    const std::vector<int> &seeds = place.seeds[size_t(side)];
    const std::vector<float> &along = place.seedAlong[size_t(side)];
    // Where along the platform: MiddleShare of people from a bell about its
    // middle, the rest anywhere (the user, 2026-11-13: more in the middle,
    // not all; later near its entrances, task 07).
    float t = unitRandom(seed, 21);
    if (unitRandom(seed, 23) < MiddleShare)
        t = (unitRandom(seed, 24) + unitRandom(seed, 25) + unitRandom(seed, 26)) / 3.0f;
    const size_t at = size_t(std::lower_bound(along.begin(), along.end(), t) - along.begin());
    size_t pick = std::min(at, seeds.size() - 1);
    if (at > 0 && (at >= along.size() || t - along[at - 1] < along[at] - t))
        pick = at - 1;
    const WalkArea::Seed &s = place.built->seeds[size_t(seeds[pick])];
    const float in = s.kind == WalkArea::Seed::OnGround ? 0.3f + 1.6f * unitRandom(seed, 22) : 0.3f + 3.5f * unitRandom(seed, 22);
    return nearSeedSpot(place, side, s.position, in, seed, out, ref);
}

// From the seed nearest `from` on the walk platform, `inward` metres further
// in from where it stands (it stands 1 m in from the edge): a place off the
// edge zone, on the walk area.
bool StationPeople::Impl::nearSeedSpot(Place &place, int side, const float *from, float inward, quint32 seed,
                                       float *out, dtPolyRef *ref) {
    if (side < 0 || size_t(side) >= place.seeds.size() || place.seeds[size_t(side)].empty())
        return false;
    const StationWalkAreas::Station::Side *found = sideOf(place, side);
    if (found == nullptr)
        return false;
    const StationWalkAreas::Station::Side &walk = *found;
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i : place.seeds[size_t(side)]) {
        const float d = distance2D(place.built->seeds[size_t(i)].position, from);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    const WalkArea::Seed &s = place.built->seeds[size_t(best)];
    // In: away from the track's line.
    float nearest[3] = {s.position[0], s.position[1], s.position[2]};
    float lineDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 5 < walk.line.size(); i += 3) {
        const float *a = &walk.line[i], *b = &walk.line[i + 3];
        const float dx = b[0] - a[0], dz = b[2] - a[2];
        const float length = dx * dx + dz * dz;
        float t = length > 0.0f ? ((s.position[0] - a[0]) * dx + (s.position[2] - a[2]) * dz) / length : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float p[3] = {a[0] + t * dx, a[1], a[2] + t * dz};
        const float d = distance2D(p, s.position);
        if (d < lineDistance) {
            lineDistance = d;
            std::copy(p, p + 3, nearest);
        }
    }
    float nx = s.position[0] - nearest[0], nz = s.position[2] - nearest[2];
    const float length = std::hypot(nx, nz);
    if (length < 1e-3f)
        return false;
    nx /= length;
    nz /= length;
    const dtQueryFilter *filter = place.crowd->getFilter(0);
    const float extents[3] = {0.6f, 1.5f, 0.6f};
    for (int attempt = 0; attempt < 4; ++attempt) {
        const float in = inward + 0.5f * float(attempt);
        // A little along the platform too, so people do not stand in rows.
        const float along = (unitRandom(seed, 30 + quint32(attempt)) - 0.5f) * 1.6f;
        const float p[3] = {s.position[0] + nx * in - nz * along, s.position[1], s.position[2] + nz * in + nx * along};
        dtPolyRef r = 0;
        float at[3];
        if (dtStatusFailed(place.query->findNearestPoly(p, extents, filter, &r, at)) || r == 0)
            continue;
        unsigned char area = 0;
        place.built->mesh->getPolyArea(r, &area);
        if (area == WalkArea::AreaEdge)
            continue;
        std::copy(at, at + 3, out);
        *ref = r;
        return true;
    }
    return false;
}

// A place near the end of the walk platform farther from `from`: where people
// getting off walk to.
bool StationPeople::Impl::farEndSpot(Place &place, int side, const float *from, quint32 seed, float *out,
                                     dtPolyRef *ref) {
    if (side < 0 || size_t(side) >= place.seeds.size() || place.seeds[size_t(side)].empty())
        return false;
    const std::vector<int> &seeds = place.seeds[size_t(side)];
    const WalkArea::Seed &first = place.built->seeds[size_t(seeds.front())];
    const WalkArea::Seed &last = place.built->seeds[size_t(seeds.back())];
    // The farther end, mostly; sometimes the nearer (an exit there).
    const bool farther = distance2D(first.position, from) > distance2D(last.position, from);
    const bool pickFirst = unitRandom(seed, 40) < 0.75f ? farther : !farther;
    // A few seeds in from the end.
    const size_t back = std::min(seeds.size() - 1, size_t(2 + unitRandom(seed, 41) * 4.0f));
    const WalkArea::Seed &end = place.built->seeds[size_t(pickFirst ? seeds[back] : seeds[seeds.size() - 1 - back])];
    return nearSeedSpot(place, side, end.position, 0.5f + 2.5f * unitRandom(seed, 42), seed, out, ref);
}

int StationPeople::Impl::newPerson(int placeIndex, int side, int group, const float *pos, quint32 seed, bool agent,
                                   int crowdId) {
    Place &place = places[size_t(placeIndex)];
    // Waiting people leave room for those getting off.
    if ((crowdId < 0 && place.people >= MostAtStation - (group >= 0 ? KeptForAlighting : 0)) || kinds.empty())
        return -1;
    int index;
    if (!freePeople.empty()) {
        index = freePeople.back();
        freePeople.pop_back();
    } else {
        index = int(people.size());
        people.emplace_back();
    }
    Person &p = people[size_t(index)];
    p = Person();
    p.used = true;
    p.place = placeIndex;
    p.side = side;
    p.group = group;
    p.seed = seed;
    std::copy(pos, pos + 3, p.pos);
    p.way = unitRandom(seed, 50) * 2.0f * float(M_PI);
    Crowd::PersonSpec spec;
    spec.kind = kindFor(seed);
    spec.behaviour = Crowd::Behaviour::Steered;
    spec.scale = 0.94f + 0.12f * unitRandom(seed, 51);
    spec.seed = seed;
    // A rider stepping out of its car (task humans 09) stays who it was.
    p.crowdId = crowdId >= 0 ? crowdId : crowd.addPerson(spec);
    const float natural = crowd.walkSpeedOf(p.crowdId);
    p.walkSpeed = natural > 0.0f ? natural : 1.1f * spec.scale;
    p.nextIdea = now + 20.0 + 70.0 * double(unitRandom(seed, 52));
    ++place.people;
    if (agent)
        addAgent(p, 0);
    return index;
}

void StationPeople::Impl::removePerson(int index) {
    Person &p = people[size_t(index)];
    if (!p.used)
        return;
    dropAgent(p);
    if (p.crowdId >= 0)
        crowd.removePerson(p.crowdId);
    if (p.place >= 0 && size_t(p.place) < places.size())
        --places[size_t(p.place)].people;
    if (p.group >= 0 && size_t(p.group) < groups.size()) {
        std::vector<int> &list = groups[size_t(p.group)].people;
        list.erase(std::remove(list.begin(), list.end(), index), list.end());
    }
    p = Person();
    freePeople.push_back(index);
}

bool StationPeople::Impl::addAgent(Person &p, int filter) {
    Place &place = places[size_t(p.place)];
    if (p.agent >= 0 || !place.crowd)
        return p.agent >= 0;
    dtCrowdAgentParams params{};
    params.radius = 0.25f;
    params.height = 1.8f;
    params.maxAcceleration = 2.0f;
    params.maxSpeed = p.walkSpeed;
    params.collisionQueryRange = 2.0f;
    params.pathOptimizationRange = 15.0f;
    params.separationWeight = 1.5f;
    params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO
            | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    params.obstacleAvoidanceType = 0;
    params.queryFilterType = static_cast<unsigned char>(filter);
    p.agent = place.crowd->addAgent(p.pos, &params);
    return p.agent >= 0;
}

void StationPeople::Impl::dropAgent(Person &p) {
    if (p.agent < 0)
        return;
    Place &place = places[size_t(p.place)];
    if (place.crowd)
        place.crowd->removeAgent(p.agent);
    p.agent = -1;
}

void StationPeople::Impl::moveTo(Person &p, const float *target, dtPolyRef ref, int filter, float hurry) {
    if (!addAgent(p, filter))
        return;
    Place &place = places[size_t(p.place)];
    const dtCrowdAgent *agent = place.crowd->getAgent(p.agent);
    const float speed = p.walkSpeed * hurry;
    if (agent->params.queryFilterType != filter || agent->params.maxSpeed != speed) {
        dtCrowdAgentParams params = agent->params;
        params.queryFilterType = static_cast<unsigned char>(filter);
        params.maxSpeed = speed;
        place.crowd->updateAgentParameters(p.agent, &params);
    }
    std::copy(target, target + 3, p.target);
    place.crowd->requestMoveTarget(p.agent, ref, target);
    p.moving = true;
    p.slowSince = -1.0;
    p.faceTrack = false;
}

// Away along the platform, gone where nobody sees them.
void StationPeople::Impl::leave(Person &p) {
    p.state = Person::State::Leaving;
    p.stand = -1;
    p.door = -1;
    p.script.clear();
    if (p.group >= 0 && size_t(p.group) < groups.size()) {
        std::vector<int> &list = groups[size_t(p.group)].people;
        const int index = int(&p - people.data());
        list.erase(std::remove(list.begin(), list.end(), index), list.end());
    }
    p.group = -1;
    Place &place = places[size_t(p.place)];
    float spot[3];
    dtPolyRef ref = 0;
    if (farEndSpot(place, p.side, p.pos, mix(p.seed, 77), spot, &ref))
        moveTo(p, spot, ref, 0);
}

// Left behind: back from the edge, waiting for the next train there.
void StationPeople::Impl::stayBehind(Person &p) {
    const int index = int(&p - people.data());
    if (p.group >= 0 && size_t(p.group) < groups.size()) {
        std::vector<int> &list = groups[size_t(p.group)].people;
        list.erase(std::remove(list.begin(), list.end(), index), list.end());
    }
    p.group = -1;
    p.stand = -1;
    p.door = -1;
    p.script.clear();
    p.state = Person::State::Waiting;
    p.carried = true;
    p.carriedSince = now;
    p.nextIdea = now + 20.0 + 40.0 * double(unitRandom(p.seed, 90));
    Place &place = places[size_t(p.place)];
    float spot[3];
    dtPolyRef ref = 0;
    if (nearSeedSpot(place, p.side, p.pos, 0.6f + 2.0f * unitRandom(p.seed, 91), mix(p.seed, 92), spot, &ref)) {
        p.state = Person::State::Approach;
        moveTo(p, spot, ref, 0);
    } else if (p.agent >= 0 && place.crowd) {
        place.crowd->resetMoveTarget(p.agent);
        p.moving = false;
    }
}

// Those left behind join the next train's people at their platform side;
// given up after half an hour without one.
void StationPeople::Impl::adoptCarried() {
    for (size_t i = 0; i < people.size(); ++i) {
        Person &p = people[i];
        if (!p.used || !p.carried)
            continue;
        int best = -1;
        for (size_t g = 0; g < groups.size(); ++g)
            if (!groups[g].done && groups[g].place == p.place && groups[g].side == p.side
                    && (best < 0 || groups[g].appear < groups[size_t(best)].appear))
                best = int(g);
        if (best >= 0) {
            p.carried = false;
            p.group = best;
            groups[size_t(best)].people.push_back(int(i));
            if (groups[size_t(best)].approached && p.state == Person::State::Waiting)
                approach(p, groups[size_t(best)]);
        } else if (now - p.carriedSince > 1800.0) {
            p.carried = false;
            leave(p);
        }
    }
}

// A door's way for a person: boarding, from where it is to the door's
// front, up the car's steps (the step clip for the floor's height, its
// place held while the clip carries the body), on to the car's middle;
// getting off, from the car's middle to the threshold, down the steps, to
// the platform's spot. Without a step clip (none fits, or none in the
// clips file) the rise is walked.
void StationPeople::Impl::doorScript(Person &p, const Door &door, bool boarding) {
    p.script.clear();
    p.leg = 0;
    p.clipLeg = -1;
    p.clip = -1;
    p.clipStart = -1.0;
    p.playing = false;
    auto push = [&](const float *q) { p.script.insert(p.script.end(), q, q + 3); };
    float ux = door.threshold[0] - door.outside[0], uz = door.threshold[2] - door.outside[2];
    float across = std::hypot(ux, uz);
    if (across < 1e-3f) {
        ux = door.inside[0] - door.outside[0];
        uz = door.inside[2] - door.outside[2];
        across = std::max(std::hypot(ux, uz), 1e-3f);
    }
    ux /= across;
    uz /= across;
    const float rise = door.threshold[1] - door.outside[1];
    const int steps = std::clamp(int(std::lround(rise / StepRise)), 0, MostCarSteps);
    int clip = -1;
    float travel[3] = {0.0f, 0.0f, 0.0f};
    if (steps > 0) {
        const int kind = crowd.kindOf(p.crowdId);
        clip = crowd.clipNamed(kind, QString("steps_%1_%2").arg(boarding ? "up" : "down").arg(steps));
        const float way = boarding ? std::atan2(ux, uz) : std::atan2(-ux, -uz);
        if (clip < 0 || !crowd.clipTravelOf(p.crowdId, clip, way, travel))
            clip = -1;
        else
            p.clipLength = crowd.clipLength(kind, clip);
    }
    if (boarding) {
        push(p.pos);
        push(door.outside);
        if (clip >= 0) {
            // Begun so far short of the threshold that it ends there.
            const float along = std::hypot(travel[0], travel[2]);
            float start[3] = {door.outside[0], door.outside[1], door.outside[2]};
            if (across > along) {
                start[0] = door.threshold[0] - ux * along;
                start[2] = door.threshold[2] - uz * along;
                push(start);
            }
            p.clipLeg = int(p.script.size() / 3) - 1;
            const float end[3] = {start[0] + travel[0], door.threshold[1], start[2] + travel[2]};
            push(end);
            p.clipRise = (door.threshold[1] - start[1]) - travel[1];
        } else {
            push(door.threshold);
        }
        push(door.inside);
    } else {
        push(door.inside);
        push(door.threshold);
        if (clip >= 0) {
            p.clipLeg = 1;
            const float end[3] = {door.threshold[0] + travel[0], door.outside[1], door.threshold[2] + travel[2]};
            push(end);
            p.clipRise = (door.outside[1] - door.threshold[1]) - travel[1];
        }
        push(door.outside);
    }
    p.clip = clip;
}

// The rail's top (the track line's height) by a place of a walk platform.
float StationPeople::Impl::railTopNear(const Place &place, int side, const float *pos) const {
    const StationWalkAreas::Station::Side *walk = sideOf(place, side);
    if (walk == nullptr || walk->line.size() < 3)
        return pos[1];
    float best = std::numeric_limits<float>::max(), y = walk->line[1];
    for (size_t k = 0; k + 2 < walk->line.size(); k += 3) {
        const float d = distance2D(&walk->line[k], pos);
        if (d < best) {
            best = d;
            y = walk->line[k + 1];
        }
    }
    return y;
}

bool StationPeople::Impl::standing(int train, int stop) const {
    if (!simFrame || size_t(train) >= simFrame->trains.size())
        return false;
    // An AI train standing there, or a driven one from standing at its
    // platform until it moves off.
    return simFrame->trains[size_t(train)].state.atStation == stop;
}

bool StationPeople::Impl::trainPast(int train, int stop) const {
    if (!simFrame || size_t(train) >= simFrame->trains.size())
        return true;
    const Sim::TrainState &s = simFrame->trains[size_t(train)].state;
    if (s.status == Sim::TrainStatus::Finished)
        return true;
    if (s.nextStop > stop + 1)
        return true;
    return s.nextStop == stop + 1 && s.atStation != stop;
}

// Each train's stops at stations with people: their groups made when due,
// drawn to the edge as the train comes, done once it has left.
void StationPeople::Impl::updateGroups() {
    for (size_t t = 0; t < in->trains.size() && t < simFrame->trains.size(); ++t) {
        const Sim::TrainPlan &plan = in->trains[t];
        const Sim::TrainState &state = simFrame->trains[t].state;
        if (state.status == Sim::TrainStatus::Finished)
            continue;
        // The stops it has still to make (the one it stands at included).
        const int from = std::max(0, state.nextStop - 1);
        for (int s = from; s < int(plan.stops.size()); ++s) {
            const qint64 key = qint64(t) * 4096 + s;
            if (groupOf.contains(key))
                continue;
            const Sim::StopPlan &stop = plan.stops[size_t(s)];
            const auto found = sideOfItem.constFind(stop.platformItem);
            if (found == sideOfItem.constEnd())
                continue;
            const int placeIndex = found.value().first, side = found.value().second;
            if (size_t(placeIndex) >= places.size() || !places[size_t(placeIndex)].active)
                continue;
            if (trainPast(int(t), s))
                continue;
            Group g;
            g.train = int(t);
            g.stop = s;
            g.place = placeIndex;
            g.side = side;
            g.seed = mix(mix(quint32(t) * 7919u + 17u, quint32(s)), quint32(stop.platformItem));
            if (stop.arrival >= 0) {
                g.appear = double(stop.arrival) - Lead;
                g.spread = Lead - LastBefore;
            } else {
                // Without a booked time: from when it heads here.
                if (state.nextStop != s || state.status == Sim::TrainStatus::Waiting)
                    continue;
                g.appear = now;
                g.spread = UnbookedSpread;
            }
            if (now < g.appear)
                continue;
            // As many as the simulation has waiting for it (none for a
            // train ending here or without passenger doors).
            g.count = stop.passengers;
            groupOf.insert(key, int(groups.size()));
            groups.push_back(g);
        }
    }
    // Drawn to the edge as their train comes; gone once it has left.
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        Group &g = groups[gi];
        if (g.done)
            continue;
        if (trainPast(g.train, g.stop)) {
            g.done = true;
            // As many as the simulation left behind stay for the next
            // train there; the rest walk away.
            const Sim::TrainState::Passengers &counted = simFrame->trains[size_t(g.train)].state.passengers;
            int stay = counted.stop == g.stop && counted.left ? counted.missed : 0;
            static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
            if (log && counted.stop == g.stop)
                qInfo().noquote() << QString("People: train %1 left stop %2: the simulation counts %3 waiting, %4 got on, "
                                             "%5 left behind, %6 got off; drawn: %7 still on the platform")
                                     .arg(g.train).arg(g.stop).arg(counted.waiting).arg(counted.boarded)
                                     .arg(counted.missed).arg(counted.alighted).arg(g.people.size());
            const std::vector<int> list = g.people;
            for (int i : list) {
                Person &p = people[size_t(i)];
                const bool onPlatform = p.state == Person::State::Waiting || p.state == Person::State::Approach
                        || p.state == Person::State::AtDoor || p.state == Person::State::ToDoor;
                if (onPlatform && stay > 0) {
                    --stay;
                    stayBehind(p);
                } else {
                    leave(p);
                }
            }
            continue;
        }
        if (g.approached || g.place < 0 || !places[size_t(g.place)].active)
            continue;
        const Sim::TrainState &state = simFrame->trains[size_t(g.train)].state;
        const Sim::StopPlan &stop = in->trains[size_t(g.train)].stops[size_t(g.stop)];
        const bool near = state.nextStop == g.stop && state.part == stop.part && stop.front - state.front < ApproachDistance
                && state.status == Sim::TrainStatus::Running;
        if (!near && !standing(g.train, g.stop))
            continue;
        g.approached = true;
        for (int i : g.people) {
            Person &p = people[size_t(i)];
            if (p.state == Person::State::Waiting && unitRandom(p.seed, 60) < Approaching)
                approach(p, g);
        }
    }
}

// How far along a walk platform's track line a place is (its projection),
// and the line's length.
float StationPeople::Impl::alongOf(const Place &place, int side, const float *pos, float *length) const {
    static const std::vector<float> none;
    const StationWalkAreas::Station::Side *walk = sideOf(place, side);
    const std::vector<float> &line = walk != nullptr ? walk->line : none;
    float walked = 0.0f, best = std::numeric_limits<float>::max(), along = 0.0f;
    for (size_t i = 0; i + 5 < line.size(); i += 3) {
        const float *a = &line[i], *b = &line[i + 3];
        const float dx = b[0] - a[0], dz = b[2] - a[2];
        const float segment = std::hypot(dx, dz);
        if (segment > 0.0f) {
            const float t = std::clamp(((pos[0] - a[0]) * dx + (pos[2] - a[2]) * dz) / (segment * segment), 0.0f, 1.0f);
            const float px = a[0] + t * dx - pos[0], pz = a[2] + t * dz - pos[2];
            const float d = px * px + pz * pz;
            if (d < best) {
                best = d;
                along = walked + t * segment;
            }
        }
        walked += segment;
    }
    if (length != nullptr)
        *length = walked;
    return along;
}

bool StationPeople::Impl::pointAlong(const Place &place, int side, float along, float *out) const {
    const StationWalkAreas::Station::Side *walk = sideOf(place, side);
    if (walk == nullptr)
        return false;
    const std::vector<float> &line = walk->line;
    float walked = 0.0f;
    for (size_t i = 0; i + 5 < line.size(); i += 3) {
        const float *a = &line[i], *b = &line[i + 3];
        const float segment = std::hypot(b[0] - a[0], b[2] - a[2]);
        if (walked + segment >= along || i + 6 >= line.size()) {
            const float t = segment > 0.0f ? std::clamp((along - walked) / segment, 0.0f, 1.0f) : 0.0f;
            for (int c = 0; c < 3; ++c)
                out[c] = a[c] + t * (b[c] - a[c]);
            return true;
        }
        walked += segment;
    }
    return false;
}

// As its train comes: up to the edge zone, within the stretch the train
// will stand at (Open Rails stops trains centred on the platform: its
// middle, a train's length long); from beyond it, into it.
void StationPeople::Impl::approach(Person &p, const Group &g) {
    Place &place = places[size_t(p.place)];
    float length = 0.0f;
    const float along = alongOf(place, p.side, p.pos, &length);
    const float train = std::max(10.0f, in->trains[size_t(g.train)].length);
    const float lo = std::max(0.0f, 0.5f * (length - train)), hi = std::min(length, 0.5f * (length + train));
    float target = along;
    if (along < lo + 1.0f || along > hi - 1.0f) {
        const float into = 2.0f + 6.0f * unitRandom(p.seed, 62);
        target = along < lo + 1.0f ? std::min(lo + into, hi) : std::max(hi - into, lo);
    }
    float from[3];
    if (!pointAlong(place, p.side, target, from))
        std::copy(p.pos, p.pos + 3, from);
    float spot[3];
    dtPolyRef ref = 0;
    if (nearSeedSpot(place, p.side, from, 0.15f + 1.0f * unitRandom(p.seed, 61), p.seed, spot, &ref)) {
        p.state = Person::State::Approach;
        moveTo(p, spot, ref, 0, std::fabs(target - along) > 15.0f ? 1.15f : 1.0f);
    }
}

// Where a standing train's doors are on the platform's side: its cars as
// last drawn, their door parts (else a door near each end of a carriage).
void StationPeople::Impl::readDoors(Stand &stand) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    Consist *consist = simulation->consistOf(stand.train);
    if (consist == nullptr)
        return;
    Place &place = places[size_t(stand.place)];
    std::vector<Door> doors;
    const Sim::TrainPlan &plan = in->trains[size_t(stand.train)];
    // A multiple unit (a passenger train without carriages): its powered
    // cars carry people too.
    bool carriages = false;
    for (int v = 0; v < consist->engItems.size(); ++v) {
        const Eng *eng = consist->engItems[v].engPointer;
        carriages = carriages || (eng != nullptr && eng->definition && eng->definition->wagonTypeId == 1);
    }
    const bool multipleUnit = plan.passenger && !carriages;
    for (int v = 0; v < consist->engItems.size(); ++v) {
        Eng *eng = consist->engItems[v].engPointer;
        if (eng == nullptr || !eng->definition)
            continue;
        const int type = eng->definition->wagonTypeId;
        if (type == 2 || type == 3)
            continue;
        // A MaSzyna car: its doors by its .mmd's prefix and count.
        const MaszynaVehicle *maszyna = eng->definition->maszyna.get();
        std::shared_ptr<const CarDoors> parts = maszyna != nullptr
                ? carDoors(eng->definition->shapePath, eng->path, maszyna->prefixes.value(QStringLiteral("animdoorprefix")),
                           maszyna->animations[1])
                : carDoors(eng->definition->shapePath, eng->path);
        if (!parts)
            return;  // read on a worker: the doors known on a later frame
        float m[16];
        int tile[2];
        qint64 when = 0;
        if (!eng->lastDrawn(m, tile, &when) || wallNow - when > DrawnLately)
            return;
        std::vector<CarDoors::Door> carDoors = parts->doors;
        static const bool logCars = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
        if (logCars) {
            QStringList list;
            for (const CarDoors::Door &cd : carDoors)
                list << QString("%1 x %2 z %3 w %4 y %5").arg(cd.group == CarAnimation::DoorLeft ? "L" : "R")
                        .arg(cd.x, 0, 'f', 2).arg(cd.z, 0, 'f', 2).arg(cd.width, 0, 'f', 2).arg(cd.bottom, 0, 'f', 2);
            qInfo().noquote() << QString("People: car %1 %2 (type %3): %4 doors %5 (%6)").arg(v)
                                 .arg(QFileInfo(eng->definition->shapePath).fileName()).arg(type)
                                 .arg(carDoors.size()).arg(list.join(", "), parts->note);
        }
        if (carDoors.empty()) {
            // A carriage (a multiple unit's car) without door parts: a door
            // near each end, both sides.
            if (type != 1 && !(multipleUnit && type >= 4))
                continue;
            const float length = eng->definition->size[2] > 0.0f ? eng->definition->size[2] : 20.0f;
            const float width = eng->definition->size[0] > 0.0f ? eng->definition->size[0] : 3.0f;
            for (const float z : {0.5f * length - 1.2f, -0.5f * length + 1.2f})
                for (const float x : {0.5f * width, -0.5f * width}) {
                    CarDoors::Door door;
                    door.group = -1;
                    door.z = z;
                    door.x = x;
                    door.bottom = std::numeric_limits<float>::quiet_NaN();
                    carDoors.push_back(door);
                }
        }
        const double ox = double(tile[0]) * 2048.0 - place.origin[0];
        const double oz = double(tile[1]) * 2048.0 - place.origin[2];
        for (const CarDoors::Door &cd : carDoors) {
            const float bottom = std::isnan(cd.bottom) ? 1.0f : cd.bottom;
            const float a[3] = {0.0f, bottom, cd.z}, b[3] = {cd.x, bottom, cd.z};
            float inside[3], threshold[3];
            transformPoint(m, a, inside);
            transformPoint(m, b, threshold);
            for (float *p : {inside, threshold}) {
                p[0] = float(double(p[0]) + ox);
                p[1] = float(double(p[1]) - place.origin[1]);
                p[2] = float(double(p[2]) + oz);
            }
            float ux = threshold[0] - inside[0], uz = threshold[2] - inside[2];
            const float across = std::hypot(ux, uz);
            if (across < 0.3f)
                continue;
            ux /= across;
            uz /= across;
            // The platform in front of it: the first walk area going out.
            Door door;
            bool found = false;
            const float extents[3] = {0.35f, 1.6f, 0.35f};
            for (float out = 0.3f; out <= 2.5f && !found; out += 0.2f) {
                const float probe[3] = {threshold[0] + ux * out, threshold[1], threshold[2] + uz * out};
                dtPolyRef ref = 0;
                float at[3];
                if (dtStatusFailed(place.query->findNearestPoly(probe, extents, place.crowd->getFilter(1), &ref, at)) || ref == 0)
                    continue;
                if (distance2D(at, probe) > 0.3f)
                    continue;
                std::copy(at, at + 3, door.outside);
                door.outsideRef = ref;
                found = true;
                // On the floor, not on the mesh above it: the way to the
                // door and its steps start there (people rose to the mesh
                // as they walked to the train, the user's review).
                float floor = 0.0f;
                if (place.built->floorAt(at[0], at[1], at[2], &floor)) {
                    door.outsideLift = at[1] - floor;
                    door.outside[1] = floor;
                }
            }
            if (!found || !onSide(place, stand.side, door.outside))
                continue;
            // The train's floor at the threshold (the user); a door near a
            // carriage's end without parts: level with the platform.
            if (std::isnan(cd.bottom)) {
                const float floor = std::max(door.outside[1], railTopNear(place, stand.side, threshold) + AssumedFloor);
                threshold[1] = floor;
                inside[1] = floor;
            }
            door.car = v;
            door.group = cd.group;
            door.localZ = cd.z;
            door.localBottom = cd.bottom;
            std::copy(inside, inside + 3, door.inside);
            std::copy(threshold, threshold + 3, door.threshold);
            doors.push_back(door);
        }
    }
    // Those getting off, as the simulation counts them, shared out over
    // the doors.
    const Sim::TrainState::Passengers &counted = simFrame->trains[size_t(stand.train)].state.passengers;
    const int off = counted.stop == stand.stop ? counted.alighting : 0;
    for (size_t k = 0; k < doors.size(); ++k)
        doors[k].alight = off / int(doors.size()) + (int(k) < off % int(doors.size()) ? 1 : 0);
    stand.doors = std::move(doors);
    stand.doorsKnown = true;
    static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
    if (log) {
        QStringList where;
        for (const Door &door : stand.doors) {
            const double gx = place.origin[0] + door.outside[0], gz = place.origin[2] + door.outside[2];
            const int tx = int(std::floor((gx + 1024.0) / 2048.0)), tz = int(std::floor((gz + 1024.0) / 2048.0));
            where << QString("car %1%2 tile [%3, %4] pos [%5, %6, %7] floor %8 (the mesh %9 cm above the platform)")
                     .arg(door.car).arg(door.group < 0 ? " (end)" : "").arg(tx).arg(tz)
                     .arg(gx - tx * 2048.0, 0, 'f', 1).arg(place.origin[1] + door.outside[1], 0, 'f', 1)
                     .arg(gz - tz * 2048.0, 0, 'f', 1).arg(door.threshold[1] - door.outside[1], 0, 'f', 2)
                     .arg(100.0 * door.outsideLift, 0, 'f', 1);
        }
        qInfo().noquote() << QString("People: train %1 at stop %2: %3 doors on the platform: %4")
                             .arg(stand.train).arg(stand.stop).arg(stand.doors.size()).arg(where.join("; "));
    }
}

// Trains standing at stations with people: their doors found, people off
// and on while they are open; boarding stopped as they close or the train
// leaves.
void StationPeople::Impl::updateStands() {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    // Stands begun.
    for (Group &g : groups) {
        if (g.done || g.place < 0 || !places[size_t(g.place)].active || !standing(g.train, g.stop))
            continue;
        bool known = false;
        for (const Stand &st : stands)
            known = known || (st.train == g.train && st.stop == g.stop);
        if (known)
            continue;
        Stand st;
        st.train = g.train;
        st.stop = g.stop;
        st.place = g.place;
        st.side = g.side;
        st.since = now;
        stands.push_back(st);
    }
    for (size_t si = 0; si < stands.size();) {
        Stand &st = stands[si];
        if (!standing(st.train, st.stop) || !places[size_t(st.place)].active) {
            // It leaves: those in its doors are in, the rest walk away.
            for (size_t i = 0; i < people.size(); ++i) {
                Person &p = people[i];
                if (!p.used || p.stand != int(si))
                    continue;
                if (p.state == Person::State::Entering) {
                    if (!seatPerson(int(i)))
                        removePerson(int(i));
                    ++figures.boarded;
                } else if (p.state == Person::State::ToDoor || p.state == Person::State::AtDoor) {
                    leave(p);
                } else {
                    p.stand = -1;
                    p.door = -1;
                }
            }
            // Its cars' people who did not get off: back to a seat (or gone).
            for (int ri = 0; ri < int(riders.size()); ++ri) {
                Rider &r = riders[size_t(ri)];
                if (!r.used || r.stand != int(si))
                    continue;
                r.stand = -1;
                r.door = -1;
                if (r.state == Rider::State::StandingUp && r.seat >= 0)
                    continue;  // still in its seat
                Car &car = cars[size_t(r.car)];
                int seat = -1;
                for (int k = 0; k < int(car.taken.size()) && seat < 0; ++k)
                    if (car.taken[size_t(k)] < 0)
                        seat = k;
                if (seat >= 0 && sitPlan(r, car, seat))
                    car.taken[size_t(seat)] = ri;
                else
                    removeRider(ri);
            }
            // The stands after it move down one.
            for (Person &p : people)
                if (p.used && p.stand > int(si))
                    --p.stand;
            for (Rider &r : riders)
                if (r.used && r.stand > int(si))
                    --r.stand;
            stands.erase(stands.begin() + long(si));
            continue;
        }
        if (!st.doorsKnown)
            readDoors(st);
        if (st.doorsKnown && !st.sentOff) {
            st.sentOff = true;
            sendOff(int(si));
        }
        Consist *consist = simulation->consistOf(st.train);
        Place &place = places[size_t(st.place)];
        // Its doors known: its people go and wait beside them, the nearest
        // door, the busier ones a little farther.
        if (st.doorsKnown && !st.doors.empty()) {
            const auto found = groupOf.constFind(qint64(st.train) * 4096 + st.stop);
            if (found != groupOf.constEnd()) {
                const std::vector<int> list = groups[size_t(found.value())].people;
                for (int i : list) {
                    Person &p = people[size_t(i)];
                    if (p.state != Person::State::Waiting && p.state != Person::State::Approach)
                        continue;
                    int best = -1;
                    float bestCost = std::numeric_limits<float>::max();
                    for (size_t k = 0; k < st.doors.size(); ++k) {
                        const float cost = distance2D(st.doors[k].outside, p.pos) + 2.0f * float(st.doors[k].assigned);
                        if (cost < bestCost) {
                            bestCost = cost;
                            best = int(k);
                        }
                    }
                    Door &door = st.doors[size_t(best)];
                    // Beside it, either side in turn, out from the edge.
                    float ux = door.outside[0] - door.inside[0], uz = door.outside[2] - door.inside[2];
                    const float across = std::max(std::hypot(ux, uz), 1e-3f);
                    ux /= across;
                    uz /= across;
                    const int rank = door.assigned++;
                    const float sideSign = (rank % 2) != 0 ? 1.0f : -1.0f;
                    const float along = std::min(3.5f, 0.9f + 0.45f * float(rank / 2)) * sideSign;
                    const float out = 0.5f + 0.35f * float((rank / 2) % 3);
                    const float spot[3] = {door.outside[0] - uz * along + ux * out, door.outside[1],
                                           door.outside[2] + ux * along + uz * out};
                    const float extents[3] = {0.8f, 1.5f, 0.8f};
                    dtPolyRef ref = 0;
                    float at[3];
                    p.stand = int(si);
                    p.door = best;
                    p.state = Person::State::AtDoor;
                    if (dtStatusSucceed(place.query->findNearestPoly(spot, extents, place.crowd->getFilter(1), &ref, at)) && ref != 0)
                        moveTo(p, at, ref, 1, 1.2f);
                }
            }
        }
        // How many may get on, as the simulation counts them: an AI train's
        // all through its doors by the time it leaves (its departure known),
        // a driven train's so far (its driver's doors); at its pace.
        const Sim::TrainState &trainState = simFrame->trains[size_t(st.train)].state;
        const Sim::TrainState::Passengers &counted = trainState.passengers;
        int mayGetOn = 0;
        double rate = 1.0;
        if (counted.stop == st.stop && counted.doors > 0) {
            const bool driven = simFrame->driven == st.train;
            const double open = driven ? counted.doorsOpenSeconds
                    : (trainState.until - Sim::SimulationCore::DoorsCloseBefore)
                      - (counted.arrived + Sim::SimulationCore::DoorsOpenAfter);
            const int through = int(std::max(0.0, open - Sim::SimulationCore::BoardingMargin) * double(counted.doors)
                                    / Sim::SimulationCore::SecondsAPerson);
            mayGetOn = std::min(counted.waiting, std::max(0, through - counted.alighting));
            rate = Sim::SimulationCore::SecondsAPerson / double(counted.doors);
        }
        for (Door &door : st.doors) {
            const int doorIndex = int(&door - st.doors.data());
            Eng *eng = consist != nullptr && door.car < consist->engItems.size() ? consist->engItems[door.car].engPointer : nullptr;
            door.open = eng != nullptr
                    && (door.group < 0 ? eng->isAnimated(CarAnimation::DoorLeft) || eng->isAnimated(CarAnimation::DoorRight)
                                       : eng->isAnimated(CarAnimation::Group(door.group)));
            if (door.open && !door.wasOpen) {
                door.nextAlight = now + 0.8;
                door.openedAt = now;
                if (st.opened < 0.0)
                    st.opened = now;
            }
            if (!door.open && door.wasOpen) {
                // Closed: those near it still get in, the others stay by it.
                for (Person &p : people)
                    if (p.used && p.stand == int(si) && p.door == doorIndex && p.state == Person::State::ToDoor
                            && distance2D(p.pos, door.outside) > StillGetIn) {
                        p.state = Person::State::AtDoor;
                        p.moving = false;
                        if (p.agent >= 0)
                            place.crowd->resetMoveTarget(p.agent);
                    }
            }
            door.wasOpen = door.open;
            // People getting off: out of the car's middle, through the door.
            while (door.open && door.alightMade < door.alight && now >= door.nextAlight) {
                quint32 seed = mix(mix(quint32(st.train) * 131u, quint32(st.stop)),
                                   quint32(door.car * 977 + door.alightMade * 13 + doorIndex));
                // Its car's people sent to it first (task humans 09), each
                // once at the door; the rest out of the car's middle.
                int crowdId = -1;
                if (door.ridersOff > 0) {
                    const int ri = riderAtDoor(int(si), doorIndex);
                    if (ri < 0)
                        break;
                    crowdId = riders[size_t(ri)].crowdId;
                    seed = riders[size_t(ri)].seed;
                    removeRider(ri, true);
                    --door.ridersOff;
                    // Those behind it a place on.
                    for (Rider &o : riders)
                        if (o.used && o.stand == int(si) && o.door == doorIndex && o.state == Rider::State::AtDoor) {
                            const float step = o.pos[2] > door.localZ ? -QueueGap : QueueGap;
                            const float on[3] = {o.pos[0], o.pos[1], o.pos[2] + step};
                            o.script.assign(o.pos, o.pos + 3);
                            o.script.insert(o.script.end(), on, on + 3);
                            o.leg = 0;
                            o.state = Rider::State::ToDoor;
                        }
                }
                const int index = newPerson(st.place, st.side, -1, door.inside, seed, false, crowdId);
                ++door.alightMade;
                door.lastAlight = now;
                door.nextAlight += AlightGap * (0.8 + 0.4 * double(unitRandom(seed, 80)));
                if (index < 0)
                    break;
                Person &p = people[size_t(index)];
                p.state = Person::State::Exiting;
                p.stand = int(si);
                p.door = doorIndex;
                doorScript(p, door, false);
                ++figures.alighted;
            }
            // Getting on once the last off is clear: one at a time, the
            // nearest of those waiting by it first.
            const bool clear = door.alightMade >= door.alight
                    && now >= (door.alight > 0 ? door.lastAlight + AfterLastOff : door.openedAt + BoardAfter);
            if (!door.open || !clear || now < door.nextBoard || now < st.nextOn || st.sentOn >= mayGetOn)
                continue;
            int next = -1;
            float nearest = std::numeric_limits<float>::max();
            for (size_t i = 0; i < people.size(); ++i) {
                const Person &p = people[i];
                if (!p.used || p.stand != int(si) || p.door != doorIndex || p.state != Person::State::AtDoor)
                    continue;
                const float d = distance2D(p.pos, door.outside);
                if (d < nearest) {
                    nearest = d;
                    next = int(i);
                }
            }
            if (next < 0)
                continue;
            Person &p = people[size_t(next)];
            p.state = Person::State::ToDoor;
            moveTo(p, door.outside, door.outsideRef, 1, 1.2f);
            door.nextBoard = now + BoardGap;
            ++st.sentOn;
            st.nextOn = std::max(now, st.nextOn) + rate;
        }
        ++si;
    }
}

// Each group's people as they come: one by one where nobody sees them, at
// once on the first frame, after a jump in time and as a station comes near.
void StationPeople::Impl::makePeople() {
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        Group &g = groups[gi];
        if (g.done || g.place < 0 || !places[size_t(g.place)].active || g.made >= g.count)
            continue;
        // None come once their train's doors have opened.
        bool opened = false;
        for (const Stand &st : stands)
            opened = opened || (st.train == g.train && st.stop == g.stop && st.opened >= 0.0);
        if (opened && !firstFrame)
            continue;
        const double due = g.spread > 0.0 ? (now - g.appear) / g.spread : 1.0;
        const int wanted = std::min(g.count, int(std::floor(double(g.count) * std::clamp(due, 0.0, 1.0) + 0.999)));
        Place &place = places[size_t(g.place)];
        const bool atOnce = firstFrame || place.fresh || (g.made == 0 && g.people.empty() && due >= 1.0);
        int tries = atOnce ? g.count * 3 : 3;
        while (g.made < wanted && tries-- > 0) {
            const quint32 seed = mix(g.seed, quint32(g.made) * 2654435761u + quint32(tries));
            float spot[3];
            dtPolyRef ref = 0;
            if (!spotFor(place, g.side, seed, spot, &ref))
                continue;
            if (!atOnce && seen(place, spot))
                continue;
            const int index = newPerson(g.place, g.side, int(gi), spot, mix(g.seed, quint32(g.made) + 1000u), true);
            if (index < 0) {
                g.made = g.count;
                break;
            }
            ++g.made;
            g.people.push_back(index);
            Person &p = people[size_t(index)];
            // Facing the track, more or less.
            p.faceTrack = true;
            p.faceWay = trackWay(place, g.side, spot) + (unitRandom(p.seed, 53) - 0.5f) * 1.6f;
            p.way = p.faceWay;
            if (g.approached && unitRandom(p.seed, 60) < Approaching)
                approach(p, g);
        }
    }
    for (Place &place : places)
        place.fresh = false;
}

// The people moved on: the crowds stepped, the scripted walked, arrivals and
// those gone dealt with.
void StationPeople::Impl::stepPeople(float dt) {
    if (dt > 0.0f) {
        int steps = std::min(MostSteps, int(std::ceil(dt / LongestStep)));
        const float step = std::min(LongestStep, dt / float(std::max(steps, 1)));
        for (Place &place : places)
            if (place.active && place.crowd && place.people > 0)
                for (int k = 0; k < steps; ++k)
                    place.crowd->update(step, nullptr);
    }
    for (size_t i = 0; i < people.size(); ++i) {
        Person &p = people[i];
        if (!p.used)
            continue;
        Place &place = places[size_t(p.place)];
        if (!p.script.empty()) {
            // Walked along its points at its own pace.
            float left = p.walkSpeed * dt;
            p.speed = dt > 0.0f ? p.walkSpeed : p.speed;
            p.playing = false;
            float wantWay = p.way;
            bool walked = false;
            while (p.leg + 1 < p.script.size() / 3) {
                const float *a = &p.script[p.leg * 3];
                const float *b = &p.script[(p.leg + 1) * 3];
                if (int(p.leg) == p.clipLeg && p.clip >= 0) {
                    // The step clip: its place held, the clip carrying the
                    // body; the rise's difference made up as it goes. Begun
                    // once turned to the door (not snapped round to it).
                    if (p.clipStart < 0.0) {
                        const float want = std::atan2(b[0] - a[0], b[2] - a[2]);
                        if (std::fabs(std::remainder(want - p.way, 2.0f * float(M_PI))) > 0.3f) {
                            p.way = turned(p.way, want, dt);
                            p.speed = 0.0f;
                            walked = false;
                            break;
                        }
                        p.clipStart = now;
                    }
                    const double t = now - p.clipStart;
                    if (t >= double(p.clipLength)) {
                        std::copy(b, b + 3, p.pos);
                        ++p.leg;
                        p.clipStart = -1.0;
                        continue;
                    }
                    const float k = p.clipLength > 0.0f ? float(t) / p.clipLength : 1.0f;
                    p.pos[0] = a[0];
                    p.pos[1] = a[1] + p.clipRise * k;
                    p.pos[2] = a[2];
                    p.way = std::atan2(b[0] - a[0], b[2] - a[2]);
                    p.playing = true;
                    p.playTime = std::min(float(t), p.clipLength - 1e-3f);
                    break;
                }
                if (left <= 0.0f)
                    break;
                const float dx = b[0] - p.pos[0], dy = b[1] - p.pos[1], dz = b[2] - p.pos[2];
                const float d = std::hypot(dx, dz);
                if (d > 1e-4f) {
                    wantWay = std::atan2(dx, dz);
                    walked = true;
                }
                if (d <= left) {
                    std::copy(b, b + 3, p.pos);
                    left -= d;
                    ++p.leg;
                } else {
                    p.pos[0] += dx / d * left;
                    p.pos[1] += dy / d * left;
                    p.pos[2] += dz / d * left;
                    left = 0.0f;
                }
            }
            // Turned toward the way walked (a step clip turns it itself).
            if (walked && !p.playing)
                p.way = turned(p.way, wantWay, dt);
            if (p.leg + 1 >= p.script.size() / 3) {
                p.script.clear();
                p.leg = 0;
                if (p.state == Person::State::Entering) {
                    // In the car: to a seat (a car with seats, task humans
                    // 09), else gone.
                    if (!seatPerson(int(i)))
                        removePerson(int(i));
                    ++figures.boarded;
                    continue;
                }
                if (p.state == Person::State::Exiting) {
                    // On the platform: away along it.
                    if (addAgent(p, 1))
                        leave(p);
                    else
                        p.state = Person::State::Leaving;
                }
            }
            continue;
        }
        if (p.agent >= 0 && place.crowd) {
            const dtCrowdAgent *agent = place.crowd->getAgent(p.agent);
            std::copy(agent->npos, agent->npos + 3, p.pos);
            // On the floor, not on Recast's mesh above it.
            float floor = 0.0f;
            if (place.built && place.built->floorAt(p.pos[0], p.pos[1], p.pos[2], &floor)) {
                const float lift = p.pos[1] - floor;
                meshLiftSum += lift;
                meshLiftMax = std::max(meshLiftMax, lift);
                ++meshLifts;
                if (p.state == Person::State::ToDoor || p.state == Person::State::AtDoor) {
                    doorLiftSum += lift;
                    doorLiftMax = std::max(doorLiftMax, lift);
                    ++doorLifts;
                }
                p.pos[1] = floor;
            } else {
                ++floorMisses;
            }
            const float speed = std::hypot(agent->vel[0], agent->vel[2]);
            p.speed = speed;
            // Turned toward where it goes, its swerves smoothed out.
            const float k = std::min(1.0f, dt * 4.0f);
            p.going[0] += (agent->vel[0] - p.going[0]) * k;
            p.going[1] += (agent->vel[2] - p.going[1]) * k;
            if (speed > 0.15f && std::hypot(p.going[0], p.going[1]) > 0.1f)
                p.way = turned(p.way, std::atan2(p.going[0], p.going[1]), dt);
            else if (p.faceTrack && dt > 0.0f)
                p.way = turned(p.way, p.faceWay, dt, 2.0f);
            if (p.moving) {
                const float d = distance2D(p.pos, p.target);
                if (speed < 0.1f) {
                    if (p.slowSince < 0.0)
                        p.slowSince = now;
                } else {
                    p.slowSince = -1.0;
                }
                const bool stuck = p.slowSince >= 0.0 && now - p.slowSince > 2.0;
                // At a door: within reach of its front, the last steps walked
                // (not pushed about by those beside it).
                // Held up near it by those waiting beside it: straight on.
                const bool there = d < 0.4f || (p.state == Person::State::ToDoor && (d < 1.2f || (d < 4.5f && stuck)))
                        || (p.state != Person::State::ToDoor && stuck && d < 3.0f);
                if (there || (stuck && now - p.slowSince > 8.0)) {
                    p.moving = false;
                    place.crowd->resetMoveTarget(p.agent);
                    if (p.state == Person::State::ToDoor && p.stand >= 0 && size_t(p.stand) < stands.size()
                            && p.door >= 0 && size_t(p.door) < stands[size_t(p.stand)].doors.size()) {
                        const Door &door = stands[size_t(p.stand)].doors[size_t(p.door)];
                        dropAgent(p);
                        p.state = Person::State::Entering;
                        doorScript(p, door, true);
                        continue;
                    }
                    if (p.state == Person::State::Approach) {
                        p.state = Person::State::Waiting;
                        p.faceTrack = true;
                        p.faceWay = trackWay(place, p.side, p.pos) + (unitRandom(p.seed, 54) - 0.5f) * 0.8f;
                    } else if (p.state == Person::State::AtDoor && p.stand >= 0 && size_t(p.stand) < stands.size()
                               && p.door >= 0 && size_t(p.door) < stands[size_t(p.stand)].doors.size()) {
                        // Beside its door: facing it.
                        const Door &door = stands[size_t(p.stand)].doors[size_t(p.door)];
                        p.faceTrack = true;
                        p.faceWay = std::atan2(door.outside[0] - p.pos[0], door.outside[2] - p.pos[2]);
                    } else if (p.state == Person::State::Waiting) {
                        p.faceTrack = true;
                        p.faceWay = trackWay(place, p.side, p.pos) + (unitRandom(p.seed, quint32(now)) - 0.5f) * 1.6f;
                    }
                }
            } else if (p.state == Person::State::Waiting && now >= p.nextIdea) {
                // Now and then a few steps.
                p.nextIdea = now + 20.0 + 70.0 * double(unitRandom(p.seed, quint32(now)));
                if (unitRandom(p.seed, quint32(now) + 1u) < 0.3f) {
                    detourSeed = mix(p.seed, quint32(now));
                    const float extents[3] = {0.5f, 1.5f, 0.5f};
                    dtPolyRef start = 0, ref = 0;
                    float at[3], to[3];
                    const dtQueryFilter *filter = place.crowd->getFilter(0);
                    if (dtStatusSucceed(place.query->findNearestPoly(p.pos, extents, filter, &start, at)) && start != 0
                            && dtStatusSucceed(place.query->findRandomPointAroundCircle(start, at, 3.0f, filter, detourRandom,
                                                                                         &ref, to))) {
                        unsigned char area = 0;
                        place.built->mesh->getPolyArea(ref, &area);
                        if (area != WalkArea::AreaEdge)
                            moveTo(p, to, ref, 0);
                    }
                }
            }
        }
        // Those walking away: gone where nobody sees them.
        if (p.state == Person::State::Leaving && !seen(place, p.pos)) {
            removePerson(int(i));
            continue;
        }
    }
}

// People in cars (task humans 09) ------------------------------------------

// A train's car with seats (its consist's index): known, or made once its
// seats are read; -1 for a car without seats (or still being read).
int StationPeople::Impl::carFor(int train, int carIndex) {
    for (size_t k = 0; k < cars.size(); ++k)
        if (cars[k].train == train && cars[k].car == carIndex)
            return int(k);
    Consist *consist = Sim::SimulationController::instance()->consistOf(train);
    if (consist == nullptr || carIndex < 0 || carIndex >= consist->engItems.size())
        return -1;
    const Eng *eng = consist->engItems[carIndex].engPointer;
    if (eng == nullptr || !eng->definition)
        return -1;
    Car car;
    car.seats = carSeatsOf(*eng, &car.model);
    if (!car.seats || car.seats->empty())
        return -1;
    car.train = train;
    car.car = carIndex;
    car.eng = eng;
    car.taken.assign(size_t(car.seats->seats.size()), -1);
    cars.push_back(std::move(car));
    return int(cars.size()) - 1;
}

int StationPeople::Impl::newRider(int carIndex, quint32 seed, int crowdId) {
    if (kinds.empty())
        return -1;
    int index;
    if (!freeRiders.empty()) {
        index = freeRiders.back();
        freeRiders.pop_back();
    } else {
        index = int(riders.size());
        riders.emplace_back();
    }
    Rider &r = riders[size_t(index)];
    r = Rider();
    r.used = true;
    r.car = carIndex;
    r.seed = seed;
    if (crowdId < 0) {
        Crowd::PersonSpec spec;
        spec.kind = kindFor(seed);
        spec.behaviour = Crowd::Behaviour::Steered;
        spec.scale = 0.94f + 0.12f * unitRandom(seed, 51);
        spec.seed = seed;
        crowdId = crowd.addPerson(spec);
    }
    r.crowdId = crowdId;
    const float natural = crowd.walkSpeedOf(crowdId);
    r.walkSpeed = (natural > 0.0f ? natural : 1.1f) * 0.8f;
    r.phase = unitRandom(seed, 90) * 10.0f;
    ++cars[size_t(carIndex)].riders;
    return index;
}

void StationPeople::Impl::removeRider(int index, bool keepCrowdPerson) {
    Rider &r = riders[size_t(index)];
    if (!r.used)
        return;
    if (r.crowdId >= 0 && !keepCrowdPerson)
        crowd.removePerson(r.crowdId);
    if (r.car >= 0 && size_t(r.car) < cars.size()) {
        Car &car = cars[size_t(r.car)];
        --car.riders;
        if (r.seat >= 0 && size_t(r.seat) < car.taken.size() && car.taken[size_t(r.seat)] == index)
            car.taken[size_t(r.seat)] = -1;
    }
    r = Rider();
    freeRiders.push_back(index);
}

// The aisle along a car at a place along it (the nearest).
const ShapeSeats::Aisle &aisleAt(const ShapeSeats::Seats &seats, float z) {
    int best = 0;
    float bestGap = std::numeric_limits<float>::max();
    for (int k = 0; k < seats.aisles.size(); ++k) {
        const ShapeSeats::Aisle &a = seats.aisles[k];
        const float gap = z < a.z0 ? a.z0 - z : (z > a.z1 ? z - a.z1 : 0.0f);
        if (gap < bestGap) {
            bestGap = gap;
            best = k;
        }
    }
    return seats.aisles[best];
}

// A rider's way to a seat (taken for it): along the aisle to the seat's
// row, in front of it, then sitting down there. Its feet stay where it
// stands; the sitting clip carries its body back onto the cushion (its
// pelvis SitBack behind the seat's place), its place lowered (or raised)
// so the clip's seat meets the cushion's top.
bool StationPeople::Impl::sitPlan(Rider &r, const Car &car, int seat) {
    const ShapeSeats::Seat &s = car.seats->seats[seat];
    const int kind = crowd.kindOf(r.crowdId);
    const int sitDown = crowd.clipNamed(kind, QStringLiteral("sit_down"));
    const float way = s.heading * float(M_PI) / 180.0f;
    float travel[3];
    if (sitDown < 0 || !crowd.clipTravelOf(r.crowdId, sitDown, way, travel, Crowd::Travel::Hips))
        return false;
    const ShapeSeats::Aisle &aisle = aisleAt(*car.seats, s.position[2]);
    r.standAt[0] = s.position[0] - travel[0] - SitBack * std::sin(way);
    r.standAt[1] = aisle.y;
    r.standAt[2] = s.position[2] - travel[2] - SitBack * std::cos(way);
    r.sunk = s.position[1] - SeatPerDrop * std::max(0.0f, -travel[1]);
    r.seatWay = way;
    r.seat = seat;
    r.script.clear();
    r.leg = 0;
    const float points[3][3] = {{aisle.x, aisle.y, r.pos[2]}, {aisle.x, aisle.y, r.standAt[2]}, {r.standAt[0], aisle.y, r.standAt[2]}};
    r.script.insert(r.script.end(), r.pos, r.pos + 3);
    for (const auto &q : points)
        r.script.insert(r.script.end(), q, q + 3);
    r.state = Rider::State::ToSeat;
    r.clip = -1;
    r.clipStart = -1.0;
    const QString sitting = unitRandom(r.seed, 91) < 0.25f ? QStringLiteral("sitting_talking") : QStringLiteral("sitting");
    r.sittingClip = crowd.clipNamed(kind, sitting);
    if (r.sittingClip < 0)
        r.sittingClip = crowd.clipNamed(kind, QStringLiteral("sitting"));
    if (r.sittingClip >= 0)
        r.sittingLength = std::max(crowd.clipLength(kind, r.sittingClip), 0.1f);
    return r.sittingClip >= 0;
}

// One who has stepped in through its door: to a free seat near the door of
// its car (when its car has seats), a rider from now on; false otherwise.
bool StationPeople::Impl::seatPerson(int personIndex) {
    Person &p = people[size_t(personIndex)];
    if (p.stand < 0 || size_t(p.stand) >= stands.size() || p.crowdId < 0)
        return false;
    const Stand &st = stands[size_t(p.stand)];
    if (p.door < 0 || size_t(p.door) >= st.doors.size())
        return false;
    const Door &door = st.doors[size_t(p.door)];
    const int c = carFor(st.train, door.car);
    if (c < 0)
        return false;
    std::vector<std::pair<float, int>> free;
    {
        const Car &car = cars[size_t(c)];
        for (int k = 0; k < int(car.taken.size()); ++k)
            if (car.taken[size_t(k)] < 0)
                free.push_back({std::fabs(car.seats->seats[k].position[2] - door.localZ), k});
    }
    if (free.empty())
        return false;
    std::sort(free.begin(), free.end());
    const int pick = free[size_t(std::min<size_t>(free.size() - 1,
                                                  size_t(unitRandom(p.seed, 92) * float(std::min<size_t>(free.size(), SeatChoice)))))].second;
    const int crowdId = p.crowdId;
    const quint32 seed = p.seed;
    p.crowdId = -1;  // the crowd's person goes on as the rider
    removePerson(personIndex);
    const int ri = newRider(c, seed, crowdId);
    if (ri < 0) {
        crowd.removePerson(crowdId);
        return false;
    }
    Rider &r = riders[size_t(ri)];
    Car &car = cars[size_t(c)];
    const ShapeSeats::Aisle &aisle = aisleAt(*car.seats, door.localZ);
    r.pos[0] = aisle.x;
    r.pos[1] = aisle.y;
    r.pos[2] = door.localZ;
    r.way = 0.0f;
    if (!sitPlan(r, car, pick)) {
        removeRider(ri);
        return false;
    }
    car.taken[size_t(pick)] = ri;
    return true;
}

// Cars of trains coming to the stations near and standing there: their
// seats read, and filled with their train's people (those on board, or
// those to get off at its next stop if more) as they come into sight far
// off, or at once on the first frame.
void StationPeople::Impl::fillCars() {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    QSet<int> trains;
    for (const Group &g : groups)
        if (!g.done && g.place >= 0 && size_t(g.place) < places.size() && places[size_t(g.place)].active)
            trains.insert(g.train);
    for (const Stand &st : stands)
        trains.insert(st.train);
    for (const int train : trains) {
        Consist *consist = simulation->consistOf(train);
        if (consist == nullptr || train < 0 || size_t(train) >= simFrame->trains.size() || size_t(train) >= in->trains.size())
            continue;
        if (!in->trains[size_t(train)].passenger)
            continue;
        // Its cars with seats and their seats, once every car's are read
        // (its people shared out over all of them).
        std::vector<int> list;
        int seatsAll = 0;
        bool ready = true;
        for (int v = 0; v < consist->engItems.size(); ++v) {
            const Eng *eng = consist->engItems[v].engPointer;
            if (eng == nullptr || !eng->definition)
                continue;
            if (!carSeatsOf(*eng)) {
                ready = false;
                continue;
            }
            const int c = carFor(train, v);
            if (c >= 0) {
                list.push_back(c);
                seatsAll += int(cars[size_t(c)].taken.size());
            }
        }
        if (!ready || list.empty() || seatsAll <= 0)
            continue;
        const Sim::TrainState &state = simFrame->trains[size_t(train)].state;
        const Sim::TrainPlan &plan = in->trains[size_t(train)];
        int onBoard = std::max(0, state.passengers.boardedTotal - state.passengers.alightedTotal);
        if (state.nextStop >= 0 && size_t(state.nextStop) < plan.stops.size()
                && !(state.passengers.stop == state.nextStop && state.passengers.alighted > 0))
            onBoard = std::max(onBoard, plan.stops[size_t(state.nextStop)].alighting);
        for (const int c : list) {
            Car &car = cars[size_t(c)];
            if (car.filled || !car.drawn)
                continue;
            // Filled where its people appearing is not seen: within reach
            // but far off, off the screen, as it comes into view, or just
            // after a reset (a jump in time).
            const float centre[3] = {0.0f, 1.5f, 0.0f};
            float at[3], unused;
            localToCrowd(car, centre, 0.0f, at, &unused);
            const float camera[3] = {playerW[0], playerW[1], playerW[2]};
            bool onScreen = true;
            if (viewMatrix != nullptr) {
                const float *v = viewMatrix;
                const float cx = v[0] * at[0] + v[4] * at[1] + v[8] * at[2] + v[12];
                const float cy = v[1] * at[0] + v[5] * at[1] + v[9] * at[2] + v[13];
                const float cw = v[3] * at[0] + v[7] * at[1] + v[11] * at[2] + v[15];
                // A car's half length beyond the screen's edge still shows.
                const float margin = 1.0f + 12.0f / std::max(cw, 1.0f);
                onScreen = cw > -12.0f && std::fabs(cx) <= margin * std::max(cw, 1.0f) && std::fabs(cy) <= margin * std::max(cw, 1.0f);
            }
            const float distance = distance2D(at, camera);
            if (distance > RiderReach)
                continue;
            if (!firstFrame && wallNow > fillAnywhereUntil && wallNow - car.shownAt > JustShown && onScreen
                    && distance < FillFarther)
                continue;
            car.filled = true;
            const int want = std::min(int(car.taken.size()),
                                      int(std::lround(double(onBoard) * double(car.taken.size()) / double(seatsAll))));
            static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
            if (log) {
                const ShapeSeats::Seat &first = car.seats->seats.front();
                float seatAt[3], seatWay;
                localToCrowd(car, first.position, 0.0f, seatAt, &seatWay);
                qInfo().noquote() << QString("People: train %1 car %2 (%3): %4 of its %5 seats taken; its first seat at "
                                             "[%6, %7, %8] in the camera's tile")
                                     .arg(train).arg(car.car).arg(QFileInfo(car.model).fileName()).arg(want)
                                     .arg(car.taken.size()).arg(seatAt[0], 0, 'f', 1).arg(seatAt[1], 0, 'f', 1)
                                     .arg(seatAt[2], 0, 'f', 1);
            }
            for (int n = 0; n < want; ++n) {
                const quint32 seed = mix(mix(quint32(train) * 7919u, quint32(car.car) * 104729u), quint32(n) * 31u + 5u);
                int seat = int(unitRandom(seed, 93) * float(car.taken.size()));
                for (int tries = 0; tries < int(car.taken.size()) && car.taken[size_t(seat)] >= 0; ++tries)
                    seat = (seat + 7) % int(car.taken.size());
                if (car.taken[size_t(seat)] >= 0)
                    break;
                const int ri = newRider(c, seed, -1);
                if (ri < 0)
                    break;
                Rider &r = riders[size_t(ri)];
                Car &here = cars[size_t(c)];
                if (!sitPlan(r, here, seat)) {
                    removeRider(ri);
                    break;
                }
                here.taken[size_t(seat)] = ri;
                std::copy(r.standAt, r.standAt + 3, r.pos);
                r.script.clear();
                r.way = r.seatWay;
                r.state = Rider::State::Sitting;
                r.clip = r.sittingClip;
                r.clipStart = now - double(r.phase);
                r.clipLength = crowd.clipLength(crowd.kindOf(r.crowdId), r.clip);
            }
        }
    }
}

// A train come to a stand: those getting off at each door, as the
// simulation counts them, stand up (the nearest to it) and walk to it.
void StationPeople::Impl::sendOff(int standIndex) {
    Stand &st = stands[size_t(standIndex)];
    static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
    if (log) {
        QStringList list;
        for (const Door &door : st.doors)
            list << QString("car %1 (%2) %3 off").arg(door.car).arg(carFor(st.train, door.car)).arg(door.alight);
        qInfo().noquote() << QString("People: train %1 at stop %2, its cars' people sent off: %3")
                             .arg(st.train).arg(st.stop).arg(list.join(", "));
    }
    for (int k = 0; k < int(st.doors.size()); ++k) {
        Door &door = st.doors[size_t(k)];
        const int c = carFor(st.train, door.car);
        if (c < 0 || door.alight <= 0)
            continue;
        std::vector<std::pair<float, int>> sitting;
        for (int ri = 0; ri < int(riders.size()); ++ri) {
            const Rider &r = riders[size_t(ri)];
            if (r.used && r.car == c && r.stand < 0
                    && (r.state == Rider::State::Sitting || r.state == Rider::State::SittingDown))
                sitting.push_back({std::fabs(r.pos[2] - door.localZ), ri});
        }
        std::sort(sitting.begin(), sitting.end());
        for (int n = 0; n < door.alight && size_t(n) < sitting.size(); ++n) {
            Rider &r = riders[size_t(sitting[size_t(n)].second)];
            r.stand = standIndex;
            r.door = k;
            r.state = Rider::State::StandingUp;
            r.clip = crowd.clipNamed(crowd.kindOf(r.crowdId), QStringLiteral("stand_up"));
            r.clipStart = now + 0.6 * double(n) + double(unitRandom(r.seed, 94));
            r.clipLength = r.clip >= 0 ? crowd.clipLength(crowd.kindOf(r.crowdId), r.clip) : 0.0f;
            ++door.ridersOff;
        }
    }
}

// The rider waiting nearest a door to step out of it, -1 none yet.
int StationPeople::Impl::riderAtDoor(int standIndex, int doorIndex) {
    int best = -1;
    float nearest = std::numeric_limits<float>::max();
    const Door &door = stands[size_t(standIndex)].doors[size_t(doorIndex)];
    for (int ri = 0; ri < int(riders.size()); ++ri) {
        const Rider &r = riders[size_t(ri)];
        if (!r.used || r.stand != standIndex || r.door != doorIndex || r.state != Rider::State::AtDoor)
            continue;
        const float d = std::fabs(r.pos[2] - door.localZ);
        if (d < nearest) {
            nearest = d;
            best = ri;
        }
    }
    return nearest < QueueGap * 0.6f ? best : -1;
}

// Cars where drawn now (gone ones with their people), and their people:
// walking along their car, sitting down, sitting, standing up, to their
// door and waiting there.
void StationPeople::Impl::stepRiders(float dt) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    for (size_t c = 0; c < cars.size();) {
        Car &car = cars[c];
        Consist *consist = simulation->consistOf(car.train);
        Eng *eng = consist != nullptr && car.car >= 0 && car.car < consist->engItems.size()
                ? consist->engItems[car.car].engPointer : nullptr;
        qint64 when = 0;
        const bool wasDrawn = car.drawn;
        car.drawn = eng == car.eng && eng != nullptr && eng->lastDrawn(car.matrix, car.tile, &when)
                && wallNow - when <= DrawnLately;
        if (car.drawn)
            car.lastDrawn = when;
        if (car.drawn && !wasDrawn)
            car.shownAt = wallNow;
        if (car.drawn && car.filled) {
            // Far off: its people gone, made again as it comes near.
            const float centre[3] = {0.0f, 1.5f, 0.0f};
            float at[3], unused;
            localToCrowd(car, centre, 0.0f, at, &unused);
            if (distance2D(at, playerW) > RiderReach + RiderReachMargin) {
                for (int ri = 0; ri < int(riders.size()); ++ri)
                    if (riders[size_t(ri)].used && riders[size_t(ri)].car == int(c))
                        removeRider(ri);
                car.filled = false;
            }
        }
        else if (car.lastDrawn == 0)
            car.lastDrawn = wallNow;
        if (eng != car.eng || wallNow - car.lastDrawn > CarGone) {
            // Gone: its people with it; the cars after it move down one.
            for (int ri = 0; ri < int(riders.size()); ++ri)
                if (riders[size_t(ri)].used && riders[size_t(ri)].car == int(c))
                    removeRider(ri);
            for (Rider &r : riders)
                if (r.used && r.car > int(c))
                    --r.car;
            cars.erase(cars.begin() + long(c));
            continue;
        }
        ++c;
    }
    for (int ri = 0; ri < int(riders.size()); ++ri) {
        Rider &r = riders[size_t(ri)];
        if (!r.used)
            continue;
        r.speed = 0.0f;
        if (!r.script.empty()) {
            float left = r.walkSpeed * dt;
            while (left > 0.0f && r.leg + 1 < r.script.size() / 3) {
                const float *b = &r.script[(r.leg + 1) * 3];
                const float dx = b[0] - r.pos[0], dy = b[1] - r.pos[1], dz = b[2] - r.pos[2];
                const float d = std::hypot(dx, dz);
                if (d > 1e-4f) {
                    // Turned toward it, smoothly.
                    r.way = turned(r.way, std::atan2(dx, dz), dt, 8.0f);
                }
                if (d <= left) {
                    std::copy(b, b + 3, r.pos);
                    left -= d;
                    ++r.leg;
                } else {
                    r.pos[0] += dx / d * left;
                    r.pos[1] += dy / d * left;
                    r.pos[2] += dz / d * left;
                    left = 0.0f;
                }
            }
            r.speed = dt > 0.0f ? r.walkSpeed : 0.0f;
            if (r.leg + 1 >= r.script.size() / 3) {
                r.script.clear();
                r.leg = 0;
                r.speed = 0.0f;
                if (r.state == Rider::State::ToSeat) {
                    r.state = Rider::State::SittingDown;
                    r.clip = -1;
                } else if (r.state == Rider::State::ToDoor) {
                    r.state = Rider::State::AtDoor;
                }
            }
            continue;
        }
        const int kind = crowd.kindOf(r.crowdId);
        if (r.state == Rider::State::SittingDown) {
            if (r.clip < 0) {
                // Turned to face away from the seat, then down.
                const float delta = std::remainder(r.seatWay - r.way, 2.0f * float(M_PI));
                r.way += delta * std::min(1.0f, dt * 6.0f);
                if (std::fabs(delta) < 0.12f) {
                    r.way = r.seatWay;
                    r.clip = crowd.clipNamed(kind, QStringLiteral("sit_down"));
                    r.clipStart = now;
                    r.clipLength = r.clip >= 0 ? crowd.clipLength(kind, r.clip) : 0.0f;
                }
            } else if (now - r.clipStart >= double(r.clipLength)) {
                r.state = Rider::State::Sitting;
                r.clip = r.sittingClip;
                r.clipStart = now - double(r.phase);
                r.clipLength = crowd.clipLength(kind, r.clip);
            }
        } else if (r.state == Rider::State::StandingUp && now - r.clipStart >= double(r.clipLength) && r.clipStart <= now) {
            // Up: its seat free, along the aisle to its door, behind those
            // already there.
            Car &car = cars[size_t(r.car)];
            if (r.seat >= 0 && size_t(r.seat) < car.taken.size() && car.taken[size_t(r.seat)] == ri)
                car.taken[size_t(r.seat)] = -1;
            r.seat = -1;
            r.clip = -1;
            r.pos[1] = r.standAt[1];
            if (r.stand < 0 || size_t(r.stand) >= stands.size() || r.door < 0
                    || size_t(r.door) >= stands[size_t(r.stand)].doors.size()) {
                r.state = Rider::State::AtDoor;
                continue;
            }
            const Door &door = stands[size_t(r.stand)].doors[size_t(r.door)];
            int ahead = 0;
            for (const Rider &o : riders)
                ahead += o.used && &o != &r && o.stand == r.stand && o.door == r.door
                        && (o.state == Rider::State::ToDoor || o.state == Rider::State::AtDoor) ? 1 : 0;
            const ShapeSeats::Aisle &aisle = aisleAt(*car.seats, door.localZ);
            const float from = r.pos[2] >= door.localZ ? 1.0f : -1.0f;
            const float spot[3] = {aisle.x, aisle.y, door.localZ + from * QueueGap * float(ahead)};
            const float row[3] = {aisle.x, aisle.y, r.pos[2]};
            r.script.assign(r.pos, r.pos + 3);
            r.script.insert(r.script.end(), row, row + 3);
            r.script.insert(r.script.end(), spot, spot + 3);
            r.leg = 0;
            r.state = Rider::State::ToDoor;
        }
    }
    figures.riders = 0;
    figures.sitting = 0;
    for (const Rider &r : riders) {
        figures.riders += r.used ? 1 : 0;
        figures.sitting += r.used && r.state == Rider::State::Sitting ? 1 : 0;
    }
}

// A place and way in a car's space, in the crowd's (the camera's tile).
void StationPeople::Impl::localToCrowd(const Car &car, const float *local, float way, float *at, float *worldWay) const {
    float w[3];
    transformPoint(car.matrix, local, w);
    at[0] = w[0] + float(double(car.tile[0] - int(playerT[0])) * 2048.0);
    at[1] = w[1];
    at[2] = w[2] + float(double(car.tile[1] - int(playerT[1])) * 2048.0);
    const float dx = car.matrix[0] * std::sin(way) + car.matrix[8] * std::cos(way);
    const float dz = car.matrix[2] * std::sin(way) + car.matrix[10] * std::cos(way);
    *worldWay = std::atan2(dx, dz);
}

void StationPeople::Impl::steerRiders() {
    for (const Rider &r : riders) {
        if (!r.used || r.crowdId < 0 || r.car < 0 || size_t(r.car) >= cars.size())
            continue;
        const Car &car = cars[size_t(r.car)];
        if (!car.drawn)
            continue;
        float local[3] = {r.pos[0], r.pos[1], r.pos[2]};
        float at[3], way;
        if (r.clip >= 0 && now >= r.clipStart) {
            double t = now - r.clipStart;
            const float length = std::max(r.clipLength, 1e-3f);
            float k = 1.0f;
            if (r.state == Rider::State::Sitting) {
                t = std::fmod(t, double(length));
            } else {
                t = std::min(t, double(length) - 1e-3);
                k = float(t) / length;
            }
            // Its place lowered onto the seat over sitting down, raised
            // again over standing up.
            if (r.state == Rider::State::SittingDown)
                local[1] = r.standAt[1] + (r.sunk - r.standAt[1]) * k;
            else if (r.state == Rider::State::Sitting)
                local[1] = r.sunk;
            else if (r.state == Rider::State::StandingUp)
                local[1] = r.sunk + (r.standAt[1] - r.sunk) * k;
            localToCrowd(car, local, r.state == Rider::State::StandingUp || r.state == Rider::State::Sitting
                                                 || r.state == Rider::State::SittingDown ? r.seatWay : r.way,
                         at, &way);
            crowd.steerClip(r.crowdId, at, way, r.clip, float(t));
            continue;
        }
        if (r.state == Rider::State::StandingUp || r.state == Rider::State::Sitting)
            local[1] = r.sunk;  // waiting to stand up: still sitting
        localToCrowd(car, local, r.state == Rider::State::StandingUp ? r.seatWay : r.way, at, &way);
        if (r.state == Rider::State::StandingUp && r.sittingClip >= 0)
            crowd.steerClip(r.crowdId, at, way, r.sittingClip, std::fmod(float(now) + r.phase, r.sittingLength));
        else
            crowd.steer(r.crowdId, at, way, r.speed);
    }
}

// The crowd: everybody where they are now, in the camera's tile.
void StationPeople::Impl::steerCrowd() {
    for (const Person &p : people) {
        if (!p.used || p.crowdId < 0)
            continue;
        const Place &place = places[size_t(p.place)];
        const float at[3] = {float(place.origin[0] - double(playerT[0]) * 2048.0) + p.pos[0],
                             float(place.origin[1]) + p.pos[1],
                             float(place.origin[2] - double(playerT[1]) * 2048.0) + p.pos[2]};
        if (p.playing)
            crowd.steerClip(p.crowdId, at, p.way, p.clip, p.playTime);
        else
            crowd.steer(p.crowdId, at, p.way, p.speed);
    }
}

void StationPeople::frame(RenderQueue &queue, const float *playerT, const float *playerW, Activity *activity) {
    Impl &m = *d;
    QElapsedTimer timer;
    timer.start();
    if (m.mode < 0)
        m.readSettings();
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if (m.mode == HumanSet::Never || activity == nullptr || simulation == nullptr || !simulation->shows(activity)) {
        if (!m.people.empty() || !m.groups.empty())
            m.reset();
        return;
    }
    m.in = simulation->input();
    m.simFrame = simulation->viewFrame();
    if (!m.in || !m.simFrame)
        return;
    m.activity = activity;
    if (m.inputFor != m.in.get() || m.runFor != m.simFrame->runId) {
        m.reset();
        m.inputFor = m.in.get();
        m.runFor = m.simFrame->runId;
    }
    m.loadHumans();
    if (!m.humansLoaded)
        return;
    std::copy(playerT, playerT + 2, m.playerT);
    std::copy(playerW, playerW + 3, m.playerW);
    m.viewMatrix = GLUU::get() != nullptr ? GLUU::get()->fMatrix : nullptr;
    m.wallNow = WorldObj::clock();

    // The stations near the camera built for the people too.
    if (!Game::viewWalkAreas)
        StationWalkAreas::instance().frame(playerT, playerW);
    m.mapSides();

    m.now = m.simFrame->time;
    float dt = 0.0f;
    // People in cars walk by the time gone (up to 2 s a frame): no crowd to
    // keep steady there.
    float riderDt = 0.0f;
    if (m.lastTime >= 0.0) {
        const double step = m.now - m.lastTime;
        if (step < -1.0 || step > 60.0) {
            // A jump in time: everybody made again for the new time.
            m.reset();
            m.inputFor = m.in.get();
            m.runFor = m.simFrame->runId;
        } else {
            dt = float(std::clamp(step, 0.0, 0.5));
            riderDt = float(std::clamp(step, 0.0, 2.0));
        }
    }
    m.lastTime = m.now;

    m.updatePlaces();
    m.updateGroups();
    m.adoptCarried();
    m.updateStands();
    m.makePeople();
    m.stepPeople(dt);
    m.stepRiders(riderDt);
    m.fillCars();
    m.firstFrame = false;
    m.steerCrowd();
    m.steerRiders();

    Crowd::View view;
    std::copy(playerW, playerW + 3, view.camera);
    view.fieldOfView = Game::cameraFov;
    m.crowd.update(dt, view);
    m.crowd.push(queue);

    // The figures (TSRE_PEOPLE_LOG=1: in the log as they change, once a
    // second at most).
    Stats &f = m.figures;
    f.stations = 0;
    for (const Impl::Place &place : m.places)
        f.stations += place.active ? 1 : 0;
    f.groups = 0;
    for (const Impl::Group &g : m.groups)
        f.groups += g.done ? 0 : 1;
    f.people = f.waiting = f.walking = f.boarding = f.alighting = 0;
    for (const Impl::Person &p : m.people) {
        if (!p.used)
            continue;
        ++f.people;
        if (p.state == Impl::Person::State::ToDoor || p.state == Impl::Person::State::Entering)
            ++f.boarding;
        else if (p.state == Impl::Person::State::Exiting)
            ++f.alighting;
        else if (p.speed > 0.15f)
            ++f.walking;
        else
            ++f.waiting;
    }
    f.updateMs = double(timer.nsecsElapsed()) / 1e6;
    static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
    static QString lastLogged;
    const QString counts = QString("%1 %2 %3 %4 %5 %6 %7 %8").arg(f.groups).arg(f.people).arg(f.boarding).arg(f.alighting)
                                   .arg(f.boarded).arg(f.alighted).arg(f.riders).arg(f.sitting);
    if (log && counts != lastLogged && (!m.logClock.isValid() || m.logClock.elapsed() > 1000)) {
        lastLogged = counts;
        m.logClock.start();
        // (debugging) those walking to doors.
        QStringList doorWalkers;
        for (const Impl::Person &p : m.people) {
            if (!p.used || p.state != Impl::Person::State::ToDoor || p.stand < 0 || size_t(p.stand) >= m.stands.size())
                continue;
            const Impl::Door &door = m.stands[size_t(p.stand)].doors[size_t(p.door)];
            const dtCrowdAgent *agent = p.agent >= 0 ? m.places[size_t(p.place)].crowd->getAgent(p.agent) : nullptr;
            doorWalkers << QString("d %1 v %2 ts %3 st %4 dy %5").arg(distance2D(p.pos, door.outside), 0, 'f', 1)
                           .arg(p.speed, 0, 'f', 2).arg(agent ? int(agent->targetState) : -1).arg(agent ? int(agent->state) : -1)
                           .arg(door.outside[1] - p.pos[1], 0, 'f', 2);
        }
        if (!doorWalkers.isEmpty())
            qInfo().noquote() << "People to doors:" << doorWalkers.join("; ");
        // The crowd's tiers (core.rendering.humanQuality) for measuring on
        // the user's hardware (roadmap, "Measuring on the user's hardware").
        const Crowd::Stats &crowd = m.crowd.stats();
        qInfo().noquote() << QString("People at %1: %2 stations, %3 groups, %4 people (%5 waiting, %6 walking, %7 "
                                     "boarding, %8 getting off), %9 boarded, %10 got off; %11 ms, crowd %12 + %13 ms, %14 unposed; "
                                     "tiers %15 near, %16 middle, %17 far, %18 impostors drawn, %19 baked in %20 ms; "
                                     "in cars %21 (%22 sitting, %23 cars with seats); on the floor: the mesh %24 cm above "
                                     "on average, %25 at most, %26 without a floor found; walking to doors %27 cm, %28 at most")
                             .arg(m.now, 0, 'f', 0).arg(f.stations).arg(f.groups).arg(f.people).arg(f.waiting)
                             .arg(f.walking).arg(f.boarding).arg(f.alighting).arg(f.boarded).arg(f.alighted)
                             .arg(f.updateMs, 0, 'f', 2).arg(crowd.updateMs, 0, 'f', 2).arg(crowd.pushMs, 0, 'f', 2)
                             .arg(crowd.unposed).arg(crowd.nearTier).arg(crowd.middleTier).arg(crowd.farTier)
                             .arg(crowd.impostorsDrawn).arg(crowd.impostorsBaked).arg(crowd.bakeMs, 0, 'f', 1)
                             .arg(f.riders).arg(f.sitting).arg(m.cars.size())
                             .arg(m.meshLifts > 0 ? 100.0 * m.meshLiftSum / m.meshLifts : 0.0, 0, 'f', 1)
                             .arg(100.0 * m.meshLiftMax, 0, 'f', 1).arg(m.floorMisses)
                             .arg(m.doorLifts > 0 ? 100.0 * m.doorLiftSum / m.doorLifts : 0.0, 0, 'f', 1)
                             .arg(100.0 * m.doorLiftMax, 0, 'f', 1);
        m.meshLiftSum = m.meshLiftMax = m.doorLiftSum = m.doorLiftMax = 0.0f;
        m.meshLifts = m.floorMisses = m.doorLifts = 0;
    }
}

}
