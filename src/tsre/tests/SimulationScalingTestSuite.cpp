#include <tsre/tests/SimulationTestSuite.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>
#include <sys/resource.h>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>

// The scaling workloads (task simulation 02, phase 7; the analysis's
// basic-timing-preview.md): on hand-made track, the route enlarged with the
// same traffic, more traffic on the same kind of route, and more trains
// meeting at one junction. Each is measured: the track's build time and
// memory, the input's (the trains' single-track stretches), wall time per
// simulated hour, the slowest simulated second, the core's work per
// simulated hour, a frame, the process's peak memory.

namespace {

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

// A NORMAL home signal of three aspects clearing two ahead.
const char *scalingSigCfg = R"(SIMISA@@@@@@@@@@JINX0G0t______

LightTextures ( 1
	LightTex ( "ltex" "SigLight.ace" 0 0 1 1 )
)
LightsTab ( 2
	LightsTabEntry ( "Red Light" colour ( 255 255 40 40 ) )
	LightsTabEntry ( "Green Light" colour ( 255 40 255 40 ) )
)
SignalTypes ( 1
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
)
SignalShapes ( 0
)
)";

constexpr int StartTime = 8 * 3600;

// A route under construction: its track, and the trains' paths (made once
// the track is whole).
struct Scenario {
    TestTrack::Layout layout;
    int nextNode = 1;
    int nextItem = 1;
    struct Run {
        PathTrack::TrackPoint from;
        PathTrack::TrackPoint to;
        bool turn = false;
        int start = StartTime;
    };
    std::vector<Run> runs;

    // A line of `pieces` 100 m pieces between two track ends, home signals
    // every `every` metres facing either way as asked; its track node.
    int line(int pieces, double every, bool forward, bool backward) {
        const int a = nextNode, track = nextNode + 1, b = nextNode + 2;
        nextNode += 3;
        layout.end(a, track);
        layout.track(track, a, b, pieces, true);
        layout.end(b, track);
        addSignals(track, pieces * 100.0, every, forward, backward);
        return track;
    }
    void addSignals(int track, double length, double every, bool forward, bool backward) {
        for (double at = every; at < length - 1.0; at += every) {
            if (forward)
                TestTrack::signalHead(layout, track, nextItem++, float(at), true, "Home");
            if (backward)
                TestTrack::signalHead(layout, track, nextItem++, float(at), false, "Home");
        }
    }
    // Two lines side by side, one each way, 20 km, signals every kilometre;
    // four trains each way five minutes apart.
    void doubleTrack() {
        const int east = line(200, 1000.0, true, false);
        const int west = line(200, 1000.0, false, true);
        for (int k = 0; k < 4; ++k) {
            runs.push_back({{east, 60.0f}, {east, 19950.0f}, false, StartTime + 300 * k});
            runs.push_back({{west, 19940.0f}, {west, 50.0f}, true, StartTime + 300 * k});
        }
    }
    // `branches` single lines of 2 km joining a 10 km trunk by a ladder of
    // junctions; four trains on each, a minute apart, all for the trunk.
    void junctionLadder(int branches) {
        const int trunk = nextNode++;
        const int trunkEnd = nextNode++;
        std::vector<int> junction(size_t(branches), 0), branch(size_t(branches) + 1, 0), link(size_t(branches) + 1, 0);
        for (int k = 1; k < branches; ++k)
            junction[size_t(k)] = nextNode++;
        for (int k = 1; k <= branches; ++k)
            branch[size_t(k)] = nextNode++;
        for (int k = 2; k < branches; ++k)
            link[size_t(k)] = nextNode++;
        std::vector<int> branchEnd(size_t(branches) + 1, 0);
        for (int k = 1; k <= branches; ++k)
            branchEnd[size_t(k)] = nextNode++;
        layout.track(trunk, junction[1], trunkEnd, 100, true);
        layout.end(trunkEnd, trunk);
        addSignals(trunk, 10000.0, 1000.0, true, false);
        for (int k = 1; k < branches; ++k) {
            // Its input towards the trunk; outputs: this branch and the rest.
            const int in = k == 1 ? trunk : link[size_t(k)];
            const int rest = k + 1 == branches ? branch[size_t(branches)] : link[size_t(k + 1)];
            layout.junction(junction[size_t(k)], in, branch[size_t(k)], rest);
            if (k >= 2)
                layout.track(link[size_t(k)], junction[size_t(k)], junction[size_t(k - 1)], 1, true);
        }
        for (int k = 1; k <= branches; ++k) {
            const int at = std::min(k, branches - 1);
            layout.end(branchEnd[size_t(k)], branch[size_t(k)]);
            layout.track(branch[size_t(k)], branchEnd[size_t(k)], junction[size_t(at)], 20, true);
            addSignals(branch[size_t(k)], 2000.0, 950.0, true, false);
            for (int n = 0; n < 4; ++n)
                runs.push_back({{branch[size_t(k)], 60.0f}, {trunk, 9900.0f}, false, StartTime + 60 * n});
        }
    }
};

struct Measured {
    size_t trains = 0;
    size_t sections = 0;
    size_t heads = 0;
    double geometryMs = 0.0;
    double networkMs = 0.0;
    size_t networkKiB = 0;
    double inputMs = 0.0;
    double hours = 0.0;
    double wallMsPerHour = 0.0;
    double slowestMs = 0.0;
    double frameUs = 0.0;
    int finished = 0;
    int waits = 0;
    int deadlocks = 0;
    Sim::SimulationCore::Work perHour;
    long peakKiB = 0;
};

long peakMemoryKiB() {
    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

Measured measure(Scenario &scenario, const SigCfg &config, double maxHours) {
    Measured out;
    TDB *db = &scenario.layout.db;
    const QScopedValueRollback<TDB *> live(Game::trackDB, db);
    QElapsedTimer timer;
    timer.start();
    auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
    out.geometryMs = timer.nsecsElapsed() / 1e6;
    timer.restart();
    auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, &config, false);
    out.networkMs = timer.nsecsElapsed() / 1e6;
    out.networkKiB = network->memory() / 1024;
    out.sections = network->sections().size();
    out.heads = network->heads().size();
    auto input = std::make_shared<Sim::SimulationInput>();
    input->startTime = StartTime;
    input->geometry = geometry;
    input->network = network;
    for (const Scenario::Run &run : scenario.runs) {
        PathTrack::Result made = PathTrack::startAt(db, run.from);
        if (run.turn)
            made = PathTrack::turnStart(db, made.nodes);
        made = PathTrack::endAt(db, made.nodes, run.to);
        Sim::TrainPlan plan;
        plan.id = int(input->trains.size());
        plan.service = QString("T%1").arg(plan.id);
        plan.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
        plan.length = 40.0f;
        plan.maxSpeed = 30.0f;
        Sim::SetRates(plan);
        plan.startTime = run.start;
        plan.route = Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 50.0);
        plan.startFront = plan.route.pathStart + 10.0;
        for (const Sim::RouteTrack &part : plan.route.parts) {
            plan.projection.push_back(network->project(part));
            plan.limits.push_back({});
        }
        input->trains.push_back(plan);
    }
    out.trains = input->trains.size();
    timer.restart();
    Sim::SimulationCore core(input);
    out.inputMs = timer.nsecsElapsed() / 1e6;
    const Sim::SimulationCore::Work before = core.work();
    std::shared_ptr<Sim::SimulationFrame> frame;
    qint64 framesNs = 0;
    int frames = 0;
    timer.restart();
    QElapsedTimer second;
    qint64 slowest = 0;
    int seconds = 0;
    while (!core.finished() && seconds < int(maxHours * 3600.0)) {
        second.start();
        core.step(1.0);
        slowest = std::max(slowest, second.nsecsElapsed());
        ++seconds;
        for (const Sim::TrainEvent &event : core.takeEvents()) {
            out.waits += event.kind == Sim::TrainEvent::Blocked ? 1 : 0;
            out.deadlocks += event.kind == Sim::TrainEvent::Deadlock ? 1 : 0;
        }
        // A frame a simulated minute, made from the one before.
        if (seconds % 60 == 0) {
            second.start();
            frame = Sim::MakeFrame(core, frame.get());
            framesNs += second.nsecsElapsed();
            ++frames;
        }
        // All left waiting for good.
        bool stuck = true;
        for (const Sim::TrainState &state : core.trains())
            stuck = stuck && (state.status == Sim::TrainStatus::Finished || state.stoppedAt == Sim::StopKind::End
                              || state.deadlocked);
        if (stuck)
            break;
    }
    const double wallMs = timer.nsecsElapsed() / 1e6;
    out.hours = seconds / 3600.0;
    out.wallMsPerHour = out.hours > 0.0 ? wallMs / out.hours : 0.0;
    out.slowestMs = slowest / 1e6;
    out.frameUs = frames > 0 ? framesNs / 1000.0 / frames : 0.0;
    for (const Sim::TrainState &state : core.trains())
        out.finished += state.status == Sim::TrainStatus::Finished || state.stoppedAt == Sim::StopKind::End ? 1 : 0;
    const Sim::SimulationCore::Work &after = core.work();
    const double hours = std::max(out.hours, 1e-9);
    out.perHour.authority = (unsigned long long)((after.authority - before.authority) / hours);
    out.perHour.sectionChecks = (unsigned long long)((after.sectionChecks - before.sectionChecks) / hours);
    out.perHour.aspectRuns = (unsigned long long)((after.aspectRuns - before.aspectRuns) / hours);
    out.perHour.aspectHeads = (unsigned long long)((after.aspectHeads - before.aspectHeads) / hours);
    out.peakKiB = peakMemoryKiB();
    return out;
}

QString row(const QString &name, const Measured &m) {
    return QString("%1 | %2 trains | %3 sections, %4 heads | track %5 ms + %6 ms, %7 KiB | input %8 ms | "
                   "%9 h simulated, %10 ms per simulated hour, slowest second %11 ms | per hour: %12 authority, "
                   "%13 section checks, %14 aspect runs, %15 head visits | frame %16 µs | %17 finished, %18 waits, "
                   "%19 deadlocked | peak %20 MiB")
        .arg(name).arg(m.trains).arg(m.sections).arg(m.heads)
        .arg(m.geometryMs, 0, 'f', 1).arg(m.networkMs, 0, 'f', 1).arg(m.networkKiB).arg(m.inputMs, 0, 'f', 1)
        .arg(m.hours, 0, 'f', 2).arg(m.wallMsPerHour, 0, 'f', 0).arg(m.slowestMs, 0, 'f', 2)
        .arg(m.perHour.authority).arg(m.perHour.sectionChecks).arg(m.perHour.aspectRuns).arg(m.perHour.aspectHeads)
        .arg(m.frameUs, 0, 'f', 0).arg(m.finished).arg(m.waits).arg(m.deadlocks).arg(m.peakKiB / 1024);
}

}

int TsreTests::runSimulationScalingSuite(bool verbose) {
    Q_UNUSED(verbose);
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const QString &what) {
        if (ok) {
            ++passed;
            qInfo().noquote() << "[tests:simulation-scaling] PASS" << what;
        } else {
            ++failed;
            qCritical().noquote() << "[tests:simulation-scaling] FAIL" << what;
        }
    };
    QTemporaryDir folder;
    const QString path = folder.path() + "/sigcfg.dat";
    writeUtf16(path, scalingSigCfg);
    const SigCfg config(path);
    const double maxHours = 3.0;

    // 1. The route enlarged, the same traffic: a double-track line and its
    // eight trains, with unused 10 km lines beside it.
    std::vector<Measured> size;
    for (int extra : {0, 100, 1000}) {
        Scenario scenario;
        scenario.doubleTrack();
        for (int k = 0; k < extra; ++k)
            scenario.line(100, 1000.0, true, true);
        size.push_back(measure(scenario, config, maxHours));
        qInfo().noquote() << "[tests:simulation-scaling]" << row(QString("route + %1 unused lines").arg(extra), size.back());
    }
    check(size.front().finished == int(size.front().trains) && size.back().finished == int(size.back().trains),
          "the route enlarged: every train finishes");
    // Steady-state work follows the traffic, not the route.
    check(size.back().perHour.authority <= size.front().perHour.authority * 11 / 10 + 10
              && size.back().perHour.sectionChecks <= size.front().perHour.sectionChecks * 11 / 10 + 10,
          QString("the route enlarged: authority work per hour the same (%1, %2)")
              .arg(size.front().perHour.authority).arg(size.back().perHour.authority));
    check(size.back().perHour.aspectHeads <= size.front().perHour.aspectHeads * 2 + 1000,
          QString("the route enlarged: head visits per hour follow the traffic (%1 to %2)")
              .arg(size.front().perHour.aspectHeads).arg(size.back().perHour.aspectHeads));

    // 2. More traffic: copies of the double-track line, eight trains each.
    std::vector<Measured> traffic;
    for (int copies : {1, 10, 100}) {
        Scenario scenario;
        for (int k = 0; k < copies; ++k)
            scenario.doubleTrack();
        traffic.push_back(measure(scenario, config, maxHours));
        qInfo().noquote() << "[tests:simulation-scaling]" << row(QString("%1 double-track lines").arg(copies), traffic.back());
    }
    check(traffic.back().finished == int(traffic.back().trains),
          "more traffic: every train finishes");

    // 3. Contention: more branches, four trains each, meeting at a ladder of
    // junctions onto one trunk.
    std::vector<Measured> contention;
    for (int branches : {2, 4, 8, 16}) {
        Scenario scenario;
        scenario.junctionLadder(branches);
        contention.push_back(measure(scenario, config, maxHours * 2));
        qInfo().noquote() << "[tests:simulation-scaling]" << row(QString("%1 branches onto a trunk").arg(branches), contention.back());
    }
    check(contention.back().finished == int(contention.back().trains) && contention.back().deadlocks == 0,
          "contention: every train reaches the trunk's end, no deadlock");

    qInfo().noquote() << QString("simulation-scaling: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
