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
#include <tsre/ogl/GLUU.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/GltfShape.h>
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
// The most people at a station (each a crowd agent).
constexpr int MostAtStation = 250;
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
// A car's place taken as current while drawn within the last this many ms.
constexpr qint64 DrawnLately = 1500;
// The longest step of the people's crowds (s), the most steps a frame.
constexpr float LongestStep = 0.1f;
constexpr int MostSteps = 5;

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

void transformPoint(const float *m, const float *p, float *out) {
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
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
        // Per walk platform (side): its seeds on floor or ground.
        std::vector<std::vector<int>> seeds;
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
        bool open = false;
        bool wasOpen = false;
        int alight = 0;
        int alightMade = 0;
        double nextAlight = 0.0;
        double lastAlight = -1.0;
        double openedAt = -1.0;
        double nextBoard = 0.0;
        int assigned = 0;
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
    QHash<int, QPair<int, int>> sideOfItem;
    std::vector<const void *> sidesFor;

    const void *inputFor = nullptr;
    unsigned int runFor = 0;
    double lastTime = -1.0;
    bool firstFrame = true;
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
    int newPerson(int place, int side, int group, const float *pos, quint32 seed, bool agent);
    void removePerson(int index);
    bool addAgent(Person &p, int filter);
    void dropAgent(Person &p);
    void moveTo(Person &p, const float *target, dtPolyRef ref, int filter, float hurry = 1.0f);
    void leave(Person &p);
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
    humans = HumanSet::forRoute(route, mode, HumanSet::tsreHumansFolder());
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
                         .arg(humans.source == HumanSet::Source::Route ? "the route's" : "TSRE's");
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
    const WalkArea::Seed &s = place.built->seeds[size_t(seeds[std::min(size_t(unitRandom(seed, 21) * float(seeds.size())), seeds.size() - 1)])];
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

int StationPeople::Impl::newPerson(int placeIndex, int side, int group, const float *pos, quint32 seed, bool agent) {
    Place &place = places[size_t(placeIndex)];
    if (place.people >= MostAtStation || kinds.empty())
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
    p.crowdId = crowd.addPerson(spec);
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
    params.maxAcceleration = 3.0f;
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

bool StationPeople::Impl::standing(int train, int stop) const {
    if (!simFrame || size_t(train) >= simFrame->trains.size())
        return false;
    const Sim::TrainState &s = simFrame->trains[size_t(train)].state;
    return s.status == Sim::TrainStatus::Stopped && s.stoppedAt == Sim::StopKind::Station && s.nextStop - 1 == stop;
}

bool StationPeople::Impl::trainPast(int train, int stop) const {
    if (!simFrame || size_t(train) >= simFrame->trains.size())
        return true;
    const Sim::TrainState &s = simFrame->trains[size_t(train)].state;
    if (s.status == Sim::TrainStatus::Finished)
        return true;
    if (s.nextStop > stop + 1)
        return true;
    return s.nextStop == stop + 1 && !(s.status == Sim::TrainStatus::Stopped && s.stoppedAt == Sim::StopKind::Station);
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
            // None board a train ending here.
            const bool last = s + 1 == int(plan.stops.size());
            g.count = last ? 0 : Sim::PassengersWaiting(activity, Game::trackDB, stop.platformItem);
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
            const std::vector<int> list = g.people;
            for (int i : list)
                leave(people[size_t(i)]);
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
    // Its first stop where it starts (its front at the platform): none get
    // off; its last: more.
    const Sim::TrainPlan &plan = in->trains[size_t(stand.train)];
    const Sim::StopPlan &here = plan.stops[size_t(stand.stop)];
    const bool first = stand.stop == 0 && here.part == 0
            && plan.startFront >= std::min(here.platformStart, here.platformEnd) - 5.0
            && plan.startFront <= std::max(here.platformStart, here.platformEnd) + 5.0;
    const bool last = stand.stop + 1 == int(in->trains[size_t(stand.train)].stops.size());
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
        std::shared_ptr<const CarDoors> parts = carDoors(eng->definition->shapePath, eng->path);
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
            }
            if (!found || !onSide(place, stand.side, door.outside))
                continue;
            // The train's floor at the threshold (the user); a door near a
            // carriage's end without parts: level with the platform.
            if (std::isnan(cd.bottom)) {
                threshold[1] = door.outside[1];
                inside[1] = door.outside[1];
            }
            door.car = v;
            door.group = cd.group;
            std::copy(inside, inside + 3, door.inside);
            std::copy(threshold, threshold + 3, door.threshold);
            const quint32 seed = mix(mix(quint32(stand.train), quint32(stand.stop)), quint32(v * 31 + int(doors.size())));
            door.alight = first ? 0 : last ? 2 + int(unitRandom(seed, 70) * 4.0f) : int(unitRandom(seed, 70) * 2.6f);
            doors.push_back(door);
        }
    }
    stand.doors = std::move(doors);
    stand.doorsKnown = true;
    static const bool log = qEnvironmentVariable("TSRE_PEOPLE_LOG") == "1";
    if (log) {
        QStringList where;
        for (const Door &door : stand.doors) {
            const double gx = place.origin[0] + door.outside[0], gz = place.origin[2] + door.outside[2];
            const int tx = int(std::floor((gx + 1024.0) / 2048.0)), tz = int(std::floor((gz + 1024.0) / 2048.0));
            where << QString("car %1%2 tile [%3, %4] pos [%5, %6, %7] floor %8")
                     .arg(door.car).arg(door.group < 0 ? " (end)" : "").arg(tx).arg(tz)
                     .arg(gx - tx * 2048.0, 0, 'f', 1).arg(place.origin[1] + door.outside[1], 0, 'f', 1)
                     .arg(gz - tz * 2048.0, 0, 'f', 1).arg(door.threshold[1] - door.outside[1], 0, 'f', 2);
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
                    removePerson(int(i));
                    ++figures.boarded;
                } else if (p.state == Person::State::ToDoor || p.state == Person::State::AtDoor) {
                    leave(p);
                } else {
                    p.stand = -1;
                    p.door = -1;
                }
            }
            // The stands after it move down one.
            for (Person &p : people)
                if (p.used && p.stand > int(si))
                    --p.stand;
            stands.erase(stands.begin() + long(si));
            continue;
        }
        if (!st.doorsKnown)
            readDoors(st);
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
                const quint32 seed = mix(mix(quint32(st.train) * 131u, quint32(st.stop)),
                                         quint32(door.car * 977 + door.alightMade * 13 + doorIndex));
                const int index = newPerson(st.place, st.side, -1, door.inside, seed, false);
                ++door.alightMade;
                door.lastAlight = now;
                door.nextAlight += AlightGap * (0.8 + 0.4 * double(unitRandom(seed, 80)));
                if (index < 0)
                    break;
                Person &p = people[size_t(index)];
                p.state = Person::State::Exiting;
                p.stand = int(si);
                p.door = doorIndex;
                p.script = {door.inside[0], door.inside[1], door.inside[2], door.threshold[0], door.threshold[1],
                            door.threshold[2], door.outside[0], door.outside[1], door.outside[2]};
                p.leg = 0;
                ++figures.alighted;
            }
            // Getting on once the last off is clear: one at a time, the
            // nearest of those waiting by it first.
            const bool clear = door.alightMade >= door.alight
                    && now >= (door.alight > 0 ? door.lastAlight + AfterLastOff : door.openedAt + BoardAfter);
            if (!door.open || !clear || now < door.nextBoard)
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
            while (left > 0.0f && p.leg + 1 < p.script.size() / 3) {
                const float *b = &p.script[(p.leg + 1) * 3];
                const float dx = b[0] - p.pos[0], dy = b[1] - p.pos[1], dz = b[2] - p.pos[2];
                const float d = std::hypot(dx, dz);
                if (d > 1e-4f)
                    p.way = std::atan2(dx, dz);
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
            if (p.leg + 1 >= p.script.size() / 3) {
                p.script.clear();
                p.leg = 0;
                if (p.state == Person::State::Entering) {
                    // In the car: gone.
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
            const float speed = std::hypot(agent->vel[0], agent->vel[2]);
            p.speed = speed;
            if (speed > 0.15f) {
                // Turned toward where it goes, smoothly.
                const float want = std::atan2(agent->vel[0], agent->vel[2]);
                float delta = std::remainder(want - p.way, 2.0f * float(M_PI));
                p.way += delta * std::min(1.0f, dt * 6.0f);
            } else if (p.faceTrack && dt > 0.0f) {
                float delta = std::remainder(p.faceWay - p.way, 2.0f * float(M_PI));
                p.way += delta * std::min(1.0f, dt * 2.0f);
            }
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
                        p.script = {p.pos[0], p.pos[1], p.pos[2], door.outside[0], door.outside[1], door.outside[2],
                                    door.threshold[0], door.threshold[1], door.threshold[2], door.inside[0],
                                    door.inside[1], door.inside[2]};
                        p.leg = 0;
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

// The crowd: everybody where they are now, in the camera's tile.
void StationPeople::Impl::steerCrowd() {
    for (const Person &p : people) {
        if (!p.used || p.crowdId < 0)
            continue;
        const Place &place = places[size_t(p.place)];
        const float at[3] = {float(place.origin[0] - double(playerT[0]) * 2048.0) + p.pos[0],
                             float(place.origin[1]) + p.pos[1],
                             float(place.origin[2] - double(playerT[1]) * 2048.0) + p.pos[2]};
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
    if (m.lastTime >= 0.0) {
        const double step = m.now - m.lastTime;
        if (step < -1.0 || step > 60.0) {
            // A jump in time: everybody made again for the new time.
            m.reset();
            m.inputFor = m.in.get();
            m.runFor = m.simFrame->runId;
        } else {
            dt = float(std::clamp(step, 0.0, 0.5));
        }
    }
    m.lastTime = m.now;

    m.updatePlaces();
    m.updateGroups();
    m.updateStands();
    m.makePeople();
    m.stepPeople(dt);
    m.firstFrame = false;
    m.steerCrowd();

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
    const QString counts = QString("%1 %2 %3 %4 %5 %6").arg(f.groups).arg(f.people).arg(f.boarding).arg(f.alighting)
                                   .arg(f.boarded).arg(f.alighted);
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
        qInfo().noquote() << QString("People at %1: %2 stations, %3 groups, %4 people (%5 waiting, %6 walking, %7 "
                                     "boarding, %8 getting off), %9 boarded, %10 got off; %11 ms, crowd %12 + %13 ms")
                             .arg(m.now, 0, 'f', 0).arg(f.stations).arg(f.groups).arg(f.people).arg(f.waiting)
                             .arg(f.walking).arg(f.boarding).arg(f.alighting).arg(f.boarded).arg(f.alighted)
                             .arg(f.updateMs, 0, 'f', 2).arg(m.crowd.stats().updateMs, 0, 'f', 2)
                             .arg(m.crowd.stats().pushMs, 0, 'f', 2);
    }
}

}
