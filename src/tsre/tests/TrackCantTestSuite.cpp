#include <tsre/tests/TrackCantTestSuite.h>

#include <QDebug>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <vector>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackCant.h>

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

    // A vector node of these sections in this order; its id.
    int path(const std::vector<int> &sections, int uid, float x = 0, float z = 0) {
        float position[3] = {x, 0, z};
        float frame[3] = {0, 0, 0};
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
        delete net.db.trackNodes[d];
        net.db.trackNodes[d] = nullptr;
        check(TrackCant::update(net.db, route, {}).count(d) == 1 && net.db.cant.nodes.count(d) == 0,
              "a deleted node dropped");
    }

    qInfo() << "[tests:track-cant] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
