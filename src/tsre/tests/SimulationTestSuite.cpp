#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <set>
#include <queue>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTime>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <algorithm>
#include <map>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SignalScript.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sim/TimetableCalculation.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/sim/WorkPlanner.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalType.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/OrTimetable.h>
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

// Signal types: a NORMAL home signal of three aspects clearing two ahead, a
// distant one (MSTS's DISTANCE), and two NORMAL ones clearing 0 and -1
// ahead (Open Rails' own values).
const char *testSigCfg = R"(SIMISA@@@@@@@@@@JINX0G0t______

LightTextures ( 1
	LightTex ( "ltex" "SigLight.ace" 0 0 1 1 )
)
LightsTab ( 2
	LightsTabEntry ( "Red Light" colour ( 255 255 40 40 ) )
	LightsTabEntry ( "Green Light" colour ( 255 40 255 40 ) )
)
SignalTypes ( 4
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
	SignalType ( "Zero"
		SignalFnType ( NORMAL )
		SignalLightTex ( "ltex" )
		SignalLights ( 1
			SignalLight ( 0 "Red Light" Position ( 0 0 0 ) Radius ( 0.1 ) )
		)
		SignalDrawStates ( 2
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Green" DrawLights ( 1 DrawLight ( 0 ) ) )
		)
		SignalAspects ( 2
			SignalAspect ( STOP "Red" )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( 0 )
	)
	SignalType ( "Skip"
		SignalFnType ( NORMAL )
		SignalLightTex ( "ltex" )
		SignalLights ( 1
			SignalLight ( 0 "Red Light" Position ( 0 0 0 ) Radius ( 0.1 ) )
		)
		SignalDrawStates ( 2
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Green" DrawLights ( 1 DrawLight ( 0 ) ) )
		)
		SignalAspects ( 2
			SignalAspect ( STOP "Red" )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( -1 )
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

    // Signal scripts (task 05): read and run as MSTS's grammar defines them.
    {
        const QString text = R"(// heading comment
SCRIPT First
/* a block
   comment */
	extern float	block_state ();
	extern float	next_sig_lr ();
	extern float	state, draw_state, enabled;
	float			a, b;
	a = b = 2 + 3 * 4;			// C's precedence: 14
	if (!enabled || block_state() !=# BLOCK_CLEAR)
		state = SIGASP_STOP;
	else if (next_sig_lr (SIGFN_NORMAL) ==# SIGASP_STOP)
	{
		state = SIGASP_APPROACH_1;
	}
	else
		state = SIGASP_CLEAR_2;
	draw_state = a / 4 + 7 / 0;			// 3.5 + 0: 3 kept
	if (a / 4 == 3.5) draw_state = draw_state + 100;	// floating point: yes
	if (1 || 0 && 0) draw_state = draw_state + 10;		// 1 || (0 && 0): yes
	if (! b ==# 5) draw_state = draw_state + 1000;		// (!b) == 5: no
	if (a /# 4 ==# 3 && #3.7 == 3) draw_state += 1;		// integer parts: yes
REM SCRIPT Skipped
	for (a = 0; a < 3; a++) b = b + 1;
SCRIPT Second
	extern float state;
	float kept;
	kept = kept + 1;
	state = state + kept;
	return;
	state = 99;
SCRIPT Third
	extern float state, draw_state;
	float i, n;
	for (i = 0; i < 10; i++)
	{
		if (i ==# 2) continue;
		if (i >= 5) break;
		n += i;					// 0 + 1 + 3 + 4 = 8
	}
	do n++; while (n < 10);			// 10
	state = n > 9 ? n * 10 + i : -1;	// 105
	draw_state = 0;
	while (1) draw_state++;			// stopped
SCRIPT Broken
	extern float approach_control_speed ();
	state = approach_control_speed (100, 200) + nothing_known + mystery (1);
	goto done;
	break;
	enabled = 1;
	state = next_sig_lr ()
)";
        QStringList fileProblems;
        const std::vector<Sim::SignalScript> scripts = Sim::ReadSignalScripts(text, "test.dat", &fileProblems);
        const auto byName = [&](const QString &name) -> const Sim::SignalScript * {
            for (const Sim::SignalScript &script : scripts)
                if (script.name == name)
                    return &script;
            return nullptr;
        };
        const Sim::SignalScript *first = byName("first"), *second = byName("second"), *third = byName("third");
        const Sim::SignalScript *broken = byName("broken");
        check(scripts.size() == 4 && first && second && third && broken && byName("skipped") == nullptr
                  && fileProblems.isEmpty() && first->usable() && second->usable() && third->usable()
                  && first->locals == 2,
              "signal scripts read: sections, REM SCRIPT skipped, comments, declarations");
        const QString brokenText = broken != nullptr ? broken->problems.join(" | ") : QString();
        check(broken && !broken->usable() && broken->openRails == QStringList{"approach_control_speed"}
                  && brokenText.contains("unknown name nothing_known") && brokenText.contains("unknown function mystery")
                  && brokenText.contains("goto is not in the language") && brokenText.contains("break outside a loop")
                  && brokenText.contains("enabled cannot be set")
                  && brokenText.contains("next_sig_lr takes 1 arguments") && brokenText.contains("; expected")
                  && brokenText.contains("test.dat line "),
              QString("signal script problems with their lines: %1").arg(brokenText));
        // A host answering fixed values.
        struct Host : Sim::SignalScriptHost {
            int on = 1, block = Sim::BlockClear, next = 0;
            int enabled() override { return on; }
            int blockState() override { return block; }
            int routeSet() override { return 1; }
            int nextSignal(int, bool, int) override { return next; }
            int thisSignal(int, bool) override { return 0; }
            int oppositeSignal(int, bool) override { return 0; }
            int distantMulti(int, int) override { return 0; }
            int feature(int) override { return 0; }
            int drawStateOf(int aspect) override { return aspect; }
        } host;
        int state = -1, draw = -1;
        if (first != nullptr)
            Sim::RunSignalScript(*first, host, state, draw);
        const int clearState = state, clearDraw = draw;
        host.next = 0;
        host.block = Sim::BlockOccupied;
        if (first != nullptr)
            Sim::RunSignalScript(*first, host, state, draw);
        const int occupied = state;
        check(clearState == 3 && clearDraw == 114 && occupied == 0,
              QString("signal scripts run: APPROACH_1 before a signal at STOP (%1), STOP with the block occupied (%2); "
                      "C's precedence, floating point, division by zero 0, integer parts, a negation of its operand "
                      "(draw %3)")
                  .arg(clearState).arg(occupied).arg(clearDraw));
        host.block = Sim::BlockClear;
        host.next = 7;
        if (first != nullptr)
            Sim::RunSignalScript(*first, host, state, draw);
        const int clear = state;
        host.next = 0;
        if (first != nullptr)
            Sim::RunSignalScript(*first, host, state, draw);
        check(clear == 7 && state == 3, QString("the block clear: CLEAR_2 before a clear signal (%1), APPROACH_1 "
                                                "before one at STOP (%2)").arg(clear).arg(state));
        int kept = 5, unused = 0;
        if (second != nullptr) {
            Sim::RunSignalScript(*second, host, kept, unused);
            Sim::RunSignalScript(*second, host, kept, unused);
        }
        check(kept == 7, QString("locals cleared for each run, state kept between runs, return stops (%1)").arg(kept));
        int loopState = 0, loopDraw = 0;
        if (third != nullptr)
            Sim::RunSignalScript(*third, host, loopState, loopDraw);
        check(loopState == 105 && loopDraw > Sim::MaxScriptSteps / 2 && loopDraw <= Sim::MaxScriptSteps + 1,
              QString("for, continue, break, +=, do while, ++, ?: (%1); an endless loop stopped (%2 rounds)")
                  .arg(loopState).arg(loopDraw));
    }

    // An engine's file: mass, force, power, speed, resistance, passengers.
    {
        QTemporaryDir folder;
        writeUtf16(folder.path() + "/Test.eng", R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( Test
	Type ( Engine )
	Mass ( "117.57t #368000lbs" )
	ORTSDavis_A ( 1000N )
	ORTSDavis_B ( 20N/m/s )
	Friction (
		150N/m/s		1		-1mph		0		1
		5.1N/rad/s		1		-1rad/s		0		1
	)
	PassengerCapacity ( 60 )
)
Engine ( Test
	Type ( Electric )
	MaxPower ( 1400kW )
	MaxForce ( 230kN#est )
	MaxVelocity ( 74.56mph )
)
)");
        Eng eng(folder.path(), "Test.eng");
        check(std::fabs(eng.massKg - 117570.0) < 0.01 && std::fabs(eng.forceN - 230000.0) < 0.01
                  && std::fabs(eng.powerW - 1400000.0) < 0.01 && std::fabs(eng.maxSpeedMps - 74.56 * 0.44704) < 1e-4
                  && eng.hasDavis && eng.davis[0] == 1000.0 && eng.davis[1] == 20.0 && eng.davis[2] == 0.0
                  && eng.hasFriction && eng.friction[0] == 150.0 && std::fabs(eng.friction[2] + 0.44704) < 1e-6
                  && eng.passengerCapacity,
              QString("an engine's file in SI: %1 kg, %2 N, %3 W, %4 m/s, Davis and Friction read")
                  .arg(eng.massKg).arg(eng.forceN).arg(eng.powerW).arg(eng.maxSpeedMps));
    }

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
        // Departures rounded up to the input's step (task 04): ready at 65 s
        // (boarding from 45 s), it leaves at 90 s on a 30 s step, 75 s on a
        // 15 s one; booked 200 s, at 210 s on both.
        double first[2] = {0.0, 0.0}, booked[2] = {0.0, 0.0};
        int k = 0;
        for (int step : {30, 15}) {
            auto rounded = std::make_shared<Sim::SimulationInput>(*inputWith(withStops, t0));
            rounded->departureStep = step;
            Sim::SimulationCore c(rounded);
            const std::vector<Sim::TrainEvent> all = runToEnd(c, 0.1);
            first[k] = when(all, Sim::TrainEvent::Departed, 0) - t0;
            booked[k++] = when(all, Sim::TrainEvent::Departed, 1) - t0;
        }
        check(std::fabs(first[0] - 90.0) < 1e-6 && std::fabs(first[1] - 75.0) < 1e-6 && std::fabs(booked[0] - 210.0) < 1e-6
                  && std::fabs(booked[1] - 210.0) < 1e-6,
              QString("departures rounded up to the step: %1 and %2 s, booked %3 and %4 s")
                  .arg(first[0]).arg(first[1]).arg(booked[0]).arg(booked[1]));
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
    {
        // Physics driving (task 03), as Open Rails' AI: a passenger train
        // (target 1.0 m/s², braking 1.0) of 200 t with the force given, its
        // power out of reach, efficiency 0.8.
        const auto physics = [&](double force) {
            Sim::TrainPlan p = plan(t0);
            p.driving = Sim::Driving::Physics;
            p.performance.mass = 200000.0;
            p.performance.force = force;
            p.performance.power = 1e9;
            p.drivingEfficiency = 0.8f;
            return p;
        };
        const auto inputOn = [&](const Sim::TrainPlan &p, std::shared_ptr<const Sim::TrackGeometry> geometry) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->trains.push_back(p);
            return std::shared_ptr<const Sim::SimulationInput>(made);
        };
        const auto lineGeometry = std::make_shared<const Sim::TrackGeometry>(line);
        const auto speedAfter = [&](const Sim::TrainPlan &p, double seconds,
                                    std::shared_ptr<const Sim::TrackGeometry> geometry) {
            Sim::SimulationCore c(inputOn(p, geometry));
            c.step(seconds);
            return double(c.trains()[0].speed);
        };
        // Far from its path's end: half the target where full power gives
        // that (1.5 m/s²), else efficiency × that half (0.4 m/s² at full
        // power: 0.8 × 0.5).
        const double strong = speedAfter(physics(300000.0), 10.0, lineGeometry);
        const double weak = speedAfter(physics(80000.0), 10.0, lineGeometry);
        check(std::fabs(strong - 5.0) < 0.01 && std::fabs(weak - 4.0) < 0.01,
              QString("physics: half the target where full power gives it, efficiency × that below "
                      "(%1 and %2 m/s after 10 s)").arg(strong).arg(weak));
        // Its next stop within 1.5 × top speed² / deceleration (600 m): its
        // braking state, full power, at least half the target.
        Sim::TrainPlan near = physics(300000.0);
        near.stops.resize(1);
        near.stops[0].front = near.startFront + 500.0;
        Sim::TrainPlan nearWeak = near;
        nearWeak.performance.force = 80000.0;
        const double full = speedAfter(near, 2.0, lineGeometry);
        const double least = speedAfter(nearWeak, 2.0, lineGeometry);
        check(std::fabs(full - 3.0) < 0.01 && std::fabs(least - 1.0) < 0.01,
              QString("physics: its stop near, full power (%1 m/s after 2 s), at least half the target (%2)")
              .arg(full).arg(least));
        // Up a 2 % grade (its 10 m pieces each 0.2 m higher): m g 2 % less
        // force; 120 kN on 200 t then falls below half the target.
        TestTrack::Layout hill;
        hill.end(1, 10);
        hill.track(10, 1, 2, 200);
        hill.end(2, 10);
        for (int i = 0; i < 200; ++i)
            hill.db.trackNodes[10]->trVectorSection[i].y = 0.2f * float(i);
        const auto hillGeometry = std::make_shared<const Sim::TrackGeometry>(&hill.db);
        const double up = speedAfter(near, 2.0, hillGeometry);
        Sim::TrainPlan modest = physics(120000.0);
        const double modestFlat = speedAfter(modest, 10.0, lineGeometry);
        const double modestUp = speedAfter(modest, 10.0, hillGeometry);
        const double expectedUp = 2.0 * (1.5 - 9.81 * 0.02);
        check(std::fabs(up - expectedUp) < 0.02 && std::fabs(modestFlat - 5.0) < 0.01 && std::fabs(modestUp - 4.0) < 0.01,
              QString("physics: up a 2 % grade %1 m/s after 2 s (%2 expected); 120 kN gains %3 m/s in 10 s on the "
                      "level, %4 up the grade").arg(up).arg(expectedUp).arg(modestFlat).arg(modestUp));
        // Braking to its stop 1500 m on: its curve at 0.22 of its
        // deceleration, BrakingMargin above it; the creep speed at the end;
        // standing at the stop.
        Sim::TrainPlan far = physics(300000.0);
        far.stops.resize(1);
        far.stops[0].front = far.startFront + 1500.0;
        Sim::SimulationCore braking(inputOn(far, lineGeometry));
        const auto speedAt = [&](double front) {
            for (int i = 0; i < 20000 && braking.trains()[0].front < front
                     && braking.trains()[0].status != Sim::TrainStatus::Stopped; ++i)
                braking.step(0.1);
            return double(braking.trains()[0].speed);
        };
        const double stop = far.stops[0].front;
        const double at200 = speedAt(stop - 200.0);
        const double curve = std::sqrt(2.0 * Sim::SimulationCore::BrakingShare * 200.0) + Sim::SimulationCore::BrakingMargin;
        const double at5 = speedAt(stop - 5.0);
        braking.step(5.0);
        check(std::fabs(at200 - curve) < 0.05 && std::fabs(at5 - Sim::SimulationCore::CreepSpeed) < 0.01
                  && braking.trains()[0].status == Sim::TrainStatus::Stopped
                  && std::fabs(braking.trains()[0].front - stop) < 1e-6,
              QString("physics: braking, %1 m/s 200 m short (%2 on the curve), %3 m/s 5 m short, standing at the stop")
              .arg(at200).arg(curve).arg(at5));
        // The same input, the same events.
        Sim::SimulationCore once(inputOn(far, lineGeometry)), twice(inputOn(far, lineGeometry));
        const std::vector<Sim::TrainEvent> first = runToEnd(once, 0.1), second = runToEnd(twice, 0.1);
        bool same = !first.empty() && first.size() == second.size();
        for (size_t i = 0; same && i < first.size(); ++i)
            same = first[i].time == second[i].time && first[i].kind == second[i].kind;
        check(same, "physics: the same input gives the same events");
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
        {
            // A signal of two heads, the first clearing none ahead: it clears
            // as far as its other head (Open Rails takes its heads' most).
            TestTrack::Layout two;
            two.end(1, 10);
            two.track(10, 1, 2, 20);
            two.end(2, 10);
            TestTrack::signalHead(two, 10, 801, 100.0f, true, "Zero");
            TestTrack::signalHead(two, 10, 802, 100.0f, true, "Home");
            const QScopedValueRollback<TDB *> twoLive(Game::trackDB, &two.db);
            const Sim::TrackGeometry twoGeometry(&two.db);
            const Sim::TrackNetwork twoNetwork(&two.db, twoGeometry, &signalConfig, false);
            const PathTrack::Result along = PathTrack::endAt(&two.db, PathTrack::startAt(&two.db, {10, 10.0f}).nodes, {10, 190.0f});
            const Sim::TrainRoute alongRoute = Sim::BuildRoute(&two.db, along.nodes, PathTrack::legs(&two.db, along.nodes), 0.0);
            const Sim::Projection twoWay = alongRoute.parts.empty() ? Sim::Projection() : twoNetwork.project(alongRoute.parts[0]);
            check(twoWay.stopSignals.size() == 1 && twoWay.stopSignals[0].normalHeads == 2 && twoWay.stopSignals[0].kind == home,
                  "a signal's heads clear as far ahead as the furthest-clearing one");
        }
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

    // Occupation and authority (task 02, phase 2): a 2 km line cut into
    // sections by signals at 500, 1000 and 1500 m; the passing loop.
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        writeUtf16(config, testSigCfg);
        const SigCfg signalConfig(config);
        TestTrack::Layout line;
        line.end(1, 10);
        line.track(10, 1, 2, 200);
        line.end(2, 10);
        TestTrack::signalHead(line, 10, 801, 500.0f, true, "Home");
        TestTrack::signalHead(line, 10, 802, 1000.0f, true, "Home");
        TestTrack::signalHead(line, 10, 803, 1500.0f, false, "Home");
        TDB *db = &line.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, db);
        auto lineGeometry = std::make_shared<const Sim::TrackGeometry>(db);
        auto lineNetwork = std::make_shared<const Sim::TrackNetwork>(db, *lineGeometry, &signalConfig, false);
        const int t0 = 8 * 3600;
        // A path from one place to another, its start turned when it would
        // run the other way.
        const auto routeOf = [](TDB *tdb, PathTrack::TrackPoint from, PathTrack::TrackPoint to, double leadIn,
                                bool turn = false) {
            PathTrack::Result made = PathTrack::startAt(tdb, from);
            if (turn)
                made = PathTrack::turnStart(tdb, made.nodes);
            made = PathTrack::endAt(tdb, made.nodes, to);
            return Sim::BuildRoute(tdb, made.nodes, PathTrack::legs(tdb, made.nodes), leadIn);
        };
        const auto trainOn = [](const Sim::TrainRoute &route, const Sim::TrackNetwork &network, int start) {
            Sim::TrainPlan p;
            p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
            p.length = 40.0f;
            p.maxSpeed = 20.0f;
            Sim::SetRates(p);
            p.startTime = start;
            p.route = route;
            p.startFront = route.pathStart + 10.0;
            for (const Sim::RouteTrack &part : route.parts)
                p.projection.push_back(network.project(part));
            return p;
        };
        const auto inputOf = [&](std::shared_ptr<const Sim::TrackGeometry> geometry,
                                 std::shared_ptr<const Sim::TrackNetwork> network, std::vector<Sim::TrainPlan> plans) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            for (size_t i = 0; i < plans.size(); ++i) {
                plans[i].id = int(i);
                made->trains.push_back(plans[i]);
            }
            return made;
        };
        const auto firstEvent = [](const std::vector<Sim::TrainEvent> &all, int train, Sim::TrainEvent::Kind kind) {
            for (const Sim::TrainEvent &e : all)
                if (e.train == train && e.kind == kind)
                    return &e;
            return static_cast<const Sim::TrainEvent *>(nullptr);
        };
        const Sim::TrainRoute east = routeOf(db, {10, 100.0f}, {10, 1990.0f}, 31.0);
        const auto following = inputOf(lineGeometry, lineNetwork, {trainOn(east, *lineNetwork, t0), trainOn(east, *lineNetwork, t0 + 10)});
        const auto runFollowing = [&](double stepSize, double *closest) {
            Sim::SimulationCore core(following);
            std::vector<Sim::TrainEvent> all;
            *closest = 1e9;
            for (int i = 0; i < 100000 && !core.finished(); ++i) {
                core.step(stepSize);
                for (const Sim::TrainEvent &e : core.takeEvents())
                    all.push_back(e);
                const Sim::TrainState &a = core.trains()[0], &b = core.trains()[1];
                if (a.status != Sim::TrainStatus::Finished && b.status != Sim::TrainStatus::Waiting)
                    *closest = std::min(*closest, (a.front - 40.0) - b.front);
            }
            return all;
        };
        double closest = 0.0;
        const std::vector<Sim::TrainEvent> followed = runFollowing(0.1, &closest);
        const Sim::TrainEvent *startB = firstEvent(followed, 1, Sim::TrainEvent::Started);
        const Sim::TrainEvent *blockedB = firstEvent(followed, 1, Sim::TrainEvent::Blocked);
        check(startB != nullptr && startB->time > t0 + 30 && blockedB != nullptr && blockedB->index == 0
                  && firstEvent(followed, 1, Sim::TrainEvent::Finished) != nullptr && closest > 0.0,
              QString("a following train starts when the first has left its section (at +%1 s), never reaches its rear "
                      "(closest %2 m), and finishes").arg(startB ? startB->time - t0 : -1).arg(closest));
        double closestLong = 0.0;
        const std::vector<Sim::TrainEvent> followedLong = runFollowing(5.0, &closestLong);
        check(closestLong > 0.0 && followedLong.size() == followed.size(),
              QString("in 5 s steps likewise (closest %1 m)").arg(closestLong));
        double again = 0.0;
        const std::vector<Sim::TrainEvent> repeated = runFollowing(0.1, &again);
        bool same = repeated.size() == followed.size();
        for (size_t i = 0; same && i < repeated.size(); ++i)
            same = repeated[i].kind == followed[i].kind && repeated[i].train == followed[i].train
                    && repeated[i].time == followed[i].time && repeated[i].index == followed[i].index;
        check(same, "the same input, the same events");

        // Opposing trains on a single line longer than they look ahead (12 km,
        // a section a kilometre): each reserves towards the other until they
        // wait nose to nose. (On the short line the first train reserves
        // all of it, the other's start too, which then waits to start.)
        {
            TestTrack::Layout longLine;
            longLine.end(1, 10);
            longLine.track(10, 1, 2, 1200);
            longLine.end(2, 10);
            for (int k = 1; k < 12; ++k)
                TestTrack::signalHead(longLine, 10, 900 + k, k * 1000.0f, true, "Home");
            TDB *longDb = &longLine.db;
            const QScopedValueRollback<TDB *> liveLong(Game::trackDB, longDb);
            auto longGeometry = std::make_shared<const Sim::TrackGeometry>(longDb);
            auto longNetwork = std::make_shared<const Sim::TrackNetwork>(longDb, *longGeometry, &signalConfig, false);
            const Sim::TrainRoute out = routeOf(longDb, {10, 100.0f}, {10, 11900.0f}, 31.0);
            const Sim::TrainRoute back = routeOf(longDb, {10, 11960.0f}, {10, 100.0f}, 31.0);
            Sim::SimulationCore opposing(inputOf(longGeometry, longNetwork, {trainOn(out, *longNetwork, t0),
                                                                             trainOn(back, *longNetwork, t0)}));
            std::vector<Sim::TrainEvent> met;
            for (int i = 0; i < 3000; ++i) {
                opposing.step(1.0);
                for (const Sim::TrainEvent &e : opposing.takeEvents())
                    met.push_back(e);
            }
            const Sim::TrainEvent *deadlockA = firstEvent(met, 0, Sim::TrainEvent::Deadlock);
            const Sim::TrainEvent *deadlockB = firstEvent(met, 1, Sim::TrainEvent::Deadlock);
            check(deadlockA != nullptr && deadlockB != nullptr && deadlockA->index == 1 && deadlockB->index == 0
                      && opposing.trains()[0].deadlocked && opposing.trains()[1].deadlocked,
                  "opposing trains on a single line stop short of each other: a deadlock, reported for both");
        }

        // Three trains in a ring: a circle of three 1 km tracks joined by
        // junctions (a stub on each one's other output), each train on its
        // own track for the next; each waits for the one ahead of it.
        {
            TestTrack::Layout ring;
            ring.track(10, 1, 2, 100);
            ring.track(11, 2, 3, 100);
            ring.track(12, 3, 1, 100);
            ring.junction(1, 12, 10, 20);
            ring.junction(2, 10, 11, 21);
            ring.junction(3, 11, 12, 22);
            for (int k = 0; k < 3; ++k) {
                ring.track(20 + k, 1 + k, 30 + k, 10);
                ring.end(30 + k, 20 + k);
            }
            TDB *ringDb = &ring.db;
            const QScopedValueRollback<TDB *> liveRing(Game::trackDB, ringDb);
            auto ringGeometry = std::make_shared<const Sim::TrackGeometry>(ringDb);
            auto ringNetwork = std::make_shared<const Sim::TrackNetwork>(ringDb, *ringGeometry, &signalConfig, false);
            std::vector<Sim::TrainPlan> three;
            for (int k = 0; k < 3; ++k)
                three.push_back(trainOn(routeOf(ringDb, {10 + k, 800.0f}, {10 + (k + 1) % 3, 500.0f}, 31.0),
                                        *ringNetwork, t0));
            Sim::SimulationCore circle(inputOf(ringGeometry, ringNetwork, three));
            std::vector<Sim::TrainEvent> ringEvents;
            for (int i = 0; i < 600; ++i) {
                circle.step(1.0);
                for (const Sim::TrainEvent &e : circle.takeEvents())
                    ringEvents.push_back(e);
            }
            bool ringed = true;
            for (int k = 0; k < 3; ++k) {
                const Sim::TrainEvent *deadlock = firstEvent(ringEvents, k, Sim::TrainEvent::Deadlock);
                ringed = ringed && deadlock != nullptr && deadlock->index == (k + 1) % 3 && circle.trains()[size_t(k)].deadlocked;
            }
            check(ringed, "three trains in a ring, each waiting for the next: a deadlock, reported for all three");
        }

        // A loose consist where a train would stand (70 to 110 m) at its
        // start: it never starts. (One clear of it in the same section does
        // not stop it: task 07, Open Rails' CanPlaceTrain.)
        auto blockedInput = inputOf(lineGeometry, lineNetwork, {trainOn(east, *lineNetwork, t0)});
        Sim::StaticConsist standing;
        standing.track.push_back(Sim::RouteInterval{10, 80.0f, 100.0f, 0.0});
        blockedInput->staticConsists.push_back(standing);
        Sim::SimulationCore stuck(blockedInput);
        stuck.step(60.0);
        const std::vector<Sim::TrainEvent> stuckEvents = stuck.takeEvents();
        const Sim::TrainEvent *stuckBlocked = firstEvent(stuckEvents, 0, Sim::TrainEvent::Blocked);
        check(stuck.trains()[0].status == Sim::TrainStatus::Waiting && stuckBlocked != nullptr
                  && stuckBlocked->index == Sim::StandingConsist && stuck.holders()[size_t(lineNetwork->sectionAt(10, 90.0f, true))] == Sim::StandingConsist,
              "a train whose start place a loose consist stands in waits, blocked by it");

        // The passing loop: two trains for track 10 from the loop's two
        // branches take turns at the junction; one on track 13 alone runs.
        TestTrack::Layout loop;
        TestTrack::passingLoop(loop);
        TDB *loopDb = &loop.db;
        const QScopedValueRollback<TDB *> liveLoop(Game::trackDB, loopDb);
        auto loopGeometry = std::make_shared<const Sim::TrackGeometry>(loopDb);
        auto loopNetwork = std::make_shared<const Sim::TrackNetwork>(loopDb, *loopGeometry, &signalConfig, false);
        const Sim::TrainRoute fromMain = routeOf(loopDb, {11, 150.0f}, {10, 10.0f}, 0.0, true);
        const Sim::TrainRoute fromSide = routeOf(loopDb, {12, 50.0f}, {10, 10.0f}, 0.0);
        const Sim::TrainRoute alone = routeOf(loopDb, {13, 45.0f}, {13, 95.0f}, 0.0);
        Sim::SimulationCore merging(inputOf(loopGeometry, loopNetwork, {trainOn(fromMain, *loopNetwork, t0),
                                                                        trainOn(fromSide, *loopNetwork, t0),
                                                                        trainOn(alone, *loopNetwork, t0)}));
        std::vector<Sim::TrainEvent> merged;
        for (int i = 0; i < 3000 && !merging.finished(); ++i) {
            merging.step(1.0);
            for (const Sim::TrainEvent &e : merging.takeEvents())
                merged.push_back(e);
        }
        const Sim::TrainEvent *endA = firstEvent(merged, 0, Sim::TrainEvent::Finished);
        const Sim::TrainEvent *endB = firstEvent(merged, 1, Sim::TrainEvent::Finished);
        const Sim::TrainEvent *waitB = firstEvent(merged, 1, Sim::TrainEvent::Blocked);
        check(endA != nullptr && endB != nullptr && waitB != nullptr && waitB->index == 0 && endA->time < endB->time
                  && firstEvent(merged, 0, Sim::TrainEvent::Blocked) == nullptr,
              "two routes into one junction take turns: the second waits for the first");
        check(firstEvent(merged, 2, Sim::TrainEvent::Finished) != nullptr && firstEvent(merged, 2, Sim::TrainEvent::Blocked) == nullptr,
              "a route clear of theirs runs without waiting");
    }

    // Signals as authority (task 02, phase 3).
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        writeUtf16(config, testSigCfg);
        const SigCfg signalConfig(config);
        const int t0 = 8 * 3600;
        const auto routeOf = [](TDB *tdb, QVector<PathTrack::TrackPoint> points, double leadIn) {
            PathTrack::Result made = PathTrack::startAt(tdb, points.first());
            for (int k = 1; k < points.size(); ++k)
                made = PathTrack::endAt(tdb, made.nodes, points[k]);
            return Sim::BuildRoute(tdb, made.nodes, PathTrack::legs(tdb, made.nodes), leadIn);
        };
        const auto trainOn = [](const Sim::TrainRoute &route, const Sim::TrackNetwork &network, int start) {
            Sim::TrainPlan p;
            p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
            p.length = 40.0f;
            p.maxSpeed = 20.0f;
            Sim::SetRates(p);
            p.startTime = start;
            p.route = route;
            p.startFront = route.pathStart + 10.0;
            for (const Sim::RouteTrack &part : route.parts)
                p.projection.push_back(network.project(part));
            return p;
        };
        const auto inputOf = [&](std::shared_ptr<const Sim::TrackGeometry> geometry,
                                 std::shared_ptr<const Sim::TrackNetwork> network, std::vector<Sim::TrainPlan> plans) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            for (size_t i = 0; i < plans.size(); ++i) {
                plans[i].id = int(i);
                made->trains.push_back(plans[i]);
            }
            return made;
        };
        // A 2 km line with signals at 500, 1000 and 1500 m of the given
        // types (two heads at 500 m when asked); how far a train starting
        // at 100 m has its track reserved, in the line's metres.
        const auto reservedTo = [&](const QStringList &types, bool doubleFirst, bool openRails) {
            TestTrack::Layout line;
            line.end(1, 10);
            line.track(10, 1, 2, 200);
            line.end(2, 10);
            for (int k = 0; k < 3; ++k)
                TestTrack::signalHead(line, 10, 950 + k, 500.0f * (k + 1), true, types[k]);
            if (doubleFirst)
                TestTrack::signalHead(line, 10, 949, 500.0f, true, types[0]);
            TDB *db = &line.db;
            const QScopedValueRollback<TDB *> live(Game::trackDB, db);
            auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
            auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, &signalConfig, openRails);
            const Sim::TrainRoute east = routeOf(db, {{10, 100.0f}, {10, 1990.0f}}, 31.0);
            Sim::SimulationCore core(inputOf(geometry, network, {trainOn(east, *network, t0)}));
            core.step(0.1);
            return core.trains()[0].authority + 69.0;
        };
        const double legacy = reservedTo({"Home", "Home", "Home"}, true, false);
        const double openRails = reservedTo({"Home", "Home", "Home"}, true, true);
        check(std::fabs(legacy - 1000.0) < 0.5 && std::fabs(openRails - 1500.0) < 0.5,
              QString("clearing two ahead: legacy counts the first signal's two heads (to %1 m), Open Rails mode two "
                      "signals (to %2 m)").arg(legacy).arg(openRails));
        const double zero = reservedTo({"Zero", "Home", "Home"}, false, true);
        const double skip = reservedTo({"Home", "Skip", "Home"}, false, true);
        check(std::fabs(zero - 1000.0) < 0.5 && skip > 1980.0,
              QString("Open Rails' 0 clears nothing beyond its signal (to %1 m); -1 does not count (to %2 m)")
                  .arg(zero).arg(skip));

        // A train held at a red signal stops 30 m short of it: the one ahead
        // waits 10 minutes in the block past the 1000 m signal.
        TestTrack::Layout line;
        line.end(1, 10);
        line.track(10, 1, 2, 200);
        line.end(2, 10);
        for (int k = 1; k <= 3; ++k)
            TestTrack::signalHead(line, 10, 960 + k, 500.0f * k, true, "Home");
        TDB *db = &line.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, db);
        auto lineGeometry = std::make_shared<const Sim::TrackGeometry>(db);
        auto lineNetwork = std::make_shared<const Sim::TrackNetwork>(db, *lineGeometry, &signalConfig, false);
        const Sim::TrainRoute east = routeOf(db, {{10, 100.0f}, {10, 1990.0f}}, 31.0);
        Sim::TrainPlan waiting = trainOn(east, *lineNetwork, t0);
        Sim::WaitPlan wait;
        wait.front = 1200.0 - 69.0;
        wait.value = 600;
        waiting.waits.push_back(wait);
        Sim::SimulationCore held(inputOf(lineGeometry, lineNetwork, {waiting, trainOn(east, *lineNetwork, t0 + 120)}));
        held.step(400.0);
        const Sim::TrainState &behind = held.trains()[1];
        const int signal1000 = lineNetwork->heads()[size_t(lineNetwork->headOfItem(962))].signal;
        // (The signal is cleared for it into the occupied block, Open
        // Rails' BLOCK_OCCUPIED, task 07: the built-in rule shows stop.)
        check(behind.status == Sim::TrainStatus::Stopped && behind.stoppedAt == Sim::StopKind::Blocked
                  && std::fabs(behind.front - (1000.0 - 69.0 - Sim::SimulationCore::SignalMargin)) < 0.5
                  && behind.atSignal == signal1000 && behind.blockedBy == 0
                  && held.occupiedBlocks()[size_t(signal1000)] && held.aspects()[size_t(lineNetwork->headOfItem(962))] == SignalType::STOP,
              QString("a train at a red signal stands 30 m short of it (front at %1 m), waiting for the train in its block")
                  .arg(behind.front + 69.0));
        held.step(900.0);
        check(held.trains()[1].front > 1000.0 - 69.0, "it goes on when the block is clear");
        // Physics driving: it creeps up to it, the last SignalCreep metres at
        // half the creep speed.
        Sim::TrainPlan creeping = trainOn(east, *lineNetwork, t0 + 120);
        creeping.driving = Sim::Driving::Physics;
        creeping.performance.mass = 200000.0;
        creeping.performance.force = 300000.0;
        creeping.performance.power = 1e9;
        Sim::SimulationCore crept(inputOf(lineGeometry, lineNetwork, {waiting, creeping}));
        const double standAt = 1000.0 - 69.0 - Sim::SimulationCore::SignalMargin;
        const double slowest = 0.5 * Sim::SimulationCore::CreepSpeed;
        double fastestCreeping = 0.0, entered = -1.0;
        for (int i = 0; i < 6000 && !(crept.trains()[1].status == Sim::TrainStatus::Stopped
                                      && crept.trains()[1].stoppedAt == Sim::StopKind::Blocked); ++i) {
            crept.step(0.1);
            const double left = standAt - crept.trains()[1].front;
            if (crept.trains()[1].status == Sim::TrainStatus::Running && left < Sim::SimulationCore::SignalCreep - 2.0)
                fastestCreeping = std::max(fastestCreeping, double(crept.trains()[1].speed));
            if (entered < 0.0 && left <= Sim::SimulationCore::SignalCreep)
                entered = crept.time();
        }
        const double creptFor = crept.time() - entered;
        check(std::fabs(crept.trains()[1].front - standAt) < 0.5 && fastestCreeping <= slowest + 0.01
                  && creptFor > (Sim::SimulationCore::SignalCreep - 2.0) / slowest,
              QString("physics: it creeps the last %1 m to a red signal at up to %2 m/s (%3 s)")
                  .arg(Sim::SimulationCore::SignalCreep).arg(fastestCreeping).arg(creptFor));

        // The passing loop: the train through the loop's other track sets the
        // junction its way and holds it; the one from the main track waits.
        TestTrack::Layout loop;
        TestTrack::passingLoop(loop);
        TDB *loopDb = &loop.db;
        const QScopedValueRollback<TDB *> liveLoop(Game::trackDB, loopDb);
        auto loopGeometry = std::make_shared<const Sim::TrackGeometry>(loopDb);
        auto loopNetwork = std::make_shared<const Sim::TrackNetwork>(loopDb, *loopGeometry, &signalConfig, false);
        const Sim::TrainRoute through = routeOf(loopDb, {{10, 20.0f}, {12, 100.0f}, {13, 80.0f}}, 0.0);
        PathTrack::Result fromMain = PathTrack::turnStart(loopDb, PathTrack::startAt(loopDb, {11, 150.0f}).nodes);
        fromMain = PathTrack::endAt(loopDb, fromMain.nodes, {10, 10.0f});
        const Sim::TrainRoute merge = Sim::BuildRoute(loopDb, fromMain.nodes, PathTrack::legs(loopDb, fromMain.nodes), 0.0);
        Sim::SimulationCore setting(inputOf(loopGeometry, loopNetwork, {trainOn(through, *loopNetwork, t0),
                                                                        trainOn(merge, *loopNetwork, t0 + 1)}));
        const int j2 = loopNetwork->junctionSection(2);
        setting.step(2.0);
        const bool setSide = setting.switches()[size_t(j2)] == 2 && setting.holders()[size_t(j2)] == 0;
        int later = -1, laterHolder = -2;
        for (int i = 0; i < 600; ++i) {
            setting.step(1.0);
            if (setting.holders()[size_t(j2)] == 1) {
                later = setting.switches()[size_t(j2)];
                laterHolder = 1;
                break;
            }
        }
        check(setSide && later == 1 && laterHolder == 1,
              "a train reserving a junction sets it its way (the loop's side) and holds it; the next sets it back for its own");
    }

    // Aspects and speeds (task 02, phase 4).
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        writeUtf16(config, testSigCfg);
        const SigCfg signalConfig(config);
        const int t0 = 8 * 3600;
        const auto routeOf = [](TDB *tdb, QVector<PathTrack::TrackPoint> points, double leadIn) {
            PathTrack::Result made = PathTrack::startAt(tdb, points.first());
            for (int k = 1; k < points.size(); ++k)
                made = PathTrack::endAt(tdb, made.nodes, points[k]);
            return Sim::BuildRoute(tdb, made.nodes, PathTrack::legs(tdb, made.nodes), leadIn);
        };
        const auto trainOn = [](const Sim::TrainRoute &route, const Sim::TrackNetwork &network, int start) {
            Sim::TrainPlan p;
            p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
            p.length = 40.0f;
            p.maxSpeed = 30.0f;
            Sim::SetRates(p);
            p.startTime = start;
            p.route = route;
            p.startFront = route.pathStart + 10.0;
            for (const Sim::RouteTrack &part : route.parts) {
                p.projection.push_back(network.project(part));
                p.limits.push_back(Sim::SpeedLimits(p.projection.back(), network, p.passenger, {}, 0.0f, part.length()));
            }
            return p;
        };
        const auto inputOf = [&](std::shared_ptr<const Sim::TrackGeometry> geometry,
                                 std::shared_ptr<const Sim::TrackNetwork> network, std::vector<Sim::TrainPlan> plans) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            for (size_t i = 0; i < plans.size(); ++i) {
                plans[i].id = int(i);
                made->trains.push_back(plans[i]);
            }
            return made;
        };
        // A line with home signals at 500, 1000 and 1500 m clearing two
        // ahead, a distant at 1400 m.
        TestTrack::Layout line;
        line.end(1, 10);
        line.track(10, 1, 2, 200);
        line.end(2, 10);
        for (int k = 1; k <= 3; ++k)
            TestTrack::signalHead(line, 10, 970 + k, 500.0f * k, true, "Home");
        TestTrack::signalHead(line, 10, 974, 1400.0f, true, "Dist");
        TDB *db = &line.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, db);
        auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
        auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, &signalConfig, false);
        const Sim::TrainRoute east = routeOf(db, {{10, 100.0f}, {10, 1990.0f}}, 31.0);
        // A consist beyond the last signal: its block never clears.
        auto aspectInput = inputOf(geometry, network, {trainOn(east, *network, t0)});
        Sim::StaticConsist beyond;
        beyond.track.push_back(Sim::RouteInterval{10, 1700.0f, 1750.0f, 0.0});
        aspectInput->staticConsists.push_back(beyond);
        Sim::SimulationCore core(aspectInput);
        core.step(0.1);
        const auto aspectOf = [&](const Sim::SimulationCore &c, int item) {
            return c.aspects()[size_t(network->headOfItem(item))];
        };
        check(aspectOf(core, 971) == SignalType::CLEAR_2 && aspectOf(core, 972) == SignalType::APPROACH_1
                  && aspectOf(core, 973) == SignalType::STOP && aspectOf(core, 974) == SignalType::APPROACH_1
                  && core.drawStates()[size_t(network->headOfItem(972))] == 1
                  && core.drawStates()[size_t(network->headOfItem(973))] == 0,
              QString("aspects without scripts: the signal before the one at stop at caution, the one before that "
                      "clear (the type has no second caution: its clearest, as the stock scripts), the distant before "
                      "the stop at caution: %1 %2 %3 %4")
                  .arg(aspectOf(core, 971)).arg(aspectOf(core, 972)).arg(aspectOf(core, 973)).arg(aspectOf(core, 974)));
        // The lights' draw states by name, in the frame with the serial.
        const auto drawNameOf = [&](const Sim::SimulationFrame &f, int item) {
            const int head = network->headOfItem(item);
            const Sim::SignalKind &kind = network->kinds()[size_t(network->heads()[size_t(head)].kind)];
            return kind.drawStateNames.value((*f.drawStates)[size_t(head)]);
        };
        const std::shared_ptr<Sim::SimulationFrame> lit = Sim::MakeFrame(core);
        check(*lit->drawStates == core.drawStates() && lit->drawSerial > 0 && drawNameOf(*lit, 972) == "yellow"
                  && drawNameOf(*lit, 973) == "red",
              QString("the frame carries the draw states (serial %1): %2 and %3 by name")
                  .arg(lit->drawSerial).arg(drawNameOf(*lit, 972), drawNameOf(*lit, 973)));
        // Through the signal at 1000 m at its caution's 40 km/h.
        double atSignal = -1.0;
        for (int i = 0; i < 4000 && atSignal < 0.0; ++i) {
            core.step(0.1);
            if (core.trains()[0].front + 69.0 >= 1000.0)
                atSignal = core.trains()[0].speed;
        }
        check(atSignal > 0.0 && atSignal <= 40.0 / 3.6 + 0.05,
              QString("a train passes a signal at caution at its aspect's speed (%1 km/h)").arg(atSignal * 3.6));

        // Signal scripts in the core (task 05): the same line, the types'
        // scripts beside sigcfg.dat. Home: stop unless cleared, caution
        // before a signal at stop, else clear; a number plate shows caution
        // instead of stop (a permissive signal). Dist: caution while a home
        // up to the next distant is at stop.
        {
            QTemporaryDir scripted;
            const QString homeScript = R"(SCRIPT Home
	extern float block_state (), route_set (), next_sig_lr (), sig_feature (), def_draw_state ();
	extern float state, draw_state, enabled;
	if (!enabled || block_state() !=# BLOCK_CLEAR || !route_set())
	{
		if (sig_feature (SIGFEAT_NUMBER_PLATE))
			state = SIGASP_APPROACH_1;
		else
			state = SIGASP_STOP;
	}
	else if (next_sig_lr (SIGFN_NORMAL) ==# SIGASP_STOP)
		state = SIGASP_APPROACH_1;
	else
		state = SIGASP_CLEAR_2;
	draw_state = def_draw_state (state);
)";
            const QString distScript = R"(SCRIPT Dist
	extern float dist_multi_sig_mr (), def_draw_state ();
	extern float state, draw_state;
	if (dist_multi_sig_mr (SIGFN_NORMAL, SIGFN_DISTANCE) ==# SIGASP_STOP)
		state = SIGASP_APPROACH_1;
	else
		state = SIGASP_CLEAR_2;
	draw_state = def_draw_state (state);
)";
            const auto writeText = [](const QString &path, const QString &text) {
                QFile file(path);
                return file.open(QIODevice::WriteOnly) && file.write(text.toLatin1()) == text.size();
            };
            const auto networkWith = [&](const QString &scripts, const QString &folder) {
                QString config = QString(testSigCfg);
                config.replace("SignalShapes ( 0 )", "ScriptFiles ( ScriptFile ( sigscr.dat ) )\nSignalShapes ( 0 )");
                QDir().mkpath(folder);
                writeUtf16(folder + "/sigcfg.dat", config);
                writeText(folder + "/sigscr.dat", scripts);
                const SigCfg withScripts(folder + "/sigcfg.dat");
                return std::make_shared<const Sim::TrackNetwork>(db, *geometry, &withScripts, false);
            };
            // A number plate on the signal at 1500 m.
            TRitem *plated = db->trackItems.count(973) ? db->trackItems[973] : nullptr;
            if (plated != nullptr)
                plated->trSignalType1 |= 1u << 4;
            auto byScript = networkWith(homeScript + distScript, scripted.path() + "/both");
            auto homeOnly = networkWith(homeScript, scripted.path() + "/home");
            auto broken = networkWith(homeScript + "SCRIPT Dist\n\tstate = mystery ();\n", scripted.path() + "/broken");
            if (plated != nullptr)
                plated->trSignalType1 &= ~(1u << 4);
            const auto rule = [](const Sim::TrackNetwork &n, const char *name) {
                for (const Sim::SignalKind &kind : n.kinds())
                    if (kind.name == name)
                        return kind.rule;
                return Sim::SignalKind::Rule::BuiltIn;
            };
            check(byScript->hasScripts && rule(*byScript, "home") == Sim::SignalKind::Rule::Script
                      && rule(*byScript, "dist") == Sim::SignalKind::Rule::Script
                      && rule(*homeOnly, "dist") == Sim::SignalKind::Rule::Basic
                      && rule(*broken, "dist") == Sim::SignalKind::Rule::BuiltIn
                      && broken->scriptWarnings.join(" ").contains("unknown function mystery")
                      && homeOnly->scriptWarnings.join(" ").contains("without a script"),
                  QString("each type's rule: its script; none on a route with scripts: Open Rails'; a script with "
                          "problems: the built-in rule, told (%1)").arg(broken->scriptWarnings.join("; ")));
            const auto aspectIn = [&](const Sim::SimulationCore &c, const Sim::TrackNetwork &n, int item) {
                return c.aspects()[size_t(n.headOfItem(item))];
            };
            const auto run = [&](std::shared_ptr<const Sim::TrackNetwork> n) {
                auto input = inputOf(geometry, n, {trainOn(east, *n, t0)});
                input->staticConsists.push_back(beyond);
                return input;
            };
            Sim::SimulationCore scriptedCore(run(byScript));
            scriptedCore.step(0.1);
            const int s971 = aspectIn(scriptedCore, *byScript, 971), s972 = aspectIn(scriptedCore, *byScript, 972);
            const int s973 = aspectIn(scriptedCore, *byScript, 973), s974 = aspectIn(scriptedCore, *byScript, 974);
            check(s971 == SignalType::CLEAR_2 && s972 == SignalType::CLEAR_2 && s973 == SignalType::APPROACH_1
                      && s974 == SignalType::CLEAR_2
                      && scriptedCore.drawStates()[size_t(byScript->headOfItem(973))] == 1,
                  QString("by script: the plated signal at caution instead of stop, the homes before it clear, the "
                          "distant reading the homes up to the next distant clear: %1 %2 %3 %4")
                      .arg(s971).arg(s972).arg(s973).arg(s974));
            // On: the train past the signal at 500 m: it stands at stop, the
            // next ones read on.
            for (int i = 0; i < 4000 && scriptedCore.trains()[0].front + 69.0 < 600.0; ++i)
                scriptedCore.step(0.1);
            check(aspectIn(scriptedCore, *byScript, 971) == SignalType::STOP
                      && aspectIn(scriptedCore, *byScript, 972) == SignalType::CLEAR_2,
                  QString("the train past it: the signal at 500 m at stop, the one at 1000 m still clear (%1 %2)")
                      .arg(aspectIn(scriptedCore, *byScript, 971)).arg(aspectIn(scriptedCore, *byScript, 972)));
            Sim::SimulationCore basicCore(run(homeOnly));
            basicCore.step(0.1);
            Sim::SimulationCore brokenCore(run(broken));
            brokenCore.step(0.1);
            check(aspectIn(basicCore, *homeOnly, 974) == SignalType::APPROACH_1
                      && aspectIn(brokenCore, *broken, 974) == SignalType::CLEAR_2,
                  QString("the distant without a script: its most restrictive, never cleared (%1); with a broken "
                          "script: the built-in rule, clear before a signal at caution (%2)")
                      .arg(aspectIn(basicCore, *homeOnly, 974)).arg(aspectIn(brokenCore, *broken, 974)));

            // The loose consist in the block past 1500 m (task 07, Open
            // Rails' BLOCK_OCCUPIED): the signal is cleared for the train
            // into it. Plated, its script shows STOP_AND_PROCEED (as USA2's):
            // the train goes in, down to the creep speed 20 m short of the
            // signal, and joins the consist (an AI train near its path's
            // end). Without the plate it shows stop: the train stands short.
            const QString permissiveScript = R"(SCRIPT Home
	extern float block_state (), route_set (), next_sig_lr (), sig_feature (), def_draw_state ();
	extern float state, draw_state, enabled;
	if (!enabled || block_state() !=# BLOCK_CLEAR || !route_set())
	{
		if (sig_feature (SIGFEAT_NUMBER_PLATE))
			state = SIGASP_STOP_AND_PROCEED;
		else
			state = SIGASP_STOP;
	}
	else if (next_sig_lr (SIGFN_NORMAL) ==# SIGASP_STOP)
		state = SIGASP_APPROACH_1;
	else
		state = SIGASP_CLEAR_2;
	draw_state = def_draw_state (state);
)";
            if (plated != nullptr)
                plated->trSignalType1 |= 1u << 4;
            auto permissiveNetwork = networkWith(permissiveScript + distScript, scripted.path() + "/permissive");
            if (plated != nullptr)
                plated->trSignalType1 &= ~(1u << 4);
            auto stopNetwork = networkWith(permissiveScript + distScript, scripted.path() + "/stop");
            const int signal1500 = permissiveNetwork->heads()[size_t(permissiveNetwork->headOfItem(973))].signal;
            Sim::SimulationCore into(run(permissiveNetwork));
            int shown = -1;
            double approachSpeed = -1.0;
            bool joined = false;
            for (int i = 0; i < 20000 && !joined; ++i) {
                into.step(0.1);
                if (into.cleared()[size_t(signal1500)] == 0 && into.occupiedBlocks()[size_t(signal1500)])
                    shown = aspectIn(into, *permissiveNetwork, 973);
                if (approachSpeed < 0.0 && into.trains()[0].front + 69.0 >= 1500.0 - Sim::SimulationCore::SignalApproach)
                    approachSpeed = into.trains()[0].speed;
                for (const Sim::TrainEvent &e : into.takeEvents())
                    joined = joined || e.kind == Sim::TrainEvent::Joined;
            }
            check(shown == SignalType::STOP_AND_PROCEED && approachSpeed >= 0.0
                      && approachSpeed <= Sim::SimulationCore::CreepSpeed + 0.3 && joined
                      && into.trains()[0].status == Sim::TrainStatus::Finished,
                  QString("a plated signal before a loose consist: STOP_AND_PROCEED (%1), passed at %2 m/s 20 m short, "
                          "the train goes in and joins it")
                      .arg(shown).arg(approachSpeed, 0, 'f', 2));
            Sim::SimulationCore shortOf(run(stopNetwork));
            shortOf.step(600.0);
            const Sim::TrainState &standing = shortOf.trains()[0];
            const int stopSignal = stopNetwork->heads()[size_t(stopNetwork->headOfItem(973))].signal;
            check(standing.stoppedAt == Sim::StopKind::Blocked && standing.atSignal == stopSignal
                      && std::fabs(standing.front + 69.0 - (1500.0 - Sim::SimulationCore::SignalMargin)) < 0.5
                      && aspectIn(shortOf, *stopNetwork, 973) == SignalType::STOP
                      && shortOf.cleared()[size_t(stopSignal)] == 0 && shortOf.occupiedBlocks()[size_t(stopSignal)],
                  QString("without the plate it shows stop, cleared for the train into the occupied block; the train "
                          "stands short of it (front at %1 m)").arg(standing.front + 69.0));
            // Waiting point 60002 short of it (task 07, Open Rails' permission
            // request): the train goes in at stop and joins the consist.
            auto permitted = run(stopNetwork);
            permitted->trains[0].waits = {Sim::WaitPlan{0, 1465.0 - 69.0, 60002}};
            Sim::SimulationCore asked(permitted);
            bool permittedJoin = false;
            for (int i = 0; i < 9000 && !permittedJoin; ++i) {
                asked.step(0.1);
                for (const Sim::TrainEvent &e : asked.takeEvents())
                    permittedJoin = permittedJoin || e.kind == Sim::TrainEvent::Joined;
            }
            check(permittedJoin && aspectIn(asked, *stopNetwork, 973) == SignalType::STOP,
                  "with permission asked at waiting point 60002 short of it, the train passes it at stop into the "
                  "occupied block and joins the consist");
        }

        // Open Rails' forced red at station stops: a stop whose platform ends
        // 50 m short of the signal at 1000 m (route distance 931). The train
        // does not clear that signal until it stands there two minutes before
        // its departure; the signal before it shows caution meanwhile.
        {
            Sim::TrainPlan stopping = trainOn(east, *network, t0);
            Sim::StopPlan stop;
            stop.part = 0;
            stop.front = 850.0;
            stop.platformEnd = 881.0;
            stop.arrival = t0 + 60;
            stop.departure = t0 + 400;
            stop.platform = "Middle";
            stopping.stops = {stop};
            Sim::SimulationCore held(inputOf(geometry, network, {stopping}));
            held.step(10.0);
            const int approaching = aspectOf(held, 972), before = aspectOf(held, 971);
            held.step(240.0);
            const bool standing = held.trains()[0].status == Sim::TrainStatus::Stopped
                    && held.trains()[0].stoppedAt == Sim::StopKind::Station;
            const int standingAspect = aspectOf(held, 972);
            held.step(60.0);
            const int released = aspectOf(held, 972);
            double departed = -1.0;
            for (int i = 0; i < 200 && departed < 0.0; ++i) {
                held.step(1.0);
                for (const Sim::TrainEvent &e : held.takeEvents())
                    if (e.kind == Sim::TrainEvent::Departed)
                        departed = e.time;
            }
            check(approaching == SignalType::STOP && before == SignalType::APPROACH_1 && standing
                      && standingAspect == SignalType::STOP && released >= SignalType::APPROACH_1
                      && std::fabs(departed - (t0 + 400)) < 1.0,
                  QString("a station stop's exit signal: at stop while the train comes (the one before at caution: %1), "
                          "while it stands (%2), cleared two minutes before it leaves (%3); it leaves on time (%4 s)")
                      .arg(before).arg(standingAspect).arg(released).arg(departed - t0));
        }

        // A stop ending 6 m short of that signal: the train stands 16 m short
        // of it (Open Rails' clearing distance), the stop favoured over the
        // signal at stop, and leaves on time.
        {
            Sim::TrainPlan stopping = trainOn(east, *network, t0);
            Sim::StopPlan stop;
            stop.platformStart = 885.0;
            stop.platformEnd = 925.0;
            stop.front = 925.0;
            stop.arrival = t0 + 60;
            stop.departure = t0 + 400;
            stop.platform = "Near";
            stopping.stops = {stop};
            Sim::SimulationCore near(inputOf(geometry, network, {stopping}));
            while (near.time() < t0 + 250.0)
                near.step(1.0);
            const bool standing = near.trains()[0].stoppedAt == Sim::StopKind::Station;
            const double front = near.trains()[0].front, signalAt = 931.0;
            double departed = -1.0;
            for (int i = 0; i < 400 && departed < 0.0; ++i) {
                near.step(1.0);
                for (const Sim::TrainEvent &e : near.takeEvents())
                    if (e.kind == Sim::TrainEvent::Departed)
                        departed = e.time;
            }
            check(standing && std::fabs(signalAt - front - 16.0) < 0.5 && std::fabs(departed - (t0 + 400)) < 1.0,
                  QString("a stop near its exit signal: standing %1 m short of it, leaving on time (%2 s)")
                      .arg(signalAt - front, 0, 'f', 1).arg(departed - t0));
        }

        // Two stops sharing the exit signal at 1000 m (Open Rails lists it once
        // per stop): kept at stop through the first stop's two minutes, let
        // go as the train leaves it; the second stop no longer holds it.
        {
            Sim::TrainPlan stopping = trainOn(east, *network, t0);
            Sim::StopPlan first;
            first.platformStart = 760.0;
            first.platformEnd = 800.0;
            first.front = 800.0;
            first.arrival = t0 + 60;
            first.departure = t0 + 300;
            first.platform = "First";
            Sim::StopPlan second = first;
            second.platformStart = 841.0;
            second.platformEnd = 881.0;
            second.front = 881.0;
            second.arrival = t0 + 320;
            second.departure = t0 + 600;
            second.platform = "Second";
            second.entry = 1;
            stopping.stops = {first, second};
            Sim::SimulationCore shared(inputOf(geometry, network, {stopping}));
            while (shared.time() < t0 + 240.0)
                shared.step(1.0);
            const bool standing = shared.trains()[0].stoppedAt == Sim::StopKind::Station
                    && shared.trains()[0].nextStop == 1;
            const int kept = aspectOf(shared, 972);
            double departed = -1.0;
            int leaving = -1, atSecond = -1;
            for (int i = 0; i < 400 && atSecond < 0; ++i) {
                shared.step(1.0);
                for (const Sim::TrainEvent &e : shared.takeEvents()) {
                    if (e.kind == Sim::TrainEvent::Departed && departed < 0.0)
                        departed = e.time;
                    if (e.kind == Sim::TrainEvent::Arrived && e.index == 1)
                        atSecond = aspectOf(shared, 972);
                }
                if (departed >= 0.0 && leaving < 0 && shared.time() >= departed + 5.0)
                    leaving = aspectOf(shared, 972);
            }
            check(standing && kept == SignalType::STOP && std::fabs(departed - (t0 + 300)) < 1.0
                      && leaving >= SignalType::APPROACH_1 && atSecond >= SignalType::APPROACH_1,
                  QString("an exit signal two stops share: at stop at the first a minute before it leaves (%1), "
                          "cleared as it leaves on time (%2, %3 s), clear at the second (%4)")
                      .arg(kept).arg(leaving).arg(departed - t0).arg(atSecond));
        }

        // A stop past the signal at 500 m (the train does not fit short of it:
        // not its exit signal); standing there with the next stop's exit
        // signal next, Open Rails lets that hold go at once.
        {
            Sim::TrainPlan stopping = trainOn(east, *network, t0);
            Sim::StopPlan past;
            past.platformStart = 400.0;
            past.platformEnd = 425.0;
            past.front = 432.5;
            past.arrival = t0 + 30;
            past.departure = t0 + 400;
            past.platform = "Past";
            Sim::StopPlan middle = past;
            middle.platformStart = 841.0;
            middle.platformEnd = 881.0;
            middle.front = 850.0;
            middle.arrival = t0 + 450;
            middle.departure = t0 + 700;
            middle.platform = "Middle";
            middle.entry = 1;
            stopping.stops = {past, middle};
            Sim::SimulationCore letGo(inputOf(geometry, network, {stopping}));
            while (letGo.time() < t0 + 200.0)
                letGo.step(1.0);
            const bool standing = letGo.trains()[0].stoppedAt == Sim::StopKind::Station
                    && letGo.trains()[0].nextStop == 1;
            const int standingAspect = aspectOf(letGo, 972);
            int atMiddle = -1;
            for (int i = 0; i < 600 && atMiddle < 0; ++i) {
                letGo.step(1.0);
                for (const Sim::TrainEvent &e : letGo.takeEvents())
                    if (e.kind == Sim::TrainEvent::Arrived && e.index == 1)
                        atMiddle = aspectOf(letGo, 972);
            }
            check(standing && standingAspect >= SignalType::APPROACH_1 && atMiddle >= SignalType::APPROACH_1,
                  QString("standing past a signal, the next stop's exit signal next: let go at once (%1), clear at "
                          "that stop (%2)").arg(standingAspect).arg(atMiddle));
        }

        // A speed post's limit from the post, until the train's rear is past
        // the resume post.
        TestTrack::Layout posts;
        posts.end(1, 10);
        posts.track(10, 1, 2, 200);
        posts.end(2, 10);
        TestTrack::speedLimit(posts, 10, 980, 1000.0f, true, 40.0f);
        TestTrack::speedLimit(posts, 10, 981, 1400.0f, true, 0.0f)->speedpostTrItemData[0] = 0b10000011;
        TDB *postDb = &posts.db;
        const QScopedValueRollback<TDB *> livePosts(Game::trackDB, postDb);
        auto postGeometry = std::make_shared<const Sim::TrackGeometry>(postDb);
        auto postNetwork = std::make_shared<const Sim::TrackNetwork>(postDb, *postGeometry, &signalConfig, false);
        const Sim::TrainRoute postRoute = routeOf(postDb, {{10, 100.0f}, {10, 1990.0f}}, 31.0);
        const Sim::TrainPlan postTrain = trainOn(postRoute, *postNetwork, t0);
        check(postTrain.limits.size() == 1 && postTrain.limits[0].size() == 1
                  && std::fabs(postTrain.limits[0][0].from - 931.0) < 0.1 && std::fabs(postTrain.limits[0][0].to - 1331.0) < 0.1
                  && std::fabs(postTrain.limits[0][0].speed - 40.0f / 3.6f) < 0.01f,
              "a route's limits: the post's speed from it to the resume post");
        Sim::SimulationCore limited(inputOf(postGeometry, postNetwork, {postTrain}));
        double fastestIn = 0.0, fastestAfter = 0.0, atPost = -1.0;
        for (int i = 0; i < 4000 && !limited.finished(); ++i) {
            limited.step(0.1);
            const Sim::TrainState &st = limited.trains()[0];
            const double at = st.front + 69.0;
            if (at >= 1000.0 && atPost < 0.0)
                atPost = st.speed;
            if (at > 1000.0 && at - 40.0 < 1400.0)
                fastestIn = std::max(fastestIn, double(st.speed));
            if (at - 40.0 > 1450.0)
                fastestAfter = std::max(fastestAfter, double(st.speed));
        }
        check(atPost > 0.0 && atPost <= 40.0 / 3.6 + 0.05 && fastestIn <= 40.0 / 3.6 + 0.05 && fastestAfter > 12.0,
              QString("a speed limit is met at its post (%1 km/h), kept until the rear is past the resume post (at most "
                      "%2 km/h), left after (%3 km/h)").arg(atPost * 3.6).arg(fastestIn * 3.6).arg(fastestAfter * 3.6));
        // A speed zone over the limit.
        const std::vector<Sim::SpeedLimit> zoned = Sim::SpeedLimits(postTrain.projection[0], *postNetwork, true,
                                                                    {{1100.0, 1200.0}}, 25.0f / 3.6f, postRoute.parts[0].length());
        check(zoned.size() == 3 && std::fabs(zoned[1].from - 1100.0) < 0.1 && std::fabs(zoned[1].speed - 25.0f / 3.6f) < 0.01f
                  && std::fabs(zoned[2].speed - 40.0f / 3.6f) < 0.01f,
              "a speed zone lowers the limit where it lies");

        // Route signals at a junction: the head for the route set shows
        // proceed, the other stop.
        TestTrack::Layout loop;
        TestTrack::passingLoop(loop);
        TestTrack::signalHead(loop, 10, 991, 80.0f, true, "Home", 2, 1);
        TestTrack::signalHead(loop, 10, 992, 80.0f, true, "Home", 2, 2);
        TDB *loopDb = &loop.db;
        const QScopedValueRollback<TDB *> liveLoop(Game::trackDB, loopDb);
        auto loopGeometry = std::make_shared<const Sim::TrackGeometry>(loopDb);
        auto loopNetwork = std::make_shared<const Sim::TrackNetwork>(loopDb, *loopGeometry, &signalConfig, false);
        const Sim::TrainRoute mainRoute = routeOf(loopDb, {{10, 20.0f}, {13, 80.0f}}, 0.0);
        Sim::SimulationCore routed(inputOf(loopGeometry, loopNetwork, {trainOn(mainRoute, *loopNetwork, t0)}));
        routed.step(0.1);
        const int mainHead = routed.aspects()[size_t(loopNetwork->headOfItem(991))];
        const int sideHead = routed.aspects()[size_t(loopNetwork->headOfItem(992))];
        check(mainHead >= SignalType::APPROACH_1 && sideHead == SignalType::STOP,
              QString("route signals: the head for the main route proceeds (%1), the one for the loop shows stop (%2)")
                  .arg(mainHead).arg(sideHead));
    }

    // Single track (task 02, phase 5): a 1 km single line between two
    // passing loops, signals every 250 m on it both ways clearing one
    // ahead; opposing trains from the line's two ends.
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        writeUtf16(config, testSigCfg);
        const SigCfg signalConfig(config);
        const int t0 = 8 * 3600;
        TestTrack::Layout line;
        line.end(1, 10);
        line.track(10, 1, 2, 10);
        line.junction(2, 10, 11, 12);
        line.track(11, 2, 3, 20);
        line.track(12, 2, 3, 22);
        line.junction(3, 13, 11, 12);
        line.track(13, 3, 4, 100);
        line.junction(4, 13, 14, 15);
        line.track(14, 4, 5, 20);
        line.track(15, 4, 5, 22);
        line.junction(5, 16, 14, 15);
        line.track(16, 5, 6, 10);
        line.end(6, 16);
        for (int k = 1; k <= 3; ++k) {
            TestTrack::signalHead(line, 13, 1100 + k, 250.0f * k, true, "Zero");
            TestTrack::signalHead(line, 13, 1110 + k, 250.0f * k, false, "Zero");
        }
        TDB *db = &line.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, db);
        auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
        auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, &signalConfig, false);
        const auto routeOf = [&](QVector<PathTrack::TrackPoint> points) {
            PathTrack::Result made = PathTrack::startAt(db, points.first());
            for (int k = 1; k < points.size(); ++k)
                made = PathTrack::endAt(db, made.nodes, points[k]);
            return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 0.0);
        };
        const auto trainOn = [&](const Sim::TrainRoute &route, int id) {
            Sim::TrainPlan p;
            p.id = id;
            p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
            p.length = 40.0f;
            p.maxSpeed = 20.0f;
            Sim::SetRates(p);
            p.startTime = t0;
            p.route = route;
            p.startFront = 45.0;
            for (const Sim::RouteTrack &part : route.parts) {
                p.projection.push_back(network->project(part));
                p.limits.push_back({});
            }
            return p;
        };
        const auto run = [&](const Sim::TrainRoute &east, const Sim::TrainRoute &west, std::vector<Sim::TrainEvent> &all,
                             size_t *stretches) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            made->trains = {trainOn(east, 0), trainOn(west, 1)};
            Sim::SimulationCore core(made);
            *stretches = core.stretches().size();
            for (int i = 0; i < 3000 && !core.finished(); ++i) {
                core.step(1.0);
                for (const Sim::TrainEvent &e : core.takeEvents())
                    all.push_back(e);
            }
            return core.finished();
        };
        const auto count = [](const std::vector<Sim::TrainEvent> &all, Sim::TrainEvent::Kind kind) {
            int n = 0;
            for (const Sim::TrainEvent &e : all)
                n += e.kind == kind ? 1 : 0;
            return n;
        };
        // East through the loops' main tracks, west through their others:
        // they pass at a loop.
        const Sim::TrainRoute east = routeOf({{10, 10.0f}, {16, 90.0f}});
        const Sim::TrainRoute west = routeOf({{16, 90.0f}, {15, 100.0f}, {12, 100.0f}, {10, 10.0f}});
        std::vector<Sim::TrainEvent> passing;
        size_t stretches = 0;
        const bool passed = run(east, west, passing, &stretches);
        check(passed && stretches == 3 && count(passing, Sim::TrainEvent::Deadlock) == 0 && count(passing, Sim::TrainEvent::Blocked) >= 1,
              QString("opposing trains on a single line between loops: %1 single-track stretches, one waits at a loop, "
                      "both finish (%2 finished, %3 waits, %4 in deadlock)").arg(stretches).arg(passed ? "both" : "not")
                  .arg(count(passing, Sim::TrainEvent::Blocked)).arg(count(passing, Sim::TrainEvent::Deadlock)));
        // Both through the loops' main tracks: nowhere to pass, a deadlock
        // for the user to see.
        const Sim::TrainRoute westMain = routeOf({{16, 90.0f}, {10, 10.0f}});
        std::vector<Sim::TrainEvent> stuck;
        size_t whole = 0;
        const bool through = run(east, westMain, stuck, &whole);
        check(!through && whole == 1 && count(stuck, Sim::TrainEvent::Deadlock) == 2,
              QString("both through the loops' main tracks: one stretch end to end (%1), a deadlock reported").arg(whole));

        // Both through the main tracks, the westbound train with passing
        // paths through the loops' other tracks (phase 6): it takes the one
        // where the line beyond is claimed; both finish.
        {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            Sim::TrainPlan westbound = trainOn(westMain, 1);
            // Its route: track 16 from 90 m back to the east loop's junction
            // (90 m), the loop (200 m), the single line, the west loop.
            Sim::PassingPlan eastLoop;
            eastLoop.part = 0;
            eastLoop.from = 90.0;
            eastLoop.to = 290.0;
            eastLoop.track.append(15, 220.0f, 0.0f);
            Sim::PassingPlan westLoop;
            westLoop.part = 0;
            westLoop.from = 1290.0;
            westLoop.to = 1490.0;
            westLoop.track.append(12, 220.0f, 0.0f);
            westbound.passing = {eastLoop, westLoop};
            made->trains = {trainOn(east, 0), westbound};
            Sim::SimulationCore core(made);
            std::vector<Sim::TrainEvent> all;
            bool onSide = false;
            for (int i = 0; i < 3000 && !core.finished(); ++i) {
                core.step(1.0);
                for (const Sim::TrainEvent &e : core.takeEvents())
                    all.push_back(e);
                // Its vehicles on the east loop's other track at some time.
                std::vector<Sim::TrackPose> fronts, rears;
                if (core.vehicleEnds(1, fronts, rears) && !fronts.empty()) {
                    const Sim::TrackPose &p = fronts.front();
                    float place[7];
                    for (float d = 0.0f; d <= 220.0f && !onSide; d += 10.0f)
                        if (db->getDrawPositionOnTrNode(place, 15, d) && std::fabs(place[0] - p.x) < 0.5f
                                && std::fabs(place[2] - p.z) < 0.5f && int(place[5]) == p.tileX)
                            onSide = true;
                }
            }
            check(core.finished() && count(all, Sim::TrainEvent::Deadlock) == 0 && onSide,
                  QString("with passing paths it takes the east loop's other track and both finish (%1, %2 deadlocked)")
                      .arg(core.finished() ? "finished" : "not finished").arg(count(all, Sim::TrainEvent::Deadlock)));
        }
    }

    // Where trains part is a place to pass only when one of them fits there
    // and no standing consist is on their tracks: a 1 km single line between
    // a short loop (200 and 220 m) and a long one (400 and 420 m).
    {
        QTemporaryDir folder;
        const QString config = folder.path() + "/sigcfg.dat";
        writeUtf16(config, testSigCfg);
        const SigCfg signalConfig(config);
        const int t0 = 8 * 3600;
        TestTrack::Layout line;
        line.end(1, 10);
        line.track(10, 1, 2, 40);
        line.junction(2, 10, 11, 12);
        line.track(11, 2, 3, 20);
        line.track(12, 2, 3, 22);
        line.junction(3, 13, 11, 12);
        line.track(13, 3, 4, 100);
        line.junction(4, 13, 14, 15);
        line.track(14, 4, 5, 40);
        line.track(15, 4, 5, 42);
        line.junction(5, 16, 14, 15);
        line.track(16, 5, 6, 40);
        line.end(6, 16);
        for (int k = 1; k <= 3; ++k) {
            TestTrack::signalHead(line, 13, 1100 + k, 250.0f * k, true, "Zero");
            TestTrack::signalHead(line, 13, 1110 + k, 250.0f * k, false, "Zero");
        }
        TDB *db = &line.db;
        const QScopedValueRollback<TDB *> live(Game::trackDB, db);
        auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
        auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, &signalConfig, false);
        const auto routeOf = [&](QVector<PathTrack::TrackPoint> points) {
            PathTrack::Result made = PathTrack::startAt(db, points.first());
            for (int k = 1; k < points.size(); ++k)
                made = PathTrack::endAt(db, made.nodes, points[k]);
            return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 0.0);
        };
        const auto trainOn = [&](const Sim::TrainRoute &route, int id, float length) {
            Sim::TrainPlan p;
            p.id = id;
            p.vehicles = {Sim::Vehicle{length / 2.0f, false, 4}, Sim::Vehicle{length / 2.0f, false, 1}};
            p.length = length;
            p.maxSpeed = 20.0f;
            Sim::SetRates(p);
            p.startTime = t0;
            p.route = route;
            p.startFront = length + 5.0;
            for (const Sim::RouteTrack &part : route.parts) {
                p.projection.push_back(network->project(part));
                p.limits.push_back({});
            }
            return p;
        };
        float track15[2] = {0.0f, 0.0f};
        {
            float place[7];
            db->getDrawPositionOnTrNode(place, 15, 1.0f);
            track15[0] = place[0];
            db->getDrawPositionOnTrNode(place, 15, 419.0f);
            track15[1] = place[0];
            if (track15[0] > track15[1])
                std::swap(track15[0], track15[1]);
        }
        struct Outcome {
            bool finished = false;
            size_t stretches = 0;
            int deadlocks = 0;
            bool onTrack15 = false;
        };
        const auto run = [&](const std::vector<Sim::TrainPlan> &trains, bool withConsist, int watch = 0) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->network = network;
            made->trains = trains;
            if (withConsist) {
                Sim::StaticConsist standing;
                standing.track.push_back(Sim::RouteInterval{14, 150.0f, 250.0f, 0.0});
                made->staticConsists.push_back(standing);
            }
            Sim::SimulationCore core(made);
            Outcome out;
            out.stretches = core.stretches().size();
            for (int i = 0; i < 4000 && !core.finished(); ++i) {
                core.step(1.0);
                for (const Sim::TrainEvent &e : core.takeEvents())
                    out.deadlocks += e.kind == Sim::TrainEvent::Deadlock ? 1 : 0;
                // The layout lays each track on its own stretch of x.
                std::vector<Sim::TrackPose> fronts, rears;
                if (core.vehicleEnds(watch, fronts, rears) && !fronts.empty())
                    out.onTrack15 = out.onTrack15 || (fronts.front().x > track15[0] && fronts.front().x < track15[1]);
            }
            out.finished = core.finished();
            return out;
        };
        const Sim::TrainRoute east = routeOf({{10, 10.0f}, {16, 390.0f}});
        const Sim::TrainRoute west = routeOf({{16, 390.0f}, {15, 400.0f}, {12, 200.0f}, {10, 10.0f}});
        // 300 m trains: the short loop is single track for them.
        const Outcome longTrains = run({trainOn(east, 0, 300.0f), trainOn(west, 1, 300.0f)}, false);
        const Outcome shortTrains = run({trainOn(east, 0, 40.0f), trainOn(west, 1, 40.0f)}, false);
        check(longTrains.finished && longTrains.deadlocks == 0 && longTrains.stretches == 2 && shortTrains.stretches == 3,
              QString("trains longer than a loop pass at the other: %1 stretches (%2 for short trains), %3, %4 deadlocked")
                  .arg(longTrains.stretches).arg(shortTrains.stretches)
                  .arg(longTrains.finished ? "both finish" : "not finished").arg(longTrains.deadlocks));
        // A consist on the long loop's main track: no place to pass there;
        // the eastbound train takes its passing path round it.
        Sim::TrainPlan eastbound = trainOn(east, 0, 40.0f);
        Sim::PassingPlan round;
        round.part = 0;
        round.from = 1590.0;
        round.to = 1990.0;
        round.track.append(15, 0.0f, 420.0f);
        eastbound.passing = {round};
        const Outcome blocked = run({eastbound, trainOn(west, 1, 40.0f)}, true);
        check(blocked.finished && blocked.deadlocks == 0 && blocked.stretches == 2 && blocked.onTrack15,
              QString("a consist on a loop's main track: %1 stretches, the passing path round it taken (%2), %3, "
                      "%4 deadlocked").arg(blocked.stretches).arg(blocked.onTrack15 ? "yes" : "no")
                  .arg(blocked.finished ? "both finish" : "not finished").arg(blocked.deadlocks));
        // Both through the main tracks, the westbound with a passing path
        // round the long loop: the line is single track either side of it,
        // each claims its side from the start and they meet there (Open
        // Rails' path-based rule); without the passing path the line is one
        // stretch end to end, both start in it: a deadlock.
        const Sim::TrainRoute westMain = routeOf({{16, 390.0f}, {10, 10.0f}});
        Sim::TrainPlan westbound = trainOn(westMain, 1, 40.0f);
        Sim::PassingPlan loopRound;
        loopRound.part = 0;
        loopRound.from = 390.0;
        loopRound.to = 790.0;
        loopRound.track.append(15, 420.0f, 0.0f);
        westbound.passing = {loopRound};
        const Outcome met = run({trainOn(east, 0, 40.0f), westbound}, false, 1);
        const Outcome endToEnd = run({trainOn(east, 0, 40.0f), trainOn(westMain, 1, 40.0f)}, false, 1);
        check(met.finished && met.deadlocks == 0 && met.onTrack15 && met.stretches == 2 && endToEnd.stretches == 1
                  && endToEnd.deadlocks > 0,
              QString("a passing path in the middle of a single line: %1 stretches (%2 without), they meet at its loop "
                      "(%3, %4), %5 deadlocked; without it %6 deadlocked")
                  .arg(met.stretches).arg(endToEnd.stretches).arg(met.onTrack15 ? "round it" : "not round it")
                  .arg(met.finished ? "both finish" : "not finished").arg(met.deadlocks).arg(endToEnd.deadlocks));
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
        // The signal scripts (task 05): read, each type of a head with its
        // rule.
        int byScript = 0, basic = 0, builtIn = 0;
        std::vector<bool> used(network->kinds().size(), false);
        for (const Sim::SignalHead &head : network->heads())
            if (head.kind >= 0)
                used[size_t(head.kind)] = true;
        for (size_t k = 0; k < network->kinds().size(); ++k) {
            if (!used[k])
                continue;
            const Sim::SignalKind::Rule rule = network->kinds()[k].rule;
            byScript += rule == Sim::SignalKind::Rule::Script ? 1 : 0;
            basic += rule == Sim::SignalKind::Rule::Basic ? 1 : 0;
            builtIn += rule == Sim::SignalKind::Rule::BuiltIn ? 1 : 0;
        }
        check(!network->hasScripts || builtIn == 0,
              QString("signal scripts: %1 read; the types of heads by script %2, without one %3, with problems %4%5")
                  .arg(network->scripts.size()).arg(byScript).arg(basic).arg(builtIn)
                  .arg(network->scriptWarnings.isEmpty() ? QString() : ": " + network->scriptWarnings.join("; ")));
    }

    // TSRE_SIM_BENCH (task 07): the work planner on this route's network
    // without an activity of its own: in the yard with the most stub
    // sidings near each other, cuts of cars, a drop-off that sorts five of
    // them into another siding and a pickup; planned from beside the yard
    // and from about 25 km away.
    if (qEnvironmentVariableIsSet("TSRE_SIM_BENCH")) {
        const std::vector<Sim::TrackSection> &sections = network->sections();
        std::map<int, double> lengths;
        for (const Sim::TrackSection &section : sections)
            if (section.kind == Sim::TrackSection::Track)
                lengths[section.node] = std::max(lengths[section.node], double(section.to));
        const auto junctionAt = [&](int node, int end) {
            const int j = network->endSection(node, end);
            return j >= 0 && sections[size_t(j)].kind == Sim::TrackSection::Junction ? j : -1;
        };
        const auto neighbours = [&](int node) {
            std::vector<int> out;
            for (int end : {0, 1}) {
                const int j = junctionAt(node, end);
                if (j < 0)
                    continue;
                for (int pin : {0, 1, 2}) {
                    const int t = sections[size_t(j)].link[pin];
                    if (t >= 0 && sections[size_t(t)].node != node)
                        out.push_back(sections[size_t(t)].node);
                }
            }
            return out;
        };
        // Nodes by distance through the junctions (either way), within a
        // radius.
        const auto around = [&](int seed, double radius) {
            std::vector<std::pair<double, int>> out;
            std::map<int, double> seen{{seed, 0.0}};
            using Entry = std::pair<double, int>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
            queue.push({0.0, seed});
            while (!queue.empty()) {
                const auto [d, node] = queue.top();
                queue.pop();
                if (d > seen[node] + 1e-6)
                    continue;
                out.push_back({d, node});
                for (int next : neighbours(node)) {
                    const double nd = d + lengths[next];
                    if (nd <= radius && (seen.count(next) == 0 || nd < seen[next])) {
                        seen[next] = nd;
                        queue.push({nd, next});
                    }
                }
            }
            return out;
        };
        const auto stub = [&](int node) {
            const double length = lengths[node];
            return length >= 200.0 && length <= 1500.0 && ((junctionAt(node, 0) >= 0) != (junctionAt(node, 1) >= 0));
        };
        int seed = -1;
        size_t most = 0;
        for (const auto &[node, length] : lengths) {
            if (!stub(node))
                continue;
            size_t count = 0;
            for (const auto &[d, other] : around(node, 1500.0))
                count += stub(other) ? 1 : 0;
            if (count > most) {
                most = count;
                seed = node;
            }
        }
        std::vector<int> yard;
        std::vector<int> through;
        if (seed >= 0)
            for (const auto &[d, node] : around(seed, 1500.0)) {
                if (stub(node))
                    yard.push_back(node);
                else if (junctionAt(node, 0) >= 0 && junctionAt(node, 1) >= 0 && lengths[node] >= 100.0)
                    through.push_back(node);
            }
        qInfo().noquote() << "[bench]" << lengths.size() << "vector nodes; the yard round node" << seed << "with" << yard.size()
                          << "stub sidings within 1.5 km";
        if (yard.size() >= 5 && !through.empty()) {
            const auto car = [](unsigned int consist, unsigned int uid, bool engine) {
                Sim::Vehicle v;
                v.length = engine ? 20.0f : 15.0f;
                v.type = engine ? 5 : 2;
                v.id = consist * 65536u + uid;
                return v;
            };
            // A cut at a siding's buffer end.
            const auto cutIn = [&](int node, unsigned int consist, int count) {
                Sim::StaticConsist c;
                c.name = QString("Cut %1").arg(consist);
                for (int k = 0; k < count; ++k)
                    c.vehicles.push_back(car(consist, unsigned(k), false));
                c.length = 15.0f * float(count);
                const double length = lengths[node];
                if (junctionAt(node, 0) < 0)
                    c.track.push_back(Sim::RouteInterval{node, 5.0f, 5.0f + c.length, 0.0});
                else
                    c.track.push_back(Sim::RouteInterval{node, float(length - 5.0 - c.length), float(length - 5.0), 0.0});
                return c;
            };
            const auto plan = [&](int startNode, const QString &where) {
                auto input = std::make_shared<Sim::SimulationInput>();
                input->geometry = geometry;
                input->network = network;
                Sim::TrainPlan p;
                p.player = true;
                p.service = "Bench";
                p.vehicles = {car(0, 0, true)};
                p.length = 20.0;
                const double length = lengths[startNode];
                Sim::RouteTrack part;
                part.append(startNode, 10.0f, float(length - 10.0));
                p.route.parts.push_back(part);
                p.startFront = 40.0;
                input->trains.push_back(p);
                input->staticConsists = {cutIn(yard[0], 1, 8), cutIn(yard[1], 2, 6), cutIn(yard[2], 3, 5), cutIn(yard[4], 4, 4)};
                Sim::EventPlan drop;
                drop.kind = Sim::EventPlan::DropOffWagonsAtLocation;
                drop.name = "sort five";
                drop.activation = 1;
                drop.cars = {1u * 65536u + 1u, 1u * 65536u + 4u, 2u * 65536u + 0u, 2u * 65536u + 5u, 3u * 65536u + 2u};
                drop.siding = true;
                drop.sidingNode = yard[3];
                drop.sidingFrom = 20.0f;
                drop.sidingTo = float(lengths[yard[3]] - 20.0);
                drop.index = 0;
                Sim::EventPlan pick;
                pick.kind = Sim::EventPlan::PickUpWagons;
                pick.name = "pick up two";
                pick.activation = 1;
                pick.cars = {4u * 65536u + 0u, 4u * 65536u + 1u};
                pick.index = 1;
                input->events = {drop, pick};
                QElapsedTimer timer;
                timer.start();
                const Sim::WorkPlan work = Sim::PlanWork(*input);
                const qint64 ms = timer.elapsed();
                QStringList goals;
                for (const Sim::WorkGoal &goal : work.goals)
                    goals << QString("%1 %2 (%3 m, %4 reversals, %5 steps)%6").arg(goal.name)
                                 .arg(goal.planned ? "planned" : "NOT planned").arg(goal.metres, 0, 'f', 0).arg(goal.reversals)
                                 .arg(goal.steps.size()).arg(goal.planned ? QString() : ": " + goal.why);
                qInfo().noquote() << "[bench]" << where << "(node" << startNode << "):" << ms << "ms |" << goals.join("; ")
                                  << "| back" << QString::number(work.back, 'f', 0) << "m";
            };
            plan(through.front(), "beside the yard");
            int far = -1;
            for (const auto &[d, node] : around(through.front(), 40000.0))
                if (d >= 25000.0 && junctionAt(node, 0) >= 0 && junctionAt(node, 1) >= 0 && lengths[node] >= 200.0) {
                    far = node;
                    break;
                }
            if (far >= 0)
                plan(far, "about 25 km away");
        }
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
    int waits = 0, deadlocks = 0, stuck = 0;
    Activity *busiest = nullptr;
    size_t busiestTrains = 0;
    std::vector<double> lateness;
    double simulated = 0.0;
    qint64 inputMs = 0, runMs = 0;
    // The route's activities, then its Open Rails timetable sets (task
    // 08), each run as an activity with every train AI.
    content->loadTimetableSets();
    QVector<Activity *> runs;
    for (int id : content->activityId)
        runs << ActLib::Act.value(id);
    for (int id : content->timetableSetId)
        runs << ActLib::TimetableSets.value(id);
    for (Activity *activity : runs) {
        if (activity == nullptr || activity->loaded != 1)
            continue;
        ++activities;
        timer.restart();
        // (TSRE_SIM_NOWORK: the player's path as it is, no work orders: for
        // comparisons with Open Rails' autopilot, task 07 phase 6.)
        auto input = Sim::MakeInput(activity, &database, speedLimit, geometry, network,
                                    content->trk != nullptr ? content->trk->tempRestrictedSpeed : -1.0f,
                                    !qEnvironmentVariableIsSet("TSRE_SIM_NOWORK"));
        // TSRE_SIM_WORK (task 07's design, temporary): for activities with
        // wagon events, the player's path against the loose consists and
        // the events' sidings.
        if (input != nullptr && qEnvironmentVariableIsSet("TSRE_SIM_WORK")) {
            bool wagons = false;
            for (const Sim::EventPlan &e : input->events)
                wagons = wagons || !e.cars.empty();
            const Sim::TrainPlan *player = nullptr;
            for (const Sim::TrainPlan &t : input->trains)
                if (t.player)
                    player = &t;
            if (wagons && player != nullptr) {
                const auto overlaps = [](const Sim::RouteInterval &a, int node, float from, float to) {
                    const float lo = std::min(a.from, a.to), hi = std::max(a.from, a.to);
                    return a.node == node && std::max(lo, std::min(from, to)) < std::min(hi, std::max(from, to)) + 1e-3f;
                };
                QStringList parts;
                for (const Sim::RouteTrack &part : player->route.parts)
                    parts << QString::number(part.length(), 'f', 0);
                qInfo().noquote() << "[work]" << activity->name << "| player" << player->service << player->vehicles.size()
                                  << "cars," << player->length << "m | parts" << parts.join(" ") << "| waits"
                                  << player->waits.size();
                for (const Sim::StaticConsist &c : input->staticConsists) {
                    QStringList met;
                    for (size_t p = 0; p < player->route.parts.size(); ++p) {
                        const Sim::RouteTrack &part = player->route.parts[p];
                        double first = -1.0;
                        for (const Sim::RouteInterval &iv : part.intervals())
                            for (const Sim::RouteInterval &ci : c.track)
                                if (overlaps(iv, ci.node, ci.from, ci.to)) {
                                    double at = -1.0;
                                    for (float d : {ci.from, ci.to}) {
                                        double here = -1.0;
                                        if (part.find(ci.node, d, here))
                                            at = at < 0.0 ? here : std::min(at, here);
                                    }
                                    if (at >= 0.0)
                                        first = first < 0.0 ? at : std::min(first, at);
                                }
                        if (first >= 0.0)
                            met << QString("part %1 at %2 of %3").arg(p).arg(first, 0, 'f', 0).arg(part.length(), 0, 'f', 0);
                    }
                    qInfo().noquote() << "[work]   loose" << c.name << c.vehicles.size() << "cars," << c.length << "m:"
                                      << (met.isEmpty() ? QString("not on the path") : met.join("; "));
                }
                for (const Sim::EventPlan &e : input->events) {
                    if (e.cars.empty())
                        continue;
                    QString siding = "-";
                    if (e.siding && e.sidingNode >= 0) {
                        QStringList on;
                        for (size_t p = 0; p < player->route.parts.size(); ++p) {
                            const Sim::RouteTrack &part = player->route.parts[p];
                            bool in = false;
                            for (const Sim::RouteInterval &iv : part.intervals())
                                in = in || overlaps(iv, e.sidingNode, e.sidingFrom, e.sidingTo);
                            if (in) {
                                // The part's end in the siding (a reversal or the path's end).
                                const Sim::RoutePoint end = part.locate(part.length());
                                const bool endsIn = end.node == e.sidingNode && end.distance >= e.sidingFrom - 1.0f
                                        && end.distance <= e.sidingTo + 1.0f;
                                on << QString("part %1%2").arg(p).arg(endsIn ? " ends in it" : "");
                            }
                        }
                        siding = QString("%1 m, %2").arg(e.sidingTo - e.sidingFrom, 0, 'f', 0)
                                .arg(on.isEmpty() ? QString("not on the path") : on.join("; "));
                    } else if (e.siding) {
                        siding = "not placed";
                    }
                    int mine = 0;
                    for (unsigned int car : e.cars)
                        mine += car / 65536u == 0 ? 1 : 0;
                    const char *kinds[] = {"time", "location", "allstops", "assemble", "assemble-at", "dropoff", "pickup",
                                           "passengers", "speed", "other"};
                    qInfo().noquote() << "[work]   event" << kinds[int(e.kind)] << e.name << "|" << e.cars.size() << "cars ("
                                      << mine << "player's) | level" << e.activation << "| siding" << siding;
                }
            }
        }
        // TSRE_SIM_YARD=<activity> (task 07's sorting): the yard.
        if (input != nullptr && activity->name.compare(qEnvironmentVariable("TSRE_SIM_YARD"), Qt::CaseInsensitive) == 0
                && input->network != nullptr) {
            const Sim::TrackNetwork &net = *input->network;
            const auto car = [](unsigned int id) { return QString("%1-%2").arg(id / 65536u).arg(id % 65536u); };
            const auto length = [&](int node) {
                const auto [first, count] = net.nodeSections(node);
                return count > 0 ? net.sections()[size_t(first + count - 1)].to : 0.0f;
            };
            std::set<int> nodes;
            for (const Sim::StaticConsist &c : input->staticConsists) {
                QStringList ids, track;
                for (const Sim::Vehicle &v : c.vehicles)
                    ids << car(v.id) + (v.type >= 4 ? "E" : "");
                for (const Sim::RouteInterval &iv : c.track) {
                    track << QString("%1:%2-%3").arg(iv.node).arg(iv.from, 0, 'f', 0).arg(iv.to, 0, 'f', 0);
                    nodes.insert(iv.node);
                }
                qInfo().noquote() << "[yard] consist" << c.name << c.vehicles.size() << "cars" << QString::number(c.length, 'f', 0)
                                  << "m on" << track.join(" ") << "|" << ids.join(" ");
            }
            for (const Sim::TrainPlan &t : input->trains)
                if (t.player) {
                    QStringList ids;
                    for (const Sim::Vehicle &v : t.vehicles)
                        ids << car(v.id) + (v.type >= 4 ? "E" : "");
                    QStringList track;
                    for (const Sim::RouteInterval &iv : t.route.parts.front().intervals()) {
                        track << QString("%1:%2-%3").arg(iv.node).arg(iv.from, 0, 'f', 0).arg(iv.to, 0, 'f', 0);
                        nodes.insert(iv.node);
                    }
                    qInfo().noquote() << "[yard] player" << t.vehicles.size() << "cars, front first:" << ids.join(" ")
                                      << "| start front" << t.startFront << "| part 0:" << track.join(" ");
                }
            for (const Sim::EventPlan &e : input->events) {
                if (e.cars.empty())
                    continue;
                QStringList ids;
                for (unsigned int id : e.cars)
                    ids << car(id);
                qInfo().noquote() << "[yard] event" << e.index << int(e.kind) << e.name << "| level" << e.activation << "| siding"
                                  << (e.siding ? QString("%1:%2-%3").arg(e.sidingNode).arg(e.sidingFrom, 0, 'f', 0).arg(e.sidingTo, 0, 'f', 0)
                                               : QString("-"))
                                  << "| act" << e.activate.size() << "inc" << e.increase.size() << "dec" << e.decrease.size() << "succ" << e.success
                                  << "|" << ids.join(" ");
                if (e.siding)
                    nodes.insert(e.sidingNode);
            }
            // Two hops round them.
            for (int hop = 0; hop < 2; ++hop) {
                std::set<int> more = nodes;
                for (int node : nodes)
                    for (int end : {0, 1}) {
                        const int j = net.endSection(node, end);
                        if (j < 0 || net.sections()[size_t(j)].kind != Sim::TrackSection::Junction)
                            continue;
                        for (int pin : {0, 1, 2}) {
                            const int t = net.sections()[size_t(j)].link[pin];
                            if (t >= 0)
                                more.insert(net.sections()[size_t(t)].node);
                        }
                    }
                nodes = more;
            }
            for (int node : nodes) {
                QStringList ends;
                for (int end : {0, 1}) {
                    const int j = net.endSection(node, end);
                    if (j < 0 || net.sections()[size_t(j)].kind != Sim::TrackSection::Junction) {
                        ends << QString("end%1: buffer").arg(end);
                        continue;
                    }
                    const Sim::TrackSection &js = net.sections()[size_t(j)];
                    QStringList pins;
                    for (int pin : {0, 1, 2})
                        pins << (js.link[pin] >= 0 ? QString::number(net.sections()[size_t(js.link[pin])].node) : QString("-"));
                    ends << QString("end%1: J%2 pin%3 [in %4 | %5 %6]").arg(end).arg(js.node).arg(net.endPin(node, end))
                                .arg(pins[0], pins[1], pins[2]);
                }
                qInfo().noquote() << "[yard] node" << node << QString::number(length(node), 'f', 0) << "m |" << ends.join(" | ");
            }
        }
        // TSRE_SIM_PLAN (task 07, phase B0): the player's work orders as the
        // planner would do them, with the activity's briefing.
        // (TSRE_SIM_PLAN=<activity file name>: that activity only.)
        const QString planOnly = qEnvironmentVariable("TSRE_SIM_PLAN");
        if (input != nullptr && qEnvironmentVariableIsSet("TSRE_SIM_PLAN")
                && (planOnly == "1" || activity->name.compare(planOnly, Qt::CaseInsensitive) == 0)) {
            bool wagons = false;
            for (const Sim::EventPlan &e : input->events)
                wagons = wagons || !e.cars.empty();
            if (wagons) {
                // (From the input as it is before the work is applied.)
                const auto plain = Sim::MakeInput(activity, &database, speedLimit, geometry, network,
                                                  content->trk != nullptr ? content->trk->tempRestrictedSpeed : -1.0f, false);
                QElapsedTimer planning;
                planning.start();
                const Sim::WorkPlan plan = Sim::PlanWork(plain != nullptr ? *plain : *input);
                const qint64 ms = planning.elapsed();
                QString briefing = activity->header != nullptr ? activity->header->briefing : QString();
                briefing.replace("\\n", " / ").replace('\n', " / ");
                qInfo().noquote() << "[plan]" << activity->name << "|" << ms << "ms | briefing:" << briefing.left(700);
                const char *kinds[] = {"time", "location", "allstops", "assemble", "assemble-at", "dropoff", "pickup",
                                       "passengers", "speed", "other"};
                for (const Sim::WorkGoal &goal : plan.goals) {
                    QStringList steps;
                    for (const Sim::WorkStep &step : goal.steps) {
                        double metres = 0.0;
                        for (const Sim::WorkLeg &leg : step.legs)
                            metres += leg.length();
                        steps << QString("%1 (%2 legs, %3 m)").arg(step.what).arg(step.legs.size()).arg(metres, 0, 'f', 0);
                    }
                    qInfo().noquote() << "[plan]   " << kinds[int(goal.kind)] << goal.name << "|" << goal.cars << "cars |"
                                      << (goal.planned ? QString("planned: %1 m, %2 reversals, %3 couplings, %4 cuts")
                                                             .arg(goal.metres, 0, 'f', 0).arg(goal.reversals)
                                                             .arg(goal.couplings).arg(goal.cuts)
                                                       : "NOT planned: " + goal.why)
                                      << "|" << steps.join("; ");
                }
                double work = 0.0;
                for (const Sim::WorkGoal &goal : plan.goals)
                    work += goal.metres;
                qInfo().noquote() << "[plan]    path" << QString::number(plan.path, 'f', 0) << "m, work" << QString::number(work, 'f', 0)
                                  << "m (from" << QString::number(plan.departure, 'f', 0) << "m); back"
                                  << (plan.rejoinPart >= 0 ? QString("to the path (part %1 at %2 m, %3 skipped):").arg(plan.rejoinPart)
                                                                     .arg(plan.rejoinAt, 0, 'f', 0).arg(plan.skipped)
                                      : plan.endEvent >= 0 ? QString("to %1:").arg(input->events[size_t(plan.endEvent)].name)
                                                           : QString("to the path's end:"))
                                  << (plan.back >= 0.0 ? QString("%1 m, %2 reversals").arg(plan.back, 0, 'f', 0).arg(plan.backReversals)
                                                       : QString("no way"))
                                  << (plan.notes.isEmpty() ? QString() : "| " + plan.notes.join("; "));
            }
        }
        // TSRE_SIM_EVENT_STOPS (platforms, all): where the player's train
        // stops at stop-here events (task 06's comparisons; none by default).
        if (input != nullptr && qEnvironmentVariableIsSet("TSRE_SIM_EVENT_STOPS"))
            std::const_pointer_cast<Sim::SimulationInput>(input)->eventStops
                    = qEnvironmentVariable("TSRE_SIM_EVENT_STOPS") == "all" ? Sim::SimulationInput::EventStops::All
                    : qEnvironmentVariable("TSRE_SIM_EVENT_STOPS") == "platforms" ? Sim::SimulationInput::EventStops::Platforms
                    : Sim::SimulationInput::EventStops::None;
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
        // A timetable set's static trains (task 08) stand as they do.
        for (const std::shared_ptr<OrTimetable> &timetable : activity->timetables)
            for (const OrTimetable::Train &train : timetable->trains)
                looseConsists += train.isStatic ? 1 : 0;
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
        // TSRE_SIM_EVENTS: the trains' events to <folder>/<activity>.jsonl,
        // a line each, for scripts/orts-compare-stops.py: the activity
        // first (its start, its loose consists), then starts, stops, waits
        // (with when they ended) and ends, with the distance travelled.
        std::unique_ptr<QFile> eventLog;
        std::vector<double> waitingSince(input->trains.size(), -1.0);
        const auto logEvent = [&](double time, int train, const QString &kind, QJsonObject line,
                                  double travelled = -1.0) {
            if (eventLog == nullptr)
                return;
            line["time"] = time;
            line["kind"] = kind;
            if (train >= 0) {
                const Sim::TrainPlan &p = input->trains[size_t(train)];
                const Sim::TrainState &state = core.trains()[size_t(train)];
                line["train"] = train;
                line["service"] = p.service;
                line["serviceTime"] = p.serviceTime;
                line["traffic"] = p.traffic;
                line["player"] = p.player;
                line["travelled"] = travelled >= 0.0 ? travelled : state.travelled;
            }
            eventLog->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
        };
        if (qEnvironmentVariableIsSet("TSRE_SIM_EVENTS")) {
            eventLog = std::make_unique<QFile>(qEnvironmentVariable("TSRE_SIM_EVENTS") + "/"
                                               + QFileInfo(activity->name).completeBaseName() + ".jsonl");
            if (!eventLog->open(QIODevice::WriteOnly | QIODevice::Truncate))
                eventLog.reset();
            int loose = 0;
            for (const ActivityObject *object : activity->activityObjects)
                loose += object != nullptr && object->objectTypeId == ActivityObject::WAGONLIST ? 1 : 0;
            logEvent(input->startTime, -1, "activity", QJsonObject{{"name", activity->name}, {"loose", loose}});
        }
        // To the end, or until nothing more will happen (every train left in
        // a deadlock or held by a standing consist), as Simulate runs.
        for (int s = 0; s < 24 * 3600 && !core.settled(); ++s) {
            core.step(1.0);
            // Every 10 s each running train's next signal and its aspect
            // (scripts/orts-compare-aspects.py).
            if (eventLog != nullptr && s % 10 == 0)
                for (size_t t = 0; t < core.trains().size(); ++t) {
                    int aspect = 0;
                    double ahead = 0.0;
                    const Sim::TrainState &state = core.trains()[t];
                    const int signal = state.status == Sim::TrainStatus::Running
                            ? core.nextSignalAhead(int(t), &aspect, &ahead) : -1;
                    if (signal < 0)
                        continue;
                    logEvent(core.time(), int(t), "signal", QJsonObject{{"aspect", aspect}, {"ahead", ahead},
                             {"signal", signal}, {"cleared", core.cleared()[size_t(signal)] == int(t)},
                             {"authority", state.authority - state.front}, {"blockedBy", state.blockedBy}});
                }
            // A wait ends when the train moves again.
            for (size_t t = 0; t < waitingSince.size() && eventLog != nullptr; ++t)
                if (waitingSince[t] >= 0.0 && (core.trains()[t].status == Sim::TrainStatus::Running
                                               || core.trains()[t].status == Sim::TrainStatus::Finished)) {
                    logEvent(core.time(), int(t), "resumed", QJsonObject{{"since", waitingSince[t]}});
                    waitingSince[t] = -1.0;
                }
            for (const Sim::TrainEvent &event : core.takeEvents()) {
                switch (event.kind) {
                case Sim::TrainEvent::Started:
                    logEvent(event.time, event.train, "started", {}, event.travelled);
                    break;
                case Sim::TrainEvent::Arrived: {
                    const Sim::StopPlan &stop = input->trains[size_t(event.train)].stops[size_t(event.index)];
                    logEvent(event.time, event.train, "arrived",
                             QJsonObject{{"stop", event.index}, {"platform", stop.platform}, {"booked", stop.arrival}},
                             event.travelled);
                    break;
                }
                case Sim::TrainEvent::Departed:
                    logEvent(event.time, event.train, "departed", QJsonObject{{"stop", event.index}}, event.travelled);
                    break;
                case Sim::TrainEvent::Blocked:
                    if (waitingSince[size_t(event.train)] < 0.0)
                        waitingSince[size_t(event.train)] = event.time;
                    logEvent(event.time, event.train, "blocked",
                             QJsonObject{{"by", event.index}, {"for", int(event.waitFor)}, {"signal", event.signal}},
                             event.travelled);
                    break;
                case Sim::TrainEvent::Finished:
                    logEvent(event.time, event.train, "finished", {}, event.travelled);
                    break;
                case Sim::TrainEvent::Removed:
                    logEvent(event.time, event.train, "removed", QJsonObject{{"other", event.index}}, event.travelled);
                    break;
                case Sim::TrainEvent::Deadlock:
                    logEvent(event.time, event.train, "deadlock", QJsonObject{{"by", event.index}}, event.travelled);
                    break;
                // Couplings and cuts (task 07): the cars taken or cut off.
                case Sim::TrainEvent::Coupled:
                case Sim::TrainEvent::Joined:
                case Sim::TrainEvent::Uncoupled:
                    logEvent(event.time, event.train,
                             event.kind == Sim::TrainEvent::Coupled ? "coupled"
                             : event.kind == Sim::TrainEvent::Joined ? "joined" : "uncoupled",
                             QJsonObject{{"cars", event.signal}}, event.travelled);
                    break;
                case Sim::TrainEvent::TookCars:
                case Sim::TrainEvent::LeftCars:
                case Sim::TrainEvent::JoinedTrain:
                case Sim::TrainEvent::Parted: {
                    const char *kinds[] = {"took", "left", "joinedTrain", "parted"};
                    logEvent(event.time, event.train, kinds[int(event.kind) - int(Sim::TrainEvent::TookCars)],
                             QJsonObject{{"cars", event.signal}, {"other", event.index}}, event.travelled);
                    break;
                }
                // The activity's events (task 06; scripts/orts-compare-events.py).
                case Sim::TrainEvent::EventFired:
                    logEvent(event.time, event.train, "event",
                             QJsonObject{{"name", input->events[size_t(event.index)].name},
                                         {"message", input->events[size_t(event.index)].message},
                                         {"standIn", event.signal == 1}});
                    break;
                case Sim::TrainEvent::ActivityEnded:
                    logEvent(event.time, -1, "activityEnd", QJsonObject{{"success", event.index == 1}});
                    break;
                default:
                    break;
                }
                if (event.kind == Sim::TrainEvent::Blocked)
                    ++waits;
                if (event.kind == Sim::TrainEvent::Deadlock) {
                    ++deadlocks;
                    if (verbose) {
                        const Sim::TrainPlan &p = input->trains[size_t(event.train)];
                        qInfo().noquote() << "[tests:simulation-route] deadlock:" << activity->name << p.service
                                          << p.serviceTime << "waits for" << event.index << "at" << event.time
                                          << "length" << p.length << "passing paths" << p.passing.size()
                                          << "front" << core.trains()[size_t(event.train)].front << "part"
                                          << core.trains()[size_t(event.train)].part;
                        QStringList sides;
                        for (const Sim::PassingPlan &side : p.passing)
                            sides << QString("%1-%2:%3").arg(side.from, 0, 'f', 0).arg(side.to, 0, 'f', 0)
                                         .arg(side.track.length(), 0, 'f', 0);
                        qInfo().noquote() << "[tests:simulation-route]   its passing paths (from-to:length)" << sides.join(" ");
                    }
                }
                if (event.kind == Sim::TrainEvent::Blocked && qEnvironmentVariableIsSet("TSRE_SIM_TRACE")) {
                    const Sim::TrainPlan &p = input->trains[size_t(event.train)];
                    qInfo().noquote() << "[sim-trace]" << activity->name << p.service << p.serviceTime << "blocked at"
                                      << event.time << "by" << event.index << "for" << int(event.waitFor) << "signal"
                                      << event.signal << "front" << core.trains()[size_t(event.train)].front;
                }
                if ((event.kind == Sim::TrainEvent::Reversed || event.kind == Sim::TrainEvent::Coupled)
                        && qEnvironmentVariableIsSet("TSRE_SIM_TRACE")) {
                    const Sim::TrainPlan &p = input->trains[size_t(event.train)];
                    qInfo().noquote() << "[sim-trace]" << activity->name << p.service << p.serviceTime
                                      << (event.kind == Sim::TrainEvent::Reversed ? "reversed at" : "coupled at") << event.time
                                      << "travelled" << core.trains()[size_t(event.train)].travelled;
                }
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
        // The activity's events (task 06): how many of each kind fired, and
        // its end.
        if (!input->events.empty()) {
            const char *kinds[] = {"time", "location", "allstops", "assemble", "assemble-at", "dropoff", "pickup",
                                   "passengers", "speed", "other"};
            std::map<QString, std::pair<int, int>> tally;
            for (size_t e = 0; e < input->events.size(); ++e) {
                std::pair<int, int> &count = tally[kinds[int(input->events[e].kind)]];
                count.first += core.eventStates()[e].fired > 0 ? 1 : 0;
                ++count.second;
            }
            QStringList parts;
            for (const auto &[kind, count] : tally)
                parts << QString("%1 %2/%3").arg(kind).arg(count.first).arg(count.second);
            const Sim::ActivityEnd end = core.activityEnd();
            const QString ending = end == Sim::ActivityEnd::None ? QString("no end")
                    : QString("%1 at %2").arg(end == Sim::ActivityEnd::Success ? "success" : "failure")
                          .arg(QTime(0, 0).addSecs(int(core.activityEndedAt()) % 86400).toString("HH:mm:ss"));
            qInfo().noquote() << "[tests:simulation-route] events:" << activity->name << "|" << parts.join(", ") << "|"
                              << ending;
            // Verbose: each event not fired and why (task 06, section 7),
            // the cars where the run left them (task 07).
            const Sim::CarsNow cars = Sim::CarsOf(core);
            if (verbose)
                for (size_t e = 0; e < input->events.size(); ++e)
                    if (core.eventStates()[e].fired == 0)
                        qInfo().noquote() << "[tests:simulation-route]   not fired:" << input->events[e].name << "|"
                                          << Sim::WhyNotFired(*input, int(e), core.eventStates()[e], end,
                                                              core.activityEndedAt(), &cars);
        }
        for (size_t t = 0; t < core.trains().size(); ++t) {
            const Sim::TrainState &state = core.trains()[t];
            const bool done = state.status == Sim::TrainStatus::Finished || state.stoppedAt == Sim::StopKind::End;
            finished += done ? 1 : 0;
            const bool waiting = !done && (state.deadlocked || state.blockedBy != Sim::NoHolder);
            stuck += waiting ? 1 : 0;
            if (waiting && verbose) {
                const Sim::TrainPlan &p = input->trains[t];
                const int by = state.blockedBy;
                qInfo().noquote() << "[tests:simulation-route] stuck:" << activity->name << p.service << p.serviceTime
                                  << (state.deadlocked ? "deadlocked," : "") << "waiting for"
                                  << (by == Sim::StandingConsist ? QString("a standing consist")
                                      : by >= 0 ? input->trains[size_t(by)].service + (input->trains[size_t(by)].player ? " (player)" : "")
                                      : QString("-"))
                                  << (by >= 0 && core.trains()[size_t(by)].stoppedAt == Sim::StopKind::End ? "(at its path's end)" : "");
            }
        }
    }
    check(activities > 0 && trains > 0 && finished + stuck == trains,
          QString("%1 activities: %2 trains (%3 warnings) made in %4 ms; %5 finished, %6 stuck waiting for track "
                  "(%7 waits, %8 trains in deadlocks), %9 station stops, %10 h simulated in %11 ms")
          .arg(activities).arg(trains).arg(warnings).arg(inputMs).arg(finished).arg(stuck).arg(waits).arg(deadlocks)
          .arg(stopsMade).arg(simulated / 3600.0, 0, 'f', 1).arg(runMs));
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

    // Simulate (task 04) on every activity: its times written, then a second
    // run changes none of them; the times restored after.
    {
        const QScopedValueRollback<Route *> current(Game::currentRoute, content);
        Sim::TimetableCalculation::ForgetAll();
        int ran = 0, converged = 0, rowsWritten = 0, notFinished = 0;
        qint64 calculationMs = 0;
        QStringList differing;
        for (int id : content->activityId) {
            Activity *activity = ActLib::Act.value(id);
            if (activity == nullptr || activity->loaded != 1)
                continue;
            std::vector<std::pair<ActivityTimetable *, std::pair<QVector<int>, QVector<int>>>> before;
            for (const ActivityTrains::Train &train : ActivityTrains::List(activity))
                if (train.timetable != nullptr)
                    before.push_back({train.timetable, {train.timetable->arrivalTime, train.timetable->departTime}});
            const Sim::TimetableOptions options;
            std::unique_ptr<Sim::TimetableCalculation> first = Sim::TimetableCalculation::Prepare(activity, options);
            if (first == nullptr)
                continue;
            ++ran;
            first->run();
            const Sim::TimetableRun written = first->write(activity);
            calculationMs += written.ms;
            // TSRE_SIM_TIMETABLE_SAVE: the first run's times saved (the
            // activity and its traffic pattern) for Open Rails, only in a
            // scratch copy of a route (TSRE_SCRATCH in the game root, as
            // scripts/orts-simulation-compare.sh makes) and never through
            // a link to the real file.
            if (qEnvironmentVariableIsSet("TSRE_SIM_TIMETABLE_SAVE")) {
                Traffic *pattern = ActivityTrains::Pattern(activity);
                const QString files[2] = {activity->path + "/" + activity->name,
                                          pattern != nullptr ? pattern->path + "/" + pattern->name : QString()};
                const bool scratch = QFileInfo::exists(Game::root + "/TSRE_SCRATCH");
                const bool linked = QFileInfo(files[0]).isSymLink() || (!files[1].isEmpty() && QFileInfo(files[1]).isSymLink());
                if (scratch && !linked) {
                    activity->save();
                    if (pattern != nullptr && !written.options.playerOnly)
                        pattern->save();
                    qInfo().noquote() << "[tests:simulation-route] timetables saved:" << files[0] << files[1];
                } else {
                    qWarning().noquote() << "[tests:simulation-route] not saved (not a scratch copy, or a linked file):"
                                         << files[0] << files[1];
                }
            }
            std::unique_ptr<Sim::TimetableCalculation> second = Sim::TimetableCalculation::Prepare(activity, options);
            second->run();
            const Sim::TimetableRun &again = second->write(activity);
            bool same = written.trains.size() == again.trains.size();
            for (size_t t = 0; t < written.trains.size(); ++t) {
                const Sim::TimetableTrain &a = written.trains[t];
                notFinished += a.finished ? 0 : 1;
                for (size_t r = 0; r < a.stops.size(); ++r) {
                    rowsWritten += a.written && a.stops[r].arrival >= 0 ? 1 : 0;
                    if (same && t < again.trains.size() && r < again.trains[t].stops.size())
                        same = a.stops[r].arrival == again.trains[t].stops[r].arrival
                            && a.stops[r].departure == again.trains[t].stops[r].departure;
                }
            }
            if (same)
                ++converged;
            else
                differing << activity->name;
            for (auto &[timetable, times] : before) {
                timetable->arrivalTime = times.first;
                timetable->departTime = times.second;
            }
        }
        Sim::TimetableCalculation::ForgetAll();
        check(ran > 0 && converged == ran,
              QString("Simulate on %1 activities: %2 timetable stops written in %3 ms (%4 trains did not finish); "
                      "simulated again, %5 unchanged%6").arg(ran).arg(rowsWritten).arg(calculationMs).arg(notFinished)
                  .arg(converged).arg(differing.isEmpty() ? QString() : ", changed: " + differing.join(", ")));
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
