#include <tsre/tests/TrackCantTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/ProceduralTrackPolicy.h>
#include <tsre/trains/CarPlacement.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/tdb/Ruch.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackCant.h>
#include <tsre/tdb/TrackShape.h>

namespace {

class Database : public TDB {
public:
    using TDB::TDB;
    using TDB::addItemToTrNode;
};

constexpr float Kmh = 1.0f / 3.6f;

// Straights of 10 m (10) and curves of 5 degrees on a 500 m radius, to the
// right (20) and to the left (21).
struct Network {
    TSectionDAT definitions{false, false};
    TSection straight{10, 0, 10.0f, 0.0f};
    TSection right{20, 1, float(5.0 * M_PI / 180.0), 500.0f};
    TSection left{21, 1, float(-5.0 * M_PI / 180.0), 500.0f};
    Database db;
    int nextItem = 0;

    Network() : db(&definitions, false) {
        definitions.sekcja[10] = &straight;
        definitions.sekcja[20] = &right;
        definitions.sekcja[21] = &left;
    }

    // A vector node of these sections in this order; its id. `pitch` the
    // TDB's AX of its first section (positive falls along the node).
    int path(const std::vector<int> &sections, int uid, float x = 0, float z = 0, float pitch = 0) {
        float position[3] = {x, 0, z};
        float frame[3] = {pitch, 0, 0};
        int ends[2] = {0, 1};
        int last = db.newTrack(0, 0, position, frame, ends, 20, sections[0], uid);
        const int vector = db.trackNodes[last]->pins[0].link;
        for (size_t i = 1; i < sections.size(); ++i)
            last = db.appendTrack(last, ends, 20, sections[i], uid + int(i));
        return vector;
    }

    bool inOrder(int vector, const std::vector<int> &sections) {
        const TRnode *node = db.trackNodes[vector];
        if (node == nullptr || node->iTrv != int(sections.size()))
            return false;
        for (int i = 0; i < node->iTrv; ++i)
            if (int(node->trVectorSection[i].sectionIndex) != sections[size_t(i)])
                return false;
        return true;
    }

    // A speed post on `vector` at `distance`: a limit (SIGN), warning or
    // resume, km/h, facing trains moving the node's way or against it.
    int post(int vector, float distance, TRitem::SType type, float kmh, bool forward) {
        const int id = nextItem++;
        TRitem *item = TRitem::newSpeedPostItem(id, distance, type);
        if (type != TRitem::RESUME)
            item->setSpeedpostSpeed(kmh);
        float pose[7] = {0, 0, 0, 0, 0, 0, 0};
        db.getDrawPositionOnTrNode(pose, vector, distance);
        item->setSpeedpostRot(pose[3] + float(forward ? M_PI / 2.0 : -M_PI / 2.0));
        db.trackItems[id] = item;
        db.iTRitems = std::max(db.iTRitems, id + 1);
        db.addItemToTrNode(vector, id);
        return id;
    }

    // A junction at `a`'s far end: `a` at its input, `b` (by its start) at
    // its first output.
    void join(int a, int b) {
        const int junction = db.trackNodes[a]->pins[1].link;
        TRnode *j = db.trackNodes[junction];
        j->typ = 2;
        j->inputPinCount = 1;
        j->outputPinCount = 2;
        j->pins[0].link = a;
        j->pins[1].link = b;
        j->pins[2].link = 0;
        const int start = db.trackNodes[b]->pins[0].link;
        delete db.trackNodes[start];
        db.trackNodes[start] = nullptr;
        db.trackNodes[b]->pins[0].link = junction;
    }
};

bool near(float a, float b, float tolerance = 1e-5f) {
    return std::fabs(a - b) <= tolerance;
}

// The cant along a section never changes faster than the slope, its ends
// meet its neighbours', and its angle is asin(cant / gauge) signed by the
// curve's direction.
bool tablesValid(const std::vector<TrackCant::Section> &s, const std::vector<int> &run,
                 const std::vector<float> &lengths, float slope, float gauge, int direction) {
    for (size_t k = 0; k < run.size(); ++k) {
        const TrackCant::Section &a = s[size_t(run[k])];
        if (a.points < 2 || a.position[0] != 0.0f || a.position[a.points - 1] != 1.0f)
            return false;
        for (int p = 0; p < a.points; ++p) {
            if (!near(a.visual[p], float(std::asin(a.physical[p] / gauge)) * float(-direction), 1e-6f))
                return false;
            if (p > 0) {
                const float along = (a.position[p] - a.position[p - 1]) * lengths[k];
                if (std::fabs(a.physical[p] - a.physical[p - 1]) > slope * along + 1e-5f)
                    return false;
            }
        }
        if (k + 1 < run.size()) {
            const TrackCant::Section &b = s[size_t(run[k + 1])];
            if (!near(a.physical[a.points - 1], b.physical[0]))
                return false;
        }
    }
    return near(s[size_t(run.front())].physical[0], 0.0f)
        && near(s[size_t(run.back())].physical[s[size_t(run.back())].points - 1], 0.0f);
}

}

int TsreTests::runTrackCantSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok)
            ++passed;
        else {
            ++failed;
            qWarning() << "[tests:track-cant] FAIL" << name;
        }
        if (verbose)
            qInfo() << "[tests:track-cant]" << (ok ? "ok" : "FAIL") << name;
    };
    using TrackCant::Route;
    using TrackCant::Standard;

    // Standards as the .trk writes them.
    {
        const Route psk = Route::fromBlocks(
                {QStringLiteral("ORTSForceSuperElevation ( 1 )"),
                 QStringLiteral("ORTSSuperElevation(\n        MaximumCant ( 0mm )\n        MinimumSpeed ( 0km/h )\n"
                                "        MaximumSpeed ( 59km/h )\n    )"),
                 QStringLiteral("ORTSSuperElevation(\n        MaximumCant ( 50mm )\n        MinimumSpeed ( 60km/h )\n"
                                "        MaximumSpeed ( 100km/h )\n    )"),
                 QStringLiteral("ORTSSuperElevation(\n        MaximumCant ( 100mm )\n        MinimumSpeed ( 101km/h )\n"
                                "        MaximumSpeed ( 200km/h )\n    )")},
                0.0f, 55.554f, true);
        check(psk.forced == 1 && psk.standards.size() == 3 && near(psk.gauge, 1.435f), "OR_PSK_NEW's standards read");
        check(near(psk.standards[0].maxCant, 0.0f) && near(psk.standards[0].minSpeed, 0.0f)
                      && near(psk.standards[0].maxSpeed, 59.0f * Kmh, 1e-4f)
                      && near(psk.standards[1].maxCant, 0.05f) && near(psk.standards[2].minSpeed, 101.0f * Kmh, 1e-4f),
              "standards' cant and speeds with their units");
        check(psk.standards[0].legacy && !psk.standards[0].isDefault
                      && near(psk.standards[0].maxFreightUnderbalance, 0.05f)
                      && near(psk.standards[0].maxPassengerUnderbalance, 0.075f),
              "a standard without underbalance takes Open Rails' 5 and 7.5 cm");

        Standard up;
        check(Standard::parse(QStringLiteral(
                      "ORTSSuperElevation ( MaxFreightUnderbalance ( 1in ) MaxPassengerUnderbalance ( 3in ) "
                      "MinimumCant ( 0.25in ) MaximumCant ( 5in ) Precision ( 0.25in ) MaxRunOffSlope ( 0.0019 ) "
                      "MaxRunOffSpeed ( 1.25in/s ) MinimumSpeed ( 15mph ) )"), up)
                      && near(up.maxFreightUnderbalance, 0.0254f) && near(up.maxPassengerUnderbalance, 0.0762f)
                      && near(up.minCant, 0.00635f) && near(up.maxCant, 0.127f) && near(up.precision, 0.00635f)
                      && near(up.runoffSlope, 0.0019f) && near(up.runoffSpeed, 0.03175f)
                      && near(up.minSpeed, 6.7056f, 1e-4f) && !up.legacy,
              "the manual's Union Pacific standard in inches and mph");

        const Route metric = Route::fromBlocks({}, 0.0f, 44.444443f, true);
        const Route fast = Route::fromBlocks({}, 1.067f, 50.0f, true);
        const Route imperial = Route::fromBlocks({}, 0.0f, 30.0f, false);
        check(metric.standards.size() == 1 && metric.standards[0].isDefault
                      && near(metric.standards[0].maxFreightUnderbalance, 0.100f)
                      && near(metric.standards[0].maxPassengerUnderbalance, 0.130f) && metric.forced == -1,
              "default metric standard");
        check(near(fast.gauge, 1.067f) && near(fast.standards[0].runoffSpeed, 0.080f)
                      && near(fast.standards[0].maxPassengerUnderbalance, 0.150f),
              "high speed default and TrackGauge");
        check(near(imperial.standards[0].minCant, 0.0127f) && near(imperial.standards[0].maxCant, 0.1524f)
                      && near(imperial.standards[0].maxFreightUnderbalance, 0.0508f)
                      && near(imperial.standards[0].minSpeed, 15.5f * 0.44704f, 1e-4f),
              "imperial default");
        const Route table = Route::fromBlocks({QStringLiteral("ORTSTrackSuperElevation ( 100 0.15 1000 0.05 )")},
                                              0.0f, 30.0f, true);
        check(table.standards.size() == 1 && table.standards[0].legacy && near(table.standards[0].minCant, 0.05f)
                      && near(table.standards[0].maxCant, 0.15f) && near(table.radiusCant.at(500.0f), 0.105556f),
              "radius table and its standard");
    }

    // The speed in effect.
    {
        Network net;
        const std::vector<int> straights(10, 10);
        const int a = net.path(straights, 100);
        net.post(a, 30, TRitem::SIGN, 60, true);
        net.post(a, 40, TRitem::WARNING, 40, true);
        net.post(a, 70, TRitem::SIGN, 80, false);
        net.post(a, 85, TRitem::RESUME, 0, true);
        TrackCant::SpeedScan scan(net.db);
        check(near(scan.at(a, 50, true, false), 60 * Kmh, 1e-4f) && near(scan.at(a, 50, true, true), 60 * Kmh, 1e-4f),
              "the last post passed, warning posts not counted");
        check(near(scan.at(a, 90, true, false), 999.0f), "a resume post");
        check(scan.at(a, 20, true, false) < 0 && scan.at(a, 80, false, false) < 0, "none before the first post");
        check(near(scan.at(a, 50, false, false), 80 * Kmh, 1e-4f), "the other way");

        const Route route = Route::fromBlocks({}, 0.0f, 100 * Kmh, true);
        TrackCant::computeAll(net.db, route);
        const auto &s = net.db.cant.nodes[a];
        // Middles at 55, 75 and 95 m: both ways, one way, after the resume.
        check(near(s[5].passengerSpeed, 80 * Kmh, 1e-4f) && near(s[7].passengerSpeed, 60 * Kmh, 1e-4f)
                      && near(s[9].passengerSpeed, 100 * Kmh, 1e-4f) && near(s[9].freightSpeed, 100 * Kmh, 1e-4f),
              "sections take the higher way, at most the route's limit");

        Network junction;
        const int in = junction.path(straights, 100);
        const int out = junction.path(std::vector<int>(5, 10), 200, 0, -100);
        junction.join(in, out);
        junction.post(in, 30, TRitem::SIGN, 60, true);
        junction.post(out, 20, TRitem::SIGN, 80, false);
        TrackCant::SpeedScan through(junction.db);
        check(near(through.at(out, 10, true, false), 60 * Kmh, 1e-4f), "back through a junction from its branch");
        check(near(through.at(in, 50, false, false), 80 * Kmh, 1e-4f),
              "back through a junction from its input, its main route");
    }

    // Curves.
    {
        const float speed = 100 * Kmh;
        const Route route = Route::fromBlocks({}, 0.0f, speed, true);
        const Standard &standard = route.standards[0];
        const float slope = std::min(standard.runoffSlope, standard.runoffSpeed / speed);
        const float curve = 500.0f * float(5.0 * M_PI / 180.0);

        Network net;
        const std::vector<int> layout = {10, 10, 10, 20, 20, 20, 20, 10, 10, 10};
        const int node = net.path(layout, 100);
        check(net.inOrder(node, layout), "test track in order");
        TrackCant::computeAll(net.db, route);
        const auto &s = net.db.cant.nodes[node];
        const std::vector<int> run = {2, 3, 4, 5, 6, 7};
        const std::vector<float> lengths = {10, curve, curve, curve, curve, 10};
        const float runLength = 20 + 4 * curve;
        const float factor = route.gauge / (9.81f * 500.0f);
        float expected = std::max(factor * speed * speed - standard.maxPassengerUnderbalance,
                                  factor * speed * speed - standard.maxFreightUnderbalance);
        expected = float(std::round(double(expected / standard.precision))) * standard.precision;
        expected = std::clamp(expected, standard.minCant, std::min(standard.maxCant, runLength / 2.6f * slope));
        check(near(s[3].nominal, expected) && near(s[6].nominal, expected) && near(expected, 0.125f, 1e-6f),
              "a curve's nominal cant");
        check(near(s[2].nominal, 0) && near(s[7].nominal, 0) && s[0].nominal < 0 && s[1].nominal < 0
                      && s[8].nominal < 0 && s[0].points == 0 && s[9].points == 0,
              "the run: the straights next to the curve, none 20 m from the node's ends");
        check(tablesValid(s, run, lengths, slope, route.gauge, 1), "runoff tables of a curve to the right");
        float top = 0;
        for (int k : run)
            for (int p = 0; p < s[size_t(k)].points; ++p)
                top = std::max(top, s[size_t(k)].physical[p]);
        check(near(top, expected) && s[2].curve == s[7].curve && s[2].curve >= 0, "the curve reaches its cant");

        Network reverse;
        const std::vector<int> layout2 = {10, 10, 10, 20, 20, 21, 21, 10, 10, 10};
        const int node2 = reverse.path(layout2, 100);
        TrackCant::computeAll(reverse.db, route);
        const auto &r = reverse.db.cant.nodes[node2];
        check(r[3].curve != r[6].curve && near(r[4].physical[r[4].points - 1], 0) && near(r[5].physical[0], 0)
                      && tablesValid(r, {2, 3, 4}, {10, curve, curve}, slope, route.gauge, 1)
                      && tablesValid(r, {5, 6, 7}, {curve, curve, 10}, slope, route.gauge, -1),
              "a reverse curve: two runs, flat where it turns");

        Network strict;
        const int node3 = strict.path(layout, 100);
        TrackCant::computeAll(strict.db, Route::fromBlocks(
                {QStringLiteral("ORTSSuperElevation ( MinimumCant ( 200mm ) )")}, 0.0f, speed, true));
        const auto &t = strict.db.cant.nodes[node3];
        bool flat = true;
        for (const TrackCant::Section &section : t)
            flat = flat && section.points == 0;
        check(flat && near(t[2].nominal, 0) && t[3].nominal < 0, "a curve too short for the minimum cant: none");

        Network band;
        const int node4 = band.path(layout, 100);
        TrackCant::computeAll(band.db, Route::fromBlocks(
                {QStringLiteral("ORTSSuperElevation ( MinimumSpeed ( 120km/h ) )")}, 0.0f, speed, true));
        check(band.db.cant.nodes[node4][4].points == 0, "no standard for the curve's speed: none");

        Network legacy;
        const int node5 = legacy.path(layout, 100);
        TrackCant::computeAll(legacy.db, Route::fromBlocks(
                {QStringLiteral("ORTSTrackSuperElevation ( 100 0.15 1000 0.05 )")}, 0.0f, speed, true));
        check(near(legacy.db.cant.nodes[node5][4].nominal, 0.105f), "the radius table gives the cant");
    }

    // Edits: only the changed nodes and those whose speeds changed.
    {
        const Route route = Route::fromBlocks({}, 0.0f, 100 * Kmh, true);
        Network net;
        const std::vector<int> layout = {10, 10, 10, 20, 20, 20, 20, 10, 10, 10};
        const int c = net.path(layout, 100);
        const int d = net.path(layout, 200, 0, -300);
        TrackCant::computeAll(net.db, route);
        const float before = net.db.cant.nodes[c][4].nominal;
        check(TrackCant::update(net.db, route, {}).empty(), "nothing changed, nothing computed");
        net.post(c, 5, TRitem::SIGN, 40, true);
        net.post(c, 225, TRitem::SIGN, 40, false);
        const std::set<int> slower = TrackCant::update(net.db, route, {});
        const auto edited = net.db.cant.nodes;
        TrackCant::computeAll(net.db, route);
        bool same = true;
        for (const auto &entry : edited)
            for (size_t i = 0; i < entry.second.size(); ++i)
                same = same && entry.second[i].sameCant(net.db.cant.nodes[entry.first][i]);
        check(slower == std::set<int>{c} && net.db.cant.nodes[c][4].nominal < before && same,
              "a speed post: that node again, as a whole computation gives it");
        check(TrackCant::update(net.db, route, {d}) == std::set<int>{d}, "a changed node computed again");
        // A dragged speed post object reports its items every frame: nothing
        // computed unless one reads differently (Route::refreshTrackCant).
        const int limit = net.post(d, 50, TRitem::SIGN, 70, true);
        TrackCant::update(net.db, route, {d});
        TRitem *item = net.db.trackItems[limit];
        const bool unchanged = !TrackCant::postsChanged(net.db, {limit}) && !TrackCant::postsChanged(net.db, {12345});
        item->setSpeedpostSpeed(50);
        const bool slowerPost = TrackCant::postsChanged(net.db, {limit});
        item->setSpeedpostSpeed(70);
        item->setTrackPosition(60);
        const bool movedPost = TrackCant::postsChanged(net.db, {limit});
        item->setTrackPosition(50);
        const bool back = !TrackCant::postsChanged(net.db, {limit});
        net.db.trackItems[limit] = nullptr;
        const bool gone = TrackCant::postsChanged(net.db, {limit});
        net.db.trackItems[limit] = item;
        check(unchanged && slowerPost && movedPost && back && gone,
              "a speed post reported unchanged computes nothing; changed, moved or deleted it does");
        delete net.db.trackNodes[d];
        net.db.trackNodes[d] = nullptr;
        check(TrackCant::update(net.db, route, {}).count(d) == 1 && net.db.cant.nodes.count(d) == 0,
              "a deleted node dropped");
    }

    // Generated profiles roll as Open Rails' SuperElevationPrimitive: about
    // the rail that stays, 0.275 m up; PositionControl; straights cut.
    {
        const QSharedPointer<OrtsTrackProfile> profile = OrtsTrackProfileParser::parseStf(
                "SIMISA@@@@@@@@@@JINX0p0t______\n"
                "TrProfile (\n Name ( \"Roll\" )\n ChordSpan ( 1 )\n TrackGauge ( 1.435m )\n"
                " SuperElevationMethod ( Outside )\n LOD (\n  CutoffRadius ( 1200m )\n"
                "  LODItem (\n   Name ( Rails ) TexName ( test.ace ) ShaderName ( TexDiff )\n"
                "   LightModelName ( OptSpecular0 ) AlphaTestMode ( 0 ) TexAddrModeName ( Wrap )\n"
                "   ESD_Alternative_Texture ( 0 ) MipMapLevelOfDetailBias ( 0 )\n"
                "   Polyline ( Name ( top ) DeltaTexCoord ( 0 0.2 )\n"
                "    Vertex ( Position ( -0.7175 0.275 ) Normal ( 0 1 0 ) TexCoord ( 0 0 ) PositionControl ( All ) )\n"
                "    Vertex ( Position ( 0.7175 0.275 ) Normal ( 0 1 0 ) TexCoord ( 1 0 ) PositionControl ( All ) )\n"
                "   )\n"
                "   Polyline ( Name ( fixed ) DeltaTexCoord ( 0 0.2 )\n"
                "    Vertex ( Position ( -2 0 ) Normal ( 0 1 0 ) TexCoord ( 0 0 ) PositionControl ( None ) )\n"
                "    Vertex ( Position ( -1.9 0 ) Normal ( 0 1 0 ) TexCoord ( 1 0 ) PositionControl ( None ) )\n"
                "   )\n"
                "   Polyline ( Name ( inner ) DeltaTexCoord ( 0 0.2 )\n"
                "    Vertex ( Position ( 1.5 0.275 ) Normal ( 0 1 0 ) TexCoord ( 0 0 ) PositionControl ( Outside ) )\n"
                "    Vertex ( Position ( 1.6 0.275 ) Normal ( 0 1 0 ) TexCoord ( 1 0 ) PositionControl ( Outside ) )\n"
                "   )\n  )\n )\n)\n");
        check(profile != nullptr && profile->valid, "roll test profile parsed");
        if (profile != nullptr && profile->valid) {
            const QVector<TSection> straight = {TSection(0, 0, 40.0f, 0.0f)};
            // A right curve's angle (negative), constant along the section.
            OrtsProfileRoll roll;
            OrtsProfileRoll::Span span;
            span.length = 40.0f;
            span.cant.points = 2;
            span.cant.position[1] = 1.0f;
            span.cant.visual[0] = span.cant.visual[1] = -0.05f;
            span.cant.physical[0] = span.cant.physical[1] = 0.0717f;
            roll.spans.append(span);
            roll.pivot = OrtsProfileRoll::pivotOf(*profile);
            OrtsProfileRoll reversed = roll;
            reversed.spans[0].reversed = true;

            struct Seen {
                std::set<int> along;
                float top = -1, low = 99, raisedX = 0, fixedY = -1, innerY = -1;
            };
            const auto look = [](const QVector<OrtsGeneratedProfileMesh> &meshes) {
                Seen seen;
                for (const OrtsGeneratedProfileMesh &mesh : meshes) {
                    for (int v = 0; v + 8 < mesh.vertices.size(); v += 9) {
                        const float x = mesh.vertices[v], y = mesh.vertices[v + 1], z = mesh.vertices[v + 2];
                        seen.along.insert(int(std::lround(z * 100)));
                        // Mesh x is the profile's mirrored (TSRE's +Z forward).
                        if (std::fabs(std::fabs(x) - 1.95f) < 0.1f)
                            seen.fixedY = std::max(seen.fixedY, y);
                        else if (std::fabs(std::fabs(x) - 1.55f) < 0.1f)
                            seen.innerY = std::max(seen.innerY, y);
                        else {
                            if (y > seen.top) {
                                seen.top = y;
                                seen.raisedX = x;
                            }
                            seen.low = std::min(seen.low, y);
                        }
                    }
                }
                return seen;
            };
            QVector<OrtsGeneratedProfileMesh> flatMeshes, rolledMeshes, reversedMeshes;
            OrtsTrackProfileRenderer::buildMeshes(*profile, straight, flatMeshes);
            OrtsTrackProfileRenderer::buildMeshes(*profile, straight, rolledMeshes, nullptr, 0, 0, nullptr, 0, &roll);
            OrtsTrackProfileRenderer::buildMeshes(*profile, straight, reversedMeshes, nullptr, 0, 0, nullptr, 0,
                                                  &reversed);
            const Seen flat = look(flatMeshes), rolled = look(rolledMeshes), back = look(reversedMeshes);
            const float raised = 0.275f + 1.435f * std::sin(0.05f);
            check(flat.along.size() == 2 && rolled.along.size() == 5,
                  "a superelevated straight is cut every 10 m (ChordSpan 1), a flat one not");
            check(near(flat.top, 0.275f) && near(rolled.top, raised, 1e-4f) && near(rolled.low, 0.275f, 1e-4f)
                          && rolled.raisedX > 0 && back.raisedX < 0 && near(back.top, raised, 1e-4f),
                  "the outer rail rises about the inner one, the other way when the path is reversed");
            check(near(rolled.fixedY, 0.0f) && near(rolled.innerY, 0.275f, 1e-4f) && near(back.innerY, raised, 0.12f)
                          && back.innerY > 0.3f,
                  "PositionControl None stays, Outside stays on the curve's inner side");
        }
    }

    // Cant taken away where it is not drawn: junctions and crossovers (no
    // cant), tunnels (no visual cant), objects drawn without it.
    {
        const Route route = Route::fromBlocks({}, 0.0f, 100 * Kmh, true);
        const std::vector<int> layout = {10, 10, 10, 20, 20, 20, 20, 10, 10, 10};
        TrackShape junction(30), tunnel(31);
        junction.numpaths = 2;
        junction.mainroute = 0;
        tunnel.numpaths = 1;
        tunnel.tunnelshape = true;

        Network net;
        net.definitions.shape[30] = &junction;
        net.definitions.shape[31] = &tunnel;
        const int node = net.path(layout, 100);
        TrackCant::computeAll(net.db, route);
        const std::vector<TrackCant::Section> before = net.db.cant.nodes[node];
        net.db.trackNodes[node]->trVectorSection[3].shapeIndex = 30;
        net.db.trackNodes[node]->trVectorSection[6].shapeIndex = 31;
        TrackCant::computeAll(net.db, route);
        const auto &s = net.db.cant.nodes[node];
        bool junctionFlat = s[3].curve < 0 && s[3].nominal == 0.0f;
        for (int p = 0; p < 4; ++p)
            junctionFlat = junctionFlat && s[3].physical[p] == 0.0f && s[3].visual[p] == 0.0f;
        check(junctionFlat && s[2].physical[s[2].points - 1] == 0.0f && s[4].physical[0] == 0.0f
                      && s[4].visual[0] == 0.0f && s[4].physical[1] == before[4].physical[1],
              "a junction's section: no cant, its neighbours' joining ends flat");
        bool tunnelFlat = s[6].curve < 0;
        for (int p = 0; p < s[6].points; ++p)
            tunnelFlat = tunnelFlat && s[6].visual[p] == 0.0f && s[6].physical[p] == before[6].physical[p];
        check(tunnelFlat && s[7].visual[0] == 0.0f && s[7].physical[0] == before[7].physical[0]
                      && s[5].visual[s[5].points - 1] == 0.0f,
              "a tunnel's section: physical cant kept, none drawn");

        Network loaded;
        const int node2 = loaded.path(layout, 100);
        TrackCant::computeAll(loaded.db, route);
        const std::vector<TrackCant::Section> drawn = loaded.db.cant.nodes[node2];
        TrackCant::setVisualOff(loaded.db, 0, 0, 104, true);
        const auto &off = loaded.db.cant.nodes[node2];
        check(off[4].visualAt(0.5f) == 0.0f && off[4].physicalAt(0.5f) == drawn[4].physicalAt(0.5f)
                      && off[3].visual[off[3].points - 1] == 0.0f && off[5].visual[0] == 0.0f,
              "an object drawn without superelevation: none drawn on its sections");
        TrackCant::computeAll(loaded.db, route);
        check(loaded.db.cant.nodes[node2][4].visualAt(0.5f) == 0.0f, "kept when the database is computed again");
        TrackCant::setVisualOff(loaded.db, 0, 0, 104, false);
        bool restored = true;
        for (size_t i = 0; i < drawn.size(); ++i)
            restored = restored && drawn[i].sameCant(loaded.db.cant.nodes[node2][i]);
        check(restored, "and drawn again when it is");

        // The pivot its profile rolls about: cars there rise with it.
        TrackCant::setPivot(loaded.db, 0, 0, 104, 0.7175f);
        check(near(loaded.db.cant.nodes[node2][4].pivot, 0.7175f) && loaded.db.cant.nodes[node2][3].pivot == 0.0f
                      && loaded.db.cant.nodes[node2][5].pivot == 0.0f,
              "an object's pivot on its sections");
        TrackCant::computeAll(loaded.db, route);
        check(near(loaded.db.cant.nodes[node2][4].pivot, 0.7175f) && drawn[4].sameCant(loaded.db.cant.nodes[node2][4]),
              "the pivot kept when the database is computed again, not part of the cant");
        const ProceduralTracksMode mode = Game::proceduralTracks;
        const int forced = Game::trackCantForced;
        Game::proceduralTracks = ProceduralTracksMode::Superelevation;
        Game::trackCantForced = -1;
        float pivotSeen = 0.0f, rollSeen = 0.0f, pivotElsewhere = 0.0f;
        float pose[7];
        for (float d = 0.5f;; d += 1.0f) {
            float elev = 0.0f, pivot = 0.0f;
            if (!loaded.db.getDrawPositionOnTrNode(pose, node2, d, &elev, &pivot))
                break;
            if (pivot > 0.0f) {
                pivotSeen = pivot;
                rollSeen = std::max(rollSeen, std::fabs(elev));
            } else {
                pivotElsewhere = std::max(pivotElsewhere, std::fabs(pivot));
            }
        }
        bool hidden = true;
        Game::proceduralTracks = ProceduralTracksMode::Enabled;
        for (float d = 0.5f;; d += 1.0f) {
            float pivot = 1.0f;
            if (!loaded.db.getDrawPositionOnTrNode(pose, node2, d, nullptr, &pivot))
                break;
            hidden = hidden && pivot == 0.0f;
        }
        Game::proceduralTracks = mode;
        Game::trackCantForced = forced;
        check(near(pivotSeen, 0.7175f) && rollSeen > 0.01f && pivotElsewhere == 0.0f && hidden,
              "the track gives its pivot with its roll where superelevation is shown");
        check(near(CarPlacement::liftOf(0.7175f, 0.1f), 0.7175f * std::sin(0.1f))
                      && near(CarPlacement::liftOf(0.7175f, -0.1f), 0.7175f * std::sin(0.1f))
                      && near(CarPlacement::liftOf(-0.7175f, 0.1f), -0.7175f * std::sin(0.1f))
                      && CarPlacement::liftOf(0.0f, 0.1f) == 0.0f,
              "a car rises with an outside pivot, sinks with an inside one, either way of the curve");
        float raised[16], level[16];
        Mat4::identity(raised);
        Mat4::identity(level);
        const float front[7] = {0, 0, 10, 0, 0, 0, 0}, rear[7] = {0, 0, -10, 0, 0, 0, 0}, tile[2] = {0, 0};
        CarPlacement::placeCar(raised, tile, front, rear, 0.1f, false, 0.05f);
        CarPlacement::placeCar(level, tile, front, rear, 0.1f, false);
        check(near(raised[13] - level[13], 0.05f) && near(raised[12], level[12]) && near(raised[14], level[14]),
              "the car placed that much higher");

        // A car leans with its track, into the curve, facing either way: Ruch
        // turns the drawn angle by its way, placeCar rolls the car by it; the
        // car's side toward the curve's centre is the lower one. On a grade
        // the car also keeps its track's slope: it rolls about its own
        // pitched axis, which runs along the track between its two ends.
        Game::proceduralTracks = ProceduralTracksMode::Superelevation;
        Game::trackCantForced = -1;
        TDB *trackDBWas = Game::trackDB;
        auto leansIn = [&](Network &net, int node, float grade) {
            Game::trackDB = &net.db;
            float steepest = -1.0f, most = 0.0f;
            for (float d = 0.5f;; d += 1.0f) {
                float elev = 0.0f;
                if (!net.db.getDrawPositionOnTrNode(pose, node, d, &elev))
                    break;
                if (std::fabs(elev) > most) {
                    most = std::fabs(elev);
                    steepest = d;
                }
            }
            auto ground = [&](float d, float *p) {
                float out[7];
                net.db.getDrawPositionOnTrNode(out, node, d);
                p[0] = out[0];
                p[1] = out[1];
                p[2] = -out[2];
            };
            float behind[3], here[3], beyond[3];
            ground(steepest - 3.0f, behind);
            ground(steepest, here);
            ground(steepest + 3.0f, beyond);
            const float centre[2] = {behind[0] + beyond[0] - 2.0f * here[0], behind[2] + beyond[2] - 2.0f * here[2]};
            bool good = steepest > 0.0f && most > 0.01f
                    && near((beyond[1] - behind[1]) / 6.0f, -std::tan(grade), 0.002f);
            for (int way = 0; way < 2; ++way) {
                Ruch front, back;
                const float ahead = way == 0 ? 2.0f : -2.0f;
                front.place(node, steepest + ahead, way);
                back.place(node, steepest - ahead, way);
                float frontElev = 0.0f, backElev = 0.0f, frontAt[7], backAt[7];
                const float *f = front.getCurrentPosition(&frontElev);
                std::copy(f, f + 7, frontAt);
                const float *b = back.getCurrentPosition(&backElev);
                std::copy(b, b + 7, backAt);
                float car[16];
                Mat4::identity(car);
                const float carTile[2] = {frontAt[5], frontAt[6]};
                CarPlacement::placeCar(car, carTile, frontAt, backAt, (frontElev + backElev) / 2.0f, false);
                float one[3] = {1.0f, 0.0f, 0.0f}, other[3] = {-1.0f, 0.0f, 0.0f}, middle[3] = {0.0f, 0.0f, 0.0f};
                float forward[3] = {0.0f, 0.0f, 1.0f};
                Vec3::transformMat4(one, one, car);
                Vec3::transformMat4(other, other, car);
                Vec3::transformMat4(middle, middle, car);
                Vec3::transformMat4(forward, forward, car);
                const bool oneInside = (one[0] - middle[0]) * centre[0] + (one[2] - middle[2]) * centre[1] > 0.0f;
                const float inside = oneInside ? one[1] : other[1], outside = oneInside ? other[1] : one[1];
                good = good && inside < outside - std::sin(most);
                // Its axis along the chord between its ends (drawn as placeCar
                // has them: z turned, the rear on the front's tile).
                const float chord[3] = {frontAt[0] - backAt[0], frontAt[1] - backAt[1], -(frontAt[2] - backAt[2])};
                const float length = std::sqrt(chord[0] * chord[0] + chord[1] * chord[1] + chord[2] * chord[2]);
                const float along[3] = {forward[0] - middle[0], forward[1] - middle[1], forward[2] - middle[2]};
                const float off[3] = {along[1] * chord[2] - along[2] * chord[1], along[2] * chord[0] - along[0] * chord[2],
                                      along[0] * chord[1] - along[1] * chord[0]};
                const float sine = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]) / std::max(length, 1e-3f);
                good = good && length > 3.9f && sine < 0.001f
                        && along[0] * chord[0] + along[1] * chord[1] + along[2] * chord[2] > 0.0f;
            }
            return good;
        };
        check(leansIn(loaded, node2, 0.0f), "a car leans into its curve with its track, facing either way");
        Network graded;
        const float grade = std::atan(0.03f);
        const int node5 = graded.path(layout, 100, 0, 0, grade);
        TrackCant::computeAll(graded.db, route);
        check(leansIn(graded, node5, grade), "on a 3 % grade too, along the slope");
        Game::trackDB = trackDBWas;
        Game::proceduralTracks = mode;
        Game::trackCantForced = forced;
    }

    // Track profiles for superelevated static track: Open Rails' default
    // and its guess.
    {
        QTemporaryDir route;
        OrtsTrackProfileCatalog::load(route.path(), true);
        const QSharedPointer<const OrtsTrackProfile> builtIn = OrtsTrackProfileCatalog::builtInDefault(1.435f);
        check(builtIn != nullptr && builtIn->valid && builtIn->lods.size() == 3
                      && builtIn->superElevationMethod == OrtsTrackProfile::SuperElevationMethod::Outside
                      && near(builtIn->trackGauge, 1.435f) && builtIn->lods[0].items.size() == 1
                      && builtIn->lods[0].items[0].textureName == "acleantrack2.ace"
                      && builtIn->lods[2].items[0].textureName == "acleantrack1.ace",
              "the built-in default profile, Open Rails' canned one");
        check(OrtsTrackProfileCatalog::defaultTrack(1.435f) == builtIn, "no route profile: the built-in default");
        check(OrtsTrackProfileCatalog::guess("A1t100mStrt.s", {"acleantrack1.ace", "acleantrack2.ace"}, 1.435f)
                      == builtIn
                      && OrtsTrackProfileCatalog::guess("Other.s", {"concrete.ace"}, 1.435f) == nullptr,
              "the guess by texture names");
        check(!OrtsTrackProfileCatalog::guessNeedsImages(1.435f, true)
                      && OrtsTrackProfileCatalog::guessNeedsImages(1.435f, false),
              "the built-in default alone: the guess needs no shape file, unless no fit stays static");

        QDir(route.path()).mkdir("TRACKPROFILES");
        // A route profile: by its shape filter, else by its texture names.
        auto writeProfile = [&](const QString &name, const QString &filter) {
            QFile file(route.path() + "/TRACKPROFILES/TrProfile_" + name + ".stf");
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
                return;
            QTextStream out(&file);
            out << "SIMISA@@@@@@@@@@JINX0p0t______\n\nTrProfile (\n Name ( \"" << name << "\" )\n";
            if (!filter.isEmpty())
                out << " IncludedShapes ( \"" << filter << "\" )\n";
            out << " SuperElevationMethod ( Both )\n LOD (\n  CutoffRadius ( 1200m )\n"
                   "  LODItem (\n   Name ( Rails ) TexName ( db.ace ) ShaderName ( TexDiff )\n"
                   "   LightModelName ( OptSpecular0 ) AlphaTestMode ( 0 ) TexAddrModeName ( Wrap )\n"
                   "   ESD_Alternative_Texture ( 0 ) MipMapLevelOfDetailBias ( 0 )\n"
                   "   Polyline ( Name ( top ) DeltaTexCoord ( 0 0.2 )\n"
                   "    Vertex ( Position ( -1 0.2 ) Normal ( 0 1 0 ) TexCoord ( 0 0 ) )\n"
                   "    Vertex ( Position ( 1 0.2 ) Normal ( 0 1 0 ) TexCoord ( 1 0 ) )\n"
                   "   )\n  )\n )\n)\n";
        };
        writeProfile("DB", "DB10*");
        OrtsTrackProfileCatalog::load(route.path(), true);
        const QSharedPointer<const OrtsTrackProfile> db =
                OrtsTrackProfileCatalog::guess("DB10_100m.s", {"db_rails.ace"}, 1.435f);
        check(db != nullptr && db->superElevationMethod == OrtsTrackProfile::SuperElevationMethod::Both
                      && OrtsTrackProfileCatalog::guess("Other2.s", {"acleantrack1.ace", "acleantrack2.ace"}, 1.435f)
                                 == builtIn,
              "the guess by a route profile's filters, the default by its textures");
        check(OrtsProfileRoll::pivotOf(*db) == 0.0f && near(OrtsProfileRoll::pivotOf(*builtIn), 0.7175f),
              "pivots: Both at the centre, Outside at the inner rail");
        check(!OrtsTrackProfileCatalog::guessNeedsImages(1.435f, true),
              "profiles choosing by shape names (as OR_PSK_NEW's): the guess needs no shape file");
        writeProfile("Plain", QString());
        OrtsTrackProfileCatalog::load(route.path(), true);
        const QSharedPointer<const OrtsTrackProfile> again =
                OrtsTrackProfileCatalog::guess("DB10_100m.s", {"db_rails.ace"}, 1.435f);
        check(OrtsTrackProfileCatalog::guessNeedsImages(1.435f, true) && again != nullptr && again != db,
              "one choosing by texture names: the guess needs them; reloaded profiles guessed again");
        OrtsTrackProfileCatalog::load(QString(), true);
    }

    qInfo() << "[tests:track-cant] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
