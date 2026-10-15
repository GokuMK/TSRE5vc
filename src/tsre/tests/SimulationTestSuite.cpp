#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace {

using PathTrack::Node;

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QString crlf = text;
    crlf.replace("\n", "\r\n");
    file.write("\xff\xfe", 2);
    file.write(reinterpret_cast<const char *>(crlf.utf16()), crlf.size() * 2);
    return true;
}

// Two signal types: a NORMAL home signal of three aspects clearing two heads
// ahead, and a distant one (MSTS's DISTANCE).
const char *testSigCfg = R"(SIMISA@@@@@@@@@@JINX0G0t______

LightTextures ( 1
	LightTex ( "ltex" "SigLight.ace" 0 0 1 1 )
)
LightsTab ( 2
	LightsTabEntry ( "Red Light" colour ( 255 255 40 40 ) )
	LightsTabEntry ( "Green Light" colour ( 255 40 255 40 ) )
)
SignalTypes ( 2
	SignalType ( "Home"
		SignalFnType ( NORMAL )
		SignalLightTex ( "ltex" )
		SignalLights ( 2
			SignalLight ( 0 "Red Light" Position ( 0 0 0 ) Radius ( 0.1 ) )
			SignalLight ( 1 "Green Light" Position ( 0 0.3 0 ) Radius ( 0.1 ) )
		)
		SignalDrawStates ( 3
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Yellow" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 2 "Green" DrawLights ( 1 DrawLight ( 1 ) ) )
		)
		SignalAspects ( 3
			SignalAspect ( STOP "Red" )
			SignalAspect ( APPROACH_1 "Yellow" SpeedKPH ( 40 ) )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( 2 )
	)
	SignalType ( "Dist"
		SignalFnType ( DISTANCE )
		SignalLightTex ( "ltex" )
		SignalLights ( 1
			SignalLight ( 0 "Green Light" Position ( 0 0 0 ) Radius ( 0.1 ) )
		)
		SignalDrawStates ( 2
			SignalDrawState ( 0 "Yellow" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Green" DrawLights ( 1 DrawLight ( 0 ) ) )
		)
		SignalAspects ( 2
			SignalAspect ( APPROACH_1 "Yellow" )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( 1 )
	)
)
SignalShapes ( 0 )
)";
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
    check(moving.status == Sim::TrainStatus::Running && std::fabs(moving.front - (41.0 + 12.5)) < 0.01
              && std::fabs(moving.speed - 5.0f) < 0.01f,
          QString("it starts at its time from rest at 1 m/s²: front at %1").arg(moving.front));
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

    // The driver on a straight 2 km track: a train of an engine and a
    // carriage, 20 m/s at most, 1 m/s² both ways.
    TestTrack::Layout straight;
    straight.end(1, 10);
    straight.track(10, 1, 2, 200);
    straight.end(2, 10);
    TDB *line = &straight.db;
    Game::trackDB = line;
    const PathTrack::Result run = PathTrack::startAt(line, {10, 100.0f});
    const Sim::TrainRoute lineRoute = Sim::BuildRoute(line, run.nodes, PathTrack::legs(line, run.nodes), 31.0);
    const int t0 = 8 * 3600;
    const auto plan = [&](int start) {
        Sim::TrainPlan p;
        p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
        p.length = 40.0f;
        p.maxSpeed = 20.0f;
        Sim::SetRates(p);
        p.startTime = start;
        p.route = lineRoute;
        p.startFront = lineRoute.pathStart + 10.0;
        return p;
    };
    const auto inputWith = [&](const Sim::TrainPlan &p, int start) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = start;
        made->geometry = std::make_shared<const Sim::TrackGeometry>(line);
        made->trains.push_back(p);
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };
    // Runs to the end; the events, by kind and index, with their times.
    const auto runToEnd = [](Sim::SimulationCore &c, double step) {
        std::vector<Sim::TrainEvent> all;
        for (int i = 0; i < 200000 && !c.finished(); ++i) {
            c.step(step);
            for (const Sim::TrainEvent &e : c.takeEvents())
                all.push_back(e);
        }
        for (const Sim::TrainEvent &e : c.takeEvents())
            all.push_back(e);
        return all;
    };
    const auto when = [](const std::vector<Sim::TrainEvent> &all, Sim::TrainEvent::Kind kind, int index) {
        for (const Sim::TrainEvent &e : all)
            if (e.kind == kind && e.index == index)
                return e.time;
        return -1.0;
    };
    check(!lineRoute.parts.empty() && lineRoute.parts[0].length() > 1900.0, "a straight route of about 2 km");
    {
        Sim::TrainPlan free = plan(t0);
        const double distance = lineRoute.parts[0].length() - free.startFront;
        Sim::SimulationCore c(inputWith(free, t0));
        const std::vector<Sim::TrainEvent> all = runToEnd(c, 0.1);
        // 20 s up to 20 m/s (200 m), 20 s braking (200 m), the rest at 20 m/s.
        const double expected = t0 + 40.0 + (distance - 400.0) / 20.0;
        const double end = when(all, Sim::TrainEvent::Finished, -1);
        check(std::fabs(end - expected) < 0.2, QString("a free run's time is the formula's: %1 s, %2 expected")
              .arg(end - t0).arg(expected - t0));
    }
    // Stops: untimed (boarding 20 s), booked later than boarding allows, and
    // reached late (it leaves after its boarding time).
    Sim::TrainPlan withStops = plan(t0);
    const double f0 = withStops.startFront;
    withStops.stops.resize(3);
    withStops.stops[0].front = f0 + 500.0;
    withStops.stops[1].front = f0 + 1000.0;
    withStops.stops[1].arrival = t0 + 150;
    withStops.stops[1].departure = t0 + 200;
    withStops.stops[2].front = f0 + 1500.0;
    withStops.stops[2].arrival = t0 + 200;
    withStops.stops[2].departure = t0 + 210;
    std::vector<Sim::TrainEvent> stopEvents;
    {
        Sim::SimulationCore c(inputWith(withStops, t0));
        stopEvents = runToEnd(c, 0.1);
        // 500 m from rest to rest: 20 s up, 5 s at 20 m/s, 20 s down.
        const double a0 = when(stopEvents, Sim::TrainEvent::Arrived, 0) - t0;
        const double d0 = when(stopEvents, Sim::TrainEvent::Departed, 0) - t0;
        const double d1 = when(stopEvents, Sim::TrainEvent::Departed, 1) - t0;
        const double a2 = when(stopEvents, Sim::TrainEvent::Arrived, 2) - t0;
        const double d2 = when(stopEvents, Sim::TrainEvent::Departed, 2) - t0;
        check(std::fabs(a0 - 45.0) < 0.2 && std::fabs(d0 - a0 - 20.0) < 0.01,
              QString("an untimed stop: arrives at %1 s, stands its 20 s boarding").arg(a0));
        check(std::fabs(d1 - 200.0) < 0.01, QString("a booked departure later than boarding: leaves at %1 s").arg(d1));
        check(std::fabs(d2 - a2 - 20.0) < 0.01 && a2 > 210.0,
              QString("reached late (%1 s, booked 210 s): leaves after boarding at %2 s").arg(a2).arg(d2));
    }
    {
        // The same with 5 s steps: the same events at the same times.
        Sim::SimulationCore c(inputWith(withStops, t0));
        const std::vector<Sim::TrainEvent> coarse = runToEnd(c, 5.0);
        bool same = coarse.size() == stopEvents.size();
        for (size_t i = 0; same && i < coarse.size(); ++i)
            same = coarse[i].kind == stopEvents[i].kind && coarse[i].index == stopEvents[i].index
                && std::fabs(coarse[i].time - stopEvents[i].time) < 1e-6;
        check(same, "5 s steps give the 0.1 s steps' events: no stop skipped");
        Sim::SimulationCore again(inputWith(withStops, t0));
        const std::vector<Sim::TrainEvent> repeat = runToEnd(again, 0.1);
        bool identical = repeat.size() == stopEvents.size();
        for (size_t i = 0; identical && i < repeat.size(); ++i)
            identical = repeat[i].time == stopEvents[i].time && repeat[i].kind == stopEvents[i].kind;
        check(identical, "the same input gives the same events");
    }
    {
        // Waiting points: 45 s; a horn (no stop); until 08:05; uncouple
        // keeping the first 3 cars, waiting 5 s.
        Sim::TrainPlan waits = plan(t0);
        waits.waits = {Sim::WaitPlan{0, f0 + 300.0, 45}, Sim::WaitPlan{0, f0 + 400.0, 60013},
                       Sim::WaitPlan{0, f0 + 600.0, 30805}, Sim::WaitPlan{0, f0 + 800.0, 40305}};
        Sim::SimulationCore c(inputWith(waits, t0));
        const std::vector<Sim::TrainEvent> all = runToEnd(c, 0.1);
        const double w0 = when(all, Sim::TrainEvent::Waiting, 0);
        const double horn = when(all, Sim::TrainEvent::Horn, 1);
        const double w2 = when(all, Sim::TrainEvent::Waiting, 2);
        const double w3 = when(all, Sim::TrainEvent::Waiting, 3);
        check(w0 > 0 && horn > w0 + 45.0 && when(all, Sim::TrainEvent::Waiting, 1) < 0 && w2 > horn && w3 > t0 + 300.0,
              QString("waits: 45 s at %1, horn passing at %2, until 08:05 from %3, on at %4")
              .arg(w0 - t0).arg(horn - t0).arg(w2 - t0).arg(w3 - t0));
    }
    {
        // The pre-run: a train that started two minutes before the activity
        // stands where it would have run to.
        Sim::TrainPlan early = plan(t0 - 120);
        Sim::SimulationCore pre(inputWith(early, t0));
        Sim::SimulationCore from(inputWith(early, t0 - 120));
        from.step(120.0);
        check(std::fabs(pre.trains()[0].front - from.trains()[0].front) < 1e-6
                  && pre.trains()[0].speed == from.trains()[0].speed && pre.time() == t0,
              QString("the pre-run: at the activity's start where it would be (%1 m)").arg(pre.trains()[0].front));
    }
    {
        // Starting at its first station: it stands until its departure.
        Sim::TrainPlan atStation = plan(t0);
        atStation.stops.resize(1);
        atStation.stops[0].front = atStation.startFront + 5.0;
        atStation.stops[0].departure = t0 + 60;
        Sim::SimulationCore c(inputWith(atStation, t0));
        c.step(30.0);
        const bool standing = c.trains()[0].status == Sim::TrainStatus::Stopped
                && c.trains()[0].front == atStation.startFront;
        c.step(31.0);
        check(standing && c.trains()[0].status == Sim::TrainStatus::Running,
              "a train starting at its first station stands there until its departure");
    }

    // The track network (task 02, phase 1): a passing loop with signals, a
    // distant signal, speed posts and a diamond crossing.
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        check(writeUtf16(config, testSigCfg), "write a sigcfg.dat");
        const SigCfg signalConfig(config);
        TestTrack::Layout loop;
        TestTrack::passingLoop(loop);
        loop.end(21, 20);
        loop.track(20, 21, 22, 3);
        loop.end(22, 20);
        TestTrack::signalHead(loop, 10, 501, 80.0f, true, "Home", 2, 1);
        TestTrack::signalHead(loop, 10, 502, 80.0f, true, "Home", 2, 2);
        TestTrack::signalHead(loop, 13, 503, 20.0f, false, "Home");
        TestTrack::signalHead(loop, 11, 504, 100.0f, true, "Dist");
        TestTrack::speedLimit(loop, 11, 601, 50.0f, true, 40.0f);
        TestTrack::speedLimit(loop, 11, 602, 150.0f, false, 60.0f);
        TestTrack::crossing(loop, 11, 701, 120.0f, 20, 702, 5.0f);
        TDB *net = &loop.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, net);
        const Sim::TrackGeometry loopGeometry(net);
        const Sim::TrackNetwork network(net, loopGeometry, &signalConfig, false);
        const auto &kinds = network.kinds();
        int home = -1, dist = -1;
        for (size_t k = 0; k < kinds.size(); ++k) {
            if (kinds[k].name == "home") home = int(k);
            if (kinds[k].name == "dist") dist = int(k);
        }
        check(home >= 0 && dist >= 0 && kinds[size_t(home)].function == Sim::SignalFunction::Normal
                  && kinds[size_t(home)].clearAhead == 2 && !kinds[size_t(home)].countsSignals
                  && kinds[size_t(dist)].function == Sim::SignalFunction::Distant
                  && kinds[size_t(home)].aspects.size() == 3 && kinds[size_t(home)].aspects[0].aspect == 0
                  && std::fabs(kinds[size_t(home)].aspects[1].speed - 40.0f / 3.6f) < 0.01f,
              "signal types: functions (DISTANCE is distant), clearing ahead in heads, aspects with speeds");
        const Sim::TrackNetwork openRails(net, loopGeometry, &signalConfig, true);
        check(openRails.kinds()[size_t(home)].countsSignals, "OpenRails/sigcfg.dat counts signals");
        const auto [first10, count10] = network.nodeSections(10);
        const auto [first11, count11] = network.nodeSections(11);
        const auto [first13, count13] = network.nodeSections(13);
        const auto &sections = network.sections();
        check(count10 == 2 && std::fabs(sections[size_t(first10)].to - 80.0f) < 0.01f && count11 == 1 && count13 == 2
                  && std::fabs(sections[size_t(first13)].to - 20.0f) < 0.01f,
              "tracks cut at NORMAL heads either way, not at the distant one");
        const int j2 = network.junctionSection(2), j3 = network.junctionSection(3);
        const auto [first12, count12] = network.nodeSections(12);
        check(j2 >= 0 && j3 >= 0 && sections[size_t(j2)].kind == Sim::TrackSection::Junction
                  && sections[size_t(j2)].link[0] == first10 + 1 && sections[size_t(j2)].link[1] == first11
                  && sections[size_t(j2)].link[2] == first12 + count12 - 1 && sections[size_t(j2)].mainPin == 1
                  && sections[size_t(first10 + 1)].link[1] == j2 && sections[size_t(first10)].link[0] == -1
                  && sections[size_t(first12)].link[0] == j3,
              "junctions linked to the sections at their pins, the main route known");
        const int a = network.headOfItem(501), b = network.headOfItem(502), c = network.headOfItem(503);
        const auto &heads = network.heads();
        check(a >= 0 && b >= 0 && c >= 0 && heads[size_t(a)].signal == heads[size_t(b)].signal
                  && heads[size_t(a)].signal != heads[size_t(c)].signal && heads[size_t(a)].junction == 2
                  && heads[size_t(a)].junctionPin == 1 && heads[size_t(b)].junctionPin == 2 && !heads[size_t(c)].forward
                  && network.signalGroups()[size_t(heads[size_t(a)].signal)].normal,
              "heads at one place one way are one signal; junction links and directions");
        const auto &posts = network.speedPosts();
        check(posts.size() == 2 && posts[0].forward != posts[1].forward
                  && std::fabs((posts[0].item == 601 ? posts[0] : posts[1]).speed - 40.0f / 3.6f) < 0.01f
                  && (posts[0].item == 601 ? posts[0] : posts[1]).forward,
              "speed posts face the way their angle says");
        // Through the loop's main track and back.
        const PathTrack::Result there = PathTrack::endAt(net, PathTrack::startAt(net, {10, 10.0f}).nodes, {13, 80.0f});
        const Sim::TrainRoute out = Sim::BuildRoute(net, there.nodes, PathTrack::legs(net, there.nodes), 0.0);
        const Sim::Projection go = out.parts.empty() ? Sim::Projection() : network.project(out.parts[0]);
        QStringList passed;
        bool joined = true;
        double reached = 0.0;
        for (size_t i = 0; i < go.sections.size(); ++i) {
            const Sim::ProjectedSection &p = go.sections[i];
            const Sim::TrackSection &section = sections[size_t(p.section)];
            passed << (section.kind == Sim::TrackSection::Junction ? QString("J%1:%2>%3").arg(section.node).arg(p.pinIn).arg(p.pinOut)
                       : section.kind == Sim::TrackSection::Crossing ? QString("X")
                       : QString("%1:%2-%3").arg(section.node).arg(section.from).arg(section.to));
            // Track and junctions follow on; a crossing lies within a track.
            if (section.kind == Sim::TrackSection::Crossing)
                continue;
            if (std::fabs(p.enter - reached) > 0.01)
                joined = false;
            reached = p.leave;
        }
        const QString way = passed.join(" ");
        check(way == "10:0-80 10:80-100 J2:0>1 11:0-200 X J3:1>0 13:0-20 13:20-100" && joined
                  && std::fabs(go.sections.front().enter) < 0.01 && std::fabs(go.sections.back().leave - out.parts[0].length()) < 0.01,
              "a route projected: sections in order, junction pins, the crossing, distances joined: " + way);
        QStringList facing;
        for (const Sim::ProjectedItem &h : go.heads)
            facing << QString::number(heads[size_t(h.index)].item);
        QStringList limits;
        for (const Sim::ProjectedItem &p : go.speedPosts)
            limits << QString::number(posts[size_t(p.index)].item);
        check(facing.join(",") == "501,502,504" && limits.join(",") == "601"
                  && std::fabs(go.heads.front().at - 70.0) < 0.01,
              "the heads and speed posts facing it, at their route distances: " + facing.join(",") + " / " + limits.join(","));
        const PathTrack::Result back = PathTrack::endAt(net, PathTrack::startAt(net, {13, 80.0f}).nodes, {10, 10.0f});
        const Sim::TrainRoute home2 = Sim::BuildRoute(net, back.nodes, PathTrack::legs(net, back.nodes), 0.0);
        const Sim::Projection come = home2.parts.empty() ? Sim::Projection() : network.project(home2.parts[0]);
        facing.clear();
        for (const Sim::ProjectedItem &h : come.heads)
            facing << QString::number(heads[size_t(h.index)].item);
        limits.clear();
        for (const Sim::ProjectedItem &p : come.speedPosts)
            limits << QString::number(posts[size_t(p.index)].item);
        check(facing.join(",") == "503" && limits.join(",") == "602",
              "the other way the other heads and posts: " + facing.join(",") + " / " + limits.join(","));
        check(network.sectionAt(10, 80.0f, true) == first10 + 1 && network.sectionAt(10, 80.0f, false) == first10
                  && network.sectionAt(10, 30.0f, true) == first10,
              "the section at a boundary is the one a train going that way enters");
    }

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

    // The track network (task 02): sections covering every vector node once,
    // NORMAL heads on section boundaries, heads facing the junction they are
    // linked to, speed posts along the track.
    bool openRailsConfig = false;
    const std::shared_ptr<const SigCfg> signalConfig = Sim::SimulationSigCfg(&database, &openRailsConfig);
    timer.restart();
    auto network = std::make_shared<const Sim::TrackNetwork>(&database, *geometry, signalConfig.get(), openRailsConfig);
    const qint64 networkMs = timer.elapsed();
    {
        int covered = 0, nodesWithTrack = 0;
        for (int node : vectors) {
            const auto [first, count] = network->nodeSections(node);
            if (geometry->length(node) <= 0.0f)
                continue;
            ++nodesWithTrack;
            bool whole = count > 0 && std::fabs(network->sections()[size_t(first)].from) < 1e-3f
                    && std::fabs(network->sections()[size_t(first + count - 1)].to - geometry->length(node)) < 1e-3f;
            for (int k = 1; k < count && whole; ++k)
                whole = network->sections()[size_t(first + k)].from == network->sections()[size_t(first + k - 1)].to;
            covered += whole ? 1 : 0;
        }
        int normal = 0, onBoundary = 0, linked = 0, linkedAhead = 0, linkedBehind = 0;
        for (const Sim::SignalHead &head : network->heads()) {
            const bool isNormal = head.kind >= 0
                    && network->kinds()[size_t(head.kind)].function == Sim::SignalFunction::Normal;
            if (isNormal) {
                ++normal;
                const auto [first, count] = network->nodeSections(head.node);
                bool boundary = head.distance <= 0.05f || head.distance >= geometry->length(head.node) - 0.05f;
                for (int k = 0; k < count && !boundary; ++k)
                    boundary = std::fabs(network->sections()[size_t(first + k)].to - head.distance) < 0.06f;
                onBoundary += boundary ? 1 : 0;
            }
            if (head.junction > 0) {
                ++linked;
                const int junction = network->junctionSection(head.junction);
                // Within a few junctions the way the head faces (any branch),
                // or the other way.
                auto within = [&](bool forward) {
                    std::vector<std::pair<int, int>> reach = {{network->endSection(head.node, forward ? 1 : 0), 0}};
                    for (size_t r = 0; r < reach.size(); ++r) {
                        const auto [at, depth] = reach[r];
                        if (at < 0)
                            continue;
                        if (at == junction)
                            return true;
                        const Sim::TrackSection &j = network->sections()[size_t(at)];
                        if (j.kind != Sim::TrackSection::Junction || depth >= 3)
                            continue;
                        for (int pin = 0; pin < 3; ++pin) {
                            const int track = j.link[pin];
                            if (track < 0)
                                continue;
                            const Sim::TrackSection &t = network->sections()[size_t(track)];
                            // The track's far end from this junction.
                            const auto [first, count] = network->nodeSections(t.node);
                            const int far = network->endSection(t.node, 0) == at && track == first ? 1 : 0;
                            reach.push_back({network->endSection(t.node, far), depth + 1});
                        }
                    }
                    return false;
                };
                linkedAhead += within(head.forward) ? 1 : 0;
                linkedBehind += network->endSection(head.node, head.forward ? 0 : 1) == junction ? 1 : 0;
            }
        }
        int posts = 0, along = 0;
        for (const auto &entry : database.trackNodes) {
            const TRnode *n = entry.second;
            if (n == nullptr || n->typ != 1)
                continue;
            for (int i = 0; i < n->iTri; ++i) {
                auto found = database.trackItems.find(n->trItemRef[i]);
                TRitem *item = found != database.trackItems.end() ? found->second : nullptr;
                if (item == nullptr || item->type != "speedpostitem" || item->speedpostTrItemData == nullptr
                        || item->getSpeedpostType() == TRitem::MILEPOST)
                    continue;
                Sim::TrackPose pose;
                if (!geometry->pose(entry.first, item->getTrackPosition(), pose))
                    continue;
                ++posts;
                double d = std::fmod(std::fabs(double(item->getSpeedpostRot()) - (pose.yaw - M_PI / 2.0)), 2.0 * M_PI);
                d = std::min(d, 2.0 * M_PI - d);
                along += std::min(d, M_PI - d) < 0.35 ? 1 : 0;
            }
        }
        check(covered == nodesWithTrack && onBoundary == normal && linkedAhead == linked,
              QString("track network in %1 ms (%2 KiB, %3 sigcfg.dat): %4 sections cover %5 of %6 nodes; %7 heads "
                      "(%8 NORMAL, %9 on section boundaries) in %10 signals; %11 linked to junctions, %12 ahead of them "
                      "(%13 behind); %14 types, %15 warnings")
                  .arg(networkMs).arg(network->memory() / 1024).arg(openRailsConfig ? "OpenRails" : "root")
                  .arg(network->sections().size()).arg(covered).arg(nodesWithTrack).arg(network->heads().size())
                  .arg(normal).arg(onBoundary).arg(network->signalGroups().size()).arg(linked).arg(linkedAhead)
                  .arg(linkedBehind).arg(network->kinds().size()).arg(network->warnings.size()));
        check(posts == 0 || along * 10 >= posts * 9,
              QString("%1 speed posts, %2 at the track's angle or against it").arg(posts).arg(along));
        // The way posts face: a warning comes before the limit of its speed,
        // in the direction both face (same node, within 3 km).
        int warningsAhead = 0, warningsBehind = 0;
        for (const Sim::SpeedPost &warning : network->speedPosts()) {
            if (warning.kind != Sim::SpeedPost::Warning)
                continue;
            double nearAhead = 1e9, nearBehind = 1e9;
            for (const Sim::SpeedPost &limit : network->speedPosts()) {
                if (limit.kind != Sim::SpeedPost::Limit || limit.node != warning.node
                        || std::fabs(limit.speed - warning.speed) > 0.1f || limit.forward != warning.forward)
                    continue;
                const double along = (limit.distance - warning.distance) * (warning.forward ? 1.0 : -1.0);
                if (along > 0.0 && along < nearAhead)
                    nearAhead = along;
                if (along < 0.0 && -along < nearBehind)
                    nearBehind = -along;
            }
            if (nearAhead < 3000.0 || nearBehind < 3000.0)
                (nearAhead <= nearBehind ? warningsAhead : warningsBehind) += 1;
        }
        check(warningsAhead >= warningsBehind,
              QString("speed posts face their way: %1 warnings before their limit, %2 after it")
                  .arg(warningsAhead).arg(warningsBehind));
        if (verbose)
            for (const QString &warning : network->warnings.mid(0, 20))
                qInfo().noquote() << "[tests:simulation-route] network:" << warning;
    }

    // Every activity: its input, run to its end.
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();
    const float speedLimit = content->trk != nullptr ? content->trk->speedLimit : 44.4444f;
    int activities = 0, trains = 0, finished = 0, warnings = 0, stopsMade = 0;
    int projectedParts = 0, projectedWhole = 0, looseConsists = 0, placedConsists = 0;
    Activity *busiest = nullptr;
    size_t busiestTrains = 0;
    std::vector<double> lateness;
    double simulated = 0.0;
    qint64 inputMs = 0, runMs = 0;
    for (int id : content->activityId) {
        Activity *activity = ActLib::Act.value(id);
        if (activity == nullptr || activity->loaded != 1)
            continue;
        ++activities;
        timer.restart();
        auto input = Sim::MakeInput(activity, &database, speedLimit, geometry, network);
        inputMs += timer.elapsed();
        // Each route part projected onto the network, end to end.
        for (const Sim::TrainPlan &plan : input->trains)
            for (size_t part = 0; part < plan.projection.size(); ++part) {
                const Sim::Projection &way = plan.projection[part];
                double reached = 0.0;
                bool whole = !way.sections.empty();
                for (const Sim::ProjectedSection &pass : way.sections) {
                    if (network->sections()[size_t(pass.section)].kind == Sim::TrackSection::Crossing)
                        continue;
                    whole = whole && std::fabs(pass.enter - reached) < 0.01;
                    reached = pass.leave;
                }
                whole = whole && std::fabs(reached - plan.route.parts[part].length()) < 0.01;
                ++projectedParts;
                projectedWhole += whole ? 1 : 0;
                if (!whole && verbose)
                    qInfo().noquote() << "[tests:simulation-route] not projected whole:" << activity->name << plan.service
                                      << "part" << part;
            }
        for (const ActivityObject *object : activity->activityObjects)
            looseConsists += object != nullptr && object->objectTypeId == ActivityObject::WAGONLIST
                    && object->con != nullptr ? 1 : 0;
        placedConsists += int(input->staticConsists.size());
        trains += int(input->trains.size());
        warnings += input->warnings.size();
        if (input->trains.size() > busiestTrains) {
            busiestTrains = input->trains.size();
            busiest = activity;
        }
        if (verbose)
            for (const QString &warning : input->warnings)
                qInfo().noquote() << "[tests:simulation-route]" << activity->name << warning;
        timer.restart();
        Sim::SimulationCore core(input);
        for (int s = 0; s < 24 * 3600 && !core.finished(); ++s) {
            core.step(1.0);
            for (const Sim::TrainEvent &event : core.takeEvents()) {
                if (event.kind != Sim::TrainEvent::Arrived)
                    continue;
                ++stopsMade;
                // Against the booked arrival, where the stop has one (the
                // first stop is where a train starts).
                const Sim::StopPlan &stop = input->trains[size_t(event.train)].stops[size_t(event.index)];
                // Timetables not calculated in MSAE book every stop at the
                // train's start time: those are left out.
                if (stop.arrival > input->trains[size_t(event.train)].startTime + 1 && event.index > 0) {
                    const double late = event.time - stop.arrival;
                    lateness.push_back(late);
                }
                if (qEnvironmentVariableIsSet("TSRE_SIM_TRACE")) {
                    const Sim::TrainPlan &p = input->trains[size_t(event.train)];
                    qInfo().noquote() << "[sim-trace]" << activity->name << p.service << p.serviceTime << "stop" << event.index
                                      << stop.platform << "arr" << stop.arrival << "dep" << stop.departure << "sim" << event.time
                                      << "late" << (stop.arrival >= 0 ? event.time - stop.arrival : 0) << "boarding" << stop.boarding
                                      << "vmax" << p.maxSpeed << "eff" << stop.efficiency << "lim" << input->routeSpeedLimit;
                }
            }
        }
        runMs += timer.elapsed();
        simulated += core.time() - input->startTime;
        for (const Sim::TrainState &state : core.trains())
            finished += state.status == Sim::TrainStatus::Finished || state.stoppedAt == Sim::StopKind::End ? 1 : 0;
    }
    check(activities > 0 && trains > 0 && finished == trains,
          QString("%1 activities: %2 trains (%3 warnings) made in %4 ms; all finished: %5, %6 station stops, "
                  "%7 h simulated in %8 ms")
          .arg(activities).arg(trains).arg(warnings).arg(inputMs).arg(finished).arg(stopsMade)
          .arg(simulated / 3600.0, 0, 'f', 1).arg(runMs));
    check(projectedWhole == projectedParts && placedConsists == looseConsists,
          QString("%1 route parts projected onto the network end to end: %2; loose consists on the track: %3 of %4")
              .arg(projectedParts).arg(projectedWhole).arg(placedConsists).arg(looseConsists));
    if (!lateness.empty()) {
        std::sort(lateness.begin(), lateness.end());
        double sum = 0.0;
        for (double late : lateness)
            sum += late;
        qInfo().noquote() << QString("[tests:simulation-route] arrivals against the timetables (%1 timed stops): "
                                     "mean %2 s, median %3 s, from %4 to %5 s")
                             .arg(lateness.size()).arg(sum / lateness.size(), 0, 'f', 0)
                             .arg(lateness[lateness.size() / 2], 0, 'f', 0)
                             .arg(lateness.front(), 0, 'f', 0).arg(lateness.back(), 0, 'f', 0);
    }

    // Measured (phase 4) on the activity with the most trains: its input
    // made as Play makes it (its own geometry copy) and its memory; the
    // core's cost in 0.1 s steps per train on the track; a frame where most
    // trains are on the track; a hundred trains at once.
    if (busiest != nullptr) {
        timer.restart();
        const std::shared_ptr<const Sim::SimulationInput> own = Sim::MakeInput(busiest, &database, speedLimit);
        const qint64 ownMs = timer.elapsed();
        Sim::SimulationCore core(own);
        QElapsedTimer stepping;
        qint64 steppingNs = 0, framesNs = 0;
        long long trainSteps = 0;
        int frames = 0, mostOnTrack = 0, mostVehicles = 0;
        while (!core.finished() && core.time() < own->startTime + 24 * 3600) {
            int onTrack = 0;
            for (const Sim::TrainState &state : core.trains())
                onTrack += state.status == Sim::TrainStatus::Running || state.status == Sim::TrainStatus::Stopped ? 1 : 0;
            stepping.start();
            core.step(Sim::SimulationCore::MaxStep);
            steppingNs += stepping.nsecsElapsed();
            trainSteps += onTrack;
            core.takeEvents();
            if (onTrack > mostOnTrack) {
                mostOnTrack = onTrack;
                stepping.start();
                const std::shared_ptr<Sim::SimulationFrame> frame = Sim::MakeFrame(core);
                framesNs = stepping.nsecsElapsed();
                frames = 1;
                mostVehicles = 0;
                for (const Sim::TrainFrame &train : frame->trains)
                    mostVehicles += int(train.fronts.size());
            }
        }
        qInfo().noquote() << QString("[tests:simulation-route] measured on %1 (%2 trains): input %3 ms with its "
                                     "geometry copy, %4 KiB (plans %5 KiB); a 0.1 s step %6 µs, %7 ns per train on the "
                                     "track; a frame with %8 trains on the track (%9 vehicles) %10 µs")
                             .arg(busiest->name).arg(own->trains.size()).arg(ownMs).arg(own->memory() / 1024)
                             .arg(own->memory(false) / 1024)
                             .arg(core.steps() > 0 ? steppingNs / 1000.0 / double(core.steps()) : 0.0, 0, 'f', 1)
                             .arg(trainSteps > 0 ? double(steppingNs) / double(trainSteps) : 0.0, 0, 'f', 0)
                             .arg(mostOnTrack).arg(mostVehicles).arg(frames > 0 ? framesNs / 1000.0 : 0.0, 0, 'f', 0);
        // A hundred trains running at once: the activity's, started together
        // and repeated.
        auto hundred = std::make_shared<Sim::SimulationInput>(*own);
        hundred->trains.clear();
        for (size_t i = 0; hundred->trains.size() < 100 && !own->trains.empty(); ++i) {
            Sim::TrainPlan plan = own->trains[i % own->trains.size()];
            plan.id = int(hundred->trains.size());
            plan.startTime = own->startTime;
            hundred->trains.push_back(plan);
        }
        Sim::SimulationCore many(hundred);
        timer.restart();
        many.step(600.0);
        const double rate = 600.0 / std::max(1e-6, timer.nsecsElapsed() * 1e-9);
        many.takeEvents();
        stepping.start();
        const std::shared_ptr<Sim::SimulationFrame> frame = Sim::MakeFrame(many);
        const qint64 frameNs = stepping.nsecsElapsed();
        int running = 0, vehicles = 0;
        for (const Sim::TrainFrame &train : frame->trains) {
            running += train.state.status == Sim::TrainStatus::Running ? 1 : 0;
            vehicles += int(train.fronts.size());
        }
        check(rate > 60.0, QString("100 trains at once: %1 simulated seconds a second in 0.1 s steps; a frame of %2 "
                                   "vehicles (%3 trains running) %4 µs")
                               .arg(rate, 0, 'f', 0).arg(vehicles).arg(running).arg(frameNs / 1000.0, 0, 'f', 0));
    }
    qInfo().noquote() << QString("simulation-route: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
