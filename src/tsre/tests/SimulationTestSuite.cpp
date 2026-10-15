#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace {

using PathTrack::Node;
using PathTrack::NodeType;

// The geometry copy and the track database give the same pose; both or
// neither give one.
bool samePose(TDB &db, const Sim::TrackGeometry &geometry, int node, float distance, float *worst) {
    float out[7] = {0, 0, 0, 0, 0, 0, 0};
    Sim::TrackPose pose;
    const bool a = db.getDrawPositionOnTrNode(out, node, distance);
    const bool b = geometry.pose(node, distance, pose);
    if (a != b)
        return false;
    if (!a)
        return true;
    const float d = std::max({std::fabs(out[0] - pose.x), std::fabs(out[1] - pose.y), std::fabs(out[2] - pose.z),
                              std::fabs(out[3] - pose.yaw), std::fabs(out[4] - pose.pitch)});
    if (worst != nullptr)
        *worst = std::max(*worst, d);
    return d < 1e-4f && int(out[5]) == pose.tileX && int(out[6]) == pose.tileZ;
}

TRnode *vectorNode(TDB &db, int id, const QVector<int> &sections, float ax, float ay, float az) {
    TRnode *node = new TRnode();
    node->typ = 1;
    node->iTrv = sections.size();
    node->trVectorSection = new TrackVectorSection[sections.size()];
    for (int i = 0; i < sections.size(); ++i) {
        TrackVectorSection &s = node->trVectorSection[i];
        s.sectionIndex = sections[i];
        s.tileX = -3 + i;
        s.tileZ = 7;
        s.x = 100.0f + 37.0f * i;
        s.y = 12.0f + i;
        s.z = -250.0f + 11.0f * i;
        s.ax = ax * (i + 1);
        s.ay = ay + 0.4f * i;
        s.az = az * i;
    }
    db.trackNodes[id] = node;
    db.iTRnodes = std::max(db.iTRnodes, id);
    return node;
}

QString partsText(const Sim::TrainRoute &route) {
    QStringList parts;
    for (const Sim::RouteTrack &part : route.parts) {
        QStringList intervals;
        for (const Sim::RouteInterval &i : part.intervals())
            intervals << QString("%1:%2-%3").arg(i.node).arg(i.from, 0, 'f', 0).arg(i.to, 0, 'f', 0);
        parts << intervals.join(",");
    }
    return parts.join(" | ");
}

}

int TsreTests::runSimulationCoreSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-core] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-core] FAIL" << name; }
    };

    // The geometry copy: straight and curved sections, turned every way,
    // over several tiles.
    {
        TSectionDAT sections(false, false);
        TSection straight(900010, 0, 25.0f, 0.0f);
        TSection right(900011, 1, 0.3f, 200.0f);
        TSection left(900012, 1, -0.25f, 150.0f);
        sections.sekcja[900010] = &straight;
        sections.sekcja[900011] = &right;
        sections.sekcja[900012] = &left;
        TDB db(&sections, false);
        db.loaded = true;
        vectorNode(db, 5, {900010, 900011, 900012, 900010}, 0.0f, 0.0f, 0.0f);
        vectorNode(db, 6, {900012, 900010, 900011}, 0.02f, 1.3f, 0.05f);
        vectorNode(db, 7, {900011, 900099}, -0.01f, -2.1f, 0.0f);
        Sim::TrackGeometry geometry(&db);
        bool same = true;
        float worst = 0.0f;
        for (int node : {5, 6, 7, 8})
            for (float d = -1.0f; d < 200.0f; d += 0.37f)
                same = same && samePose(db, geometry, node, d, &worst);
        check(same, QString("the geometry copy gives the track database's poses (worst %1)").arg(worst));
        check(std::fabs(geometry.length(5) - (25 + 60 + 37.5f + 25)) < 0.01f && geometry.length(8) < 0,
              "node lengths, none for a node not there");
        Sim::TrackPose pose;
        check(geometry.pose(7, 30.0f, pose) && !geometry.pose(7, 70.0f, pose),
              "a node with a missing section answers only before it");
        sections.sekcja.clear();
    }

    // Routes: the passing loop, a path that reverses on track 13 and comes
    // back over the loop's other track.
    TestTrack::Layout l;
    TestTrack::passingLoop(l);
    TDB *tdb = &l.db;
    const QScopedValueRollback<TDB*> trackDB(Game::trackDB, tdb);
    PathTrack::Result path = PathTrack::startAt(tdb, {10, 20.0f});
    path = PathTrack::reverseAt(tdb, path.nodes, {13, 60.0f});
    int secondJunction = -1;
    for (int k = 0; k < path.nodes.size(); ++k)
        if (path.nodes[k].type == NodeType::Junction && path.nodes[k].track == 3)
            secondJunction = k;
    if (path.ok && secondJunction > 0)
        path = PathTrack::otherExit(tdb, path.nodes, secondJunction);
    check(path.ok, "a path out over track 11, reversing on 13 and back over 12");
    const QVector<PathTrack::Leg> legs = PathTrack::legs(tdb, path.nodes);
    const Sim::TrainRoute route = Sim::BuildRoute(tdb, path.nodes, legs, 100.0);
    const QString text = partsText(route);
    check(route.parts.size() == 2 && route.leadInEnded && std::fabs(route.pathStart - 20.0) < 0.01,
          "two parts; the lead-in ends at the track end 20 m behind the start: " + text);
    bool outOver11 = false, backOver12 = false, backOver11 = false;
    if (route.parts.size() == 2) {
        for (const Sim::RouteInterval &i : route.parts[0].intervals())
            outOver11 = outOver11 || i.node == 11;
        for (const Sim::RouteInterval &i : route.parts[1].intervals()) {
            backOver12 = backOver12 || i.node == 12;
            backOver11 = backOver11 || i.node == 11;
        }
    }
    check(outOver11 && backOver12 && !backOver11, "each pass of a junction keeps its own branch: " + text);
    check(route.parts.size() == 2 && route.parts[0].intervals().back().node == 13
              && std::fabs(route.parts[0].intervals().back().to - 60.0f) < 0.01f
              && route.parts[1].intervals().front().node == 13
              && !route.parts[1].intervals().front().forward(),
          "the first part ends at the reversal point, the second leaves it the other way");
    if (!route.parts.empty()) {
        const Sim::RoutePoint start = route.parts[0].locate(route.pathStart);
        const Sim::RoutePoint behind = route.parts[0].locate(0.0);
        check(start.node == 10 && std::fabs(start.distance - 20.0f) < 0.01f && start.forward
                  && behind.node == 10 && std::fabs(behind.distance) < 0.01f,
              "route distances map to the track: the path's start, the lead-in's end");
        int part = -1;
        double at = -1;
        const double firstLength = route.parts[0].length() - route.pathStart;
        check(route.place(firstLength + 15.0, part, at) && part == 1 && std::fabs(at - 15.0) < 0.01
                  && route.place(5.0, part, at) && part == 0 && std::fabs(at - 25.0) < 0.01,
              "path distances find their part and route distance");
    }

    // The core: a train of two 20 m cars at 10 m/s along that route.
    auto input = std::make_shared<Sim::SimulationInput>();
    input->startTime = 8 * 3600;
    input->geometry = std::make_shared<const Sim::TrackGeometry>(tdb);
    Sim::TrainPlan train;
    train.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
    train.length = 40.0f;
    train.maxSpeed = 30.0f;
    Sim::SetRates(train);
    train.startTime = 8 * 3600 + 10;
    train.route = route;
    // As MakeInput places it: the front half a car past the start, moved
    // ahead where the lead-in ended.
    train.startFront = std::max(route.pathStart + 10.0, 41.0);
    input->trains.push_back(train);
    Sim::SimulationCore core(input);
    std::vector<Sim::TrackPose> fronts, rears;
    check(core.trains()[0].status == Sim::TrainStatus::Waiting && !core.vehicleEnds(0, fronts, rears),
          "a train waits for its start time, off the track");
    for (int i = 0; i < 15; ++i)
        core.step(1.0);
    const Sim::TrainState &moving = core.trains()[0];
    check(moving.status == Sim::TrainStatus::Running && std::fabs(moving.front - (41.0 + 50.0)) < 0.01,
          QString("it starts at its time and runs: front at %1").arg(moving.front));
    check(core.vehicleEnds(0, fronts, rears) && fronts.size() == 2
              && std::fabs(std::hypot(fronts[0].x - rears[0].x, fronts[0].z - rears[0].z) - 20.0f) < 0.05f
              && std::fabs(rears[0].x - fronts[1].x) < 0.05f && fronts[0].x > rears[1].x,
          "its cars' ends: 20 m apart, coupled, the first ahead");
    int steps = 0;
    while (core.trains()[0].part == 0 && steps++ < 1000)
        core.step(1.0);
    check(core.trains()[0].part == 1 && core.trains()[0].reversed && std::fabs(core.trains()[0].front - 40.0) < 10.01,
          "at the reversal point it turns: the last car leads");
    while (core.trains()[0].status != Sim::TrainStatus::Finished && steps++ < 10000)
        core.step(1.0);
    check(core.trains()[0].status == Sim::TrainStatus::Finished, "at its path's end it finishes");

    qInfo().noquote() << QString("simulation-core: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runSimulationRouteSuite(bool verbose) {
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "simulation-route requires a route with one TDB (--game-root, --route)";
        return 1;
    }
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-route] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-route] FAIL" << name; }
    };
    const QScopedValueRollback<QString> routeName(Game::routeName, databases.first().completeBaseName());
    TSectionDAT sections(false, false);
    if (QFileInfo::exists(sourceRoute.filePath("tsection.dat")))
        sections.loadRoute(false);
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded)
        return 1;
    const QScopedValueRollback<TDB*> trackDB(Game::trackDB, &database);
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);

    // The geometry copy at random places.
    QElapsedTimer timer;
    timer.start();
    auto geometry = std::make_shared<const Sim::TrackGeometry>(&database);
    const qint64 copyMs = timer.elapsed();
    QVector<int> vectors;
    for (const auto &entry : database.trackNodes)
        if (entry.second != nullptr && entry.second->typ == 1)
            vectors.push_back(entry.first);
    QRandomGenerator random(20261015);
    bool same = true;
    float worst = 0.0f;
    for (int i = 0; i < 10000 && !vectors.isEmpty(); ++i) {
        const int node = vectors[random.bounded(vectors.size())];
        const float length = geometry->length(node);
        same = same && samePose(database, *geometry, node, float(random.generateDouble() * (length + 2.0) - 1.0), &worst);
    }
    check(same, QString("%1 vector nodes copied in %2 ms (%3 KiB); 10000 random poses the same (worst %4)")
          .arg(geometry->nodeCount()).arg(copyMs).arg(geometry->memory() / 1024).arg(worst));

    // Every activity: its input, run to its end.
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();
    const float speedLimit = content->trk != nullptr ? content->trk->speedLimit : 44.4444f;
    int activities = 0, trains = 0, finished = 0, warnings = 0;
    qint64 inputMs = 0, runMs = 0;
    for (int id : content->activityId) {
        Activity *activity = ActLib::Act.value(id);
        if (activity == nullptr || activity->loaded != 1)
            continue;
        ++activities;
        timer.restart();
        auto input = Sim::MakeInput(activity, &database, speedLimit, geometry);
        inputMs += timer.elapsed();
        trains += int(input->trains.size());
        warnings += input->warnings.size();
        if (verbose)
            for (const QString &warning : input->warnings)
                qInfo().noquote() << "[tests:simulation-route]" << activity->name << warning;
        timer.restart();
        Sim::SimulationCore core(input);
        for (int s = 0; s < 24 * 3600; ++s) {
            core.step(1.0);
            bool all = true;
            for (const Sim::TrainState &state : core.trains())
                all = all && state.status == Sim::TrainStatus::Finished;
            if (all)
                break;
        }
        runMs += timer.elapsed();
        for (const Sim::TrainState &state : core.trains())
            finished += state.status == Sim::TrainStatus::Finished ? 1 : 0;
    }
    check(activities > 0 && trains > 0 && finished == trains,
          QString("%1 activities: %2 trains (%3 warnings) made in %4 ms, all finished: %5, run in %6 ms")
          .arg(activities).arg(trains).arg(warnings).arg(inputMs).arg(finished).arg(runMs));
    qInfo().noquote() << QString("simulation-route: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
