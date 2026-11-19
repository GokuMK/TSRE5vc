#include <tsre/tests/SimulationTestSuite.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <memory>
#include <vector>
#include <tsre/Game.h>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/RunData.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationSession.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/TrainFields.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>

namespace {

// What two trains' plans differ in, by field (the first few).
QString PlanDifference(const Sim::TrainPlan &a, const Sim::TrainPlan &b) {
    QStringList out;
    const auto field = [&](const char *name, const QString &x, const QString &y) {
        if (x != y)
            out << QString("%1 %2 / %3").arg(name, x.left(60), y.left(60));
    };
    const auto n = [](double v) { return QString::number(v, 'g', 9); };
    field("service", a.service, b.service);
    field("consist", a.consist, b.consist);
    field("vehicles", QString::number(a.vehicles.size()), QString::number(b.vehicles.size()));
    for (size_t v = 0; v < std::min(a.vehicles.size(), b.vehicles.size()); ++v) {
        field("vehicle file", a.vehicles[v].file, b.vehicles[v].file);
        field("vehicle flip", QString::number(a.vehicles[v].flip), QString::number(b.vehicles[v].flip));
        field("vehicle number", a.vehicles[v].number, b.vehicles[v].number);
    }
    field("length", n(a.length), n(b.length));
    field("startTime", QString::number(a.startTime), QString::number(b.startTime));
    field("createTime", QString::number(a.createTime), QString::number(b.createTime));
    field("formedBy", QString::number(a.formedBy), QString::number(b.formedBy));
    field("staysAtEnd", QString::number(a.staysAtEnd), QString::number(b.staysAtEnd));
    field("maxSpeed", n(a.maxSpeed), n(b.maxSpeed));
    field("startFront", n(a.startFront), n(b.startFront));
    field("parts", QString::number(a.route.parts.size()), QString::number(b.route.parts.size()));
    field("stops", QString::number(a.stops.size()), QString::number(b.stops.size()));
    for (size_t k = 0; k < std::min(a.stops.size(), b.stops.size()); ++k) {
        field("stop platform", a.stops[k].platform, b.stops[k].platform);
        field("stop arrival", QString::number(a.stops[k].arrival), QString::number(b.stops[k].arrival));
        field("stop departure", QString::number(a.stops[k].departure), QString::number(b.stops[k].departure));
        field("stop front", n(a.stops[k].front), n(b.stops[k].front));
        field("stop holdsExit", QString::number(a.stops[k].holdsExit), QString::number(b.stops[k].holdsExit));
        field("stop passengers", QString::number(a.stops[k].passengers), QString::number(b.stops[k].passengers));
    }
    field("waits", QString::number(a.waits.size()), QString::number(b.waits.size()));
    field("follows", QString::number(a.follows.size()), QString::number(b.follows.size()));
    field("extra", QStringList(a.extra.keys()).join(","), QStringList(b.extra.keys()).join(","));
    for (int f = 0; f < TrainFields::Count; ++f)
        field("field", a.fields[f], b.fields[f]);
    field("activatedBy", QString::number(a.activatedBy), QString::number(b.activatedBy));
    field("cruise", n(a.cruiseSpeed), n(b.cruiseSpeed));
    field("factors", n(a.accelerationFactor) + " " + n(a.decelerationFactor),
          n(b.accelerationFactor) + " " + n(b.decelerationFactor));
    return out.isEmpty() ? QString("something else") : out.mid(0, 4).join("; ");
}

// A run's data written by the editor's writers and read back as a client
// reads it (task server 05): served by RouteFiles under another route
// folder, loaded with the editor's loaders, the input made again. Its
// digest and each train's against the input made from the activity itself;
// what differs in `why`.
bool RoundTrip(Activity *activity, QString &why, int &files, int &trains) {
    const Sim::RunOptions options = Sim::RunOptions::Current();
    QString error;
    const std::shared_ptr<const Sim::SimulationInput> made = Sim::MakeRunInput(activity, options, &error);
    if (made == nullptr) {
        why = "no input: " + error;
        return false;
    }
    const Sim::RunData data = Sim::GatherRunData(activity);
    files = data.files.size();
    trains = int(made->trains.size());
    // The bytes as a message carries them.
    Sim::RunData received;
    if (!Sim::RunData::Decode(data.encode(), received, &error)) {
        why = error;
        return false;
    }
    const QString route = ActivityTrains::RouteDirectory(activity);
    const QString elsewhere = route + "-run05";
    const QString under = Sim::PathUnder(Game::root, elsewhere);
    QHash<QString, QByteArray> served;
    for (auto it = received.files.constBegin(); it != received.files.constEnd(); ++it)
        served.insert(under + "/" + it.key(), it.value());
    RouteFiles::setServed(Game::root, served);
    Activity *again = Sim::LoadRunData(received, elsewhere);
    RouteFiles::clearServed();
    if (again == nullptr) {
        why = "the activity does not read back";
        return false;
    }
    const std::shared_ptr<const Sim::SimulationInput> back = Sim::MakeRunInput(again, options, &error);
    if (back == nullptr) {
        why = "no input read back: " + error;
        return false;
    }
    if (back->digest() == made->digest())
        return true;
    const std::vector<QByteArray> a = made->trainDigests(), b = back->trainDigests();
    QStringList differ;
    QString first;
    for (size_t t = 0; t < std::max(a.size(), b.size()); ++t)
        if (t >= a.size() || t >= b.size() || a[t] != b[t]) {
            differ << (t < made->trains.size() ? made->trains[t].service : QString::number(t));
            if (first.isEmpty() && t < made->trains.size() && t < back->trains.size())
                first = PlanDifference(made->trains[t], back->trains[t]);
        }
    why = differ.isEmpty() ? QString("the trains agree, the rest differs")
                           : QString("%1 trains differ: %2 (the first: %3)")
                                     .arg(differ.size()).arg(differ.mid(0, 5).join(", "), first);
    return false;
}

}

int TsreTests::runSimulationWireSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-wire] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-wire] FAIL" << name; }
    };
    const QScopedValueRollback<QString> day(Game::timeOfDayDate, "2026-10-15");
    const QStringList named = qEnvironmentVariable("TSRE_SIM_BASELINE_ACTIVITY").split(',', Qt::SkipEmptyParts);

    if (!named.isEmpty()) {
        // A route's activities and timetable sets (as simulation-session).
        if (!Game::checkRoot(Game::root) || Game::route.trimmed().isEmpty() || !Game::checkRoute(Game::route)) {
            qWarning() << "simulation-wire: TSRE_SIM_BASELINE_ACTIVITY needs --game-root and --route";
            return 1;
        }
        Game::gui = false;
        Game::consoleOutput = true;
        if (Game::currentShapeLib == nullptr)
            Game::currentShapeLib = new ShapeLib();
        if (Game::currentEngLib == nullptr)
            Game::currentEngLib = new EngLib();
        Route *route = new Route();
        route->load();
        check(route->loaded, "the route loads: " + Game::route);
        if (!route->loaded)
            return 1;
        for (const QString &wanted : named) {
            const QString name = wanted.trimmed();
            Activity *activity = nullptr;
            for (int id : route->timetableSetId)
                if (ActLib::TimetableSets.value(id) != nullptr
                        && ActLib::TimetableSets.value(id)->name.compare(name, Qt::CaseInsensitive) == 0)
                    activity = ActLib::TimetableSets.value(id);
            for (int id : route->activityId)
                if (activity == nullptr && ActLib::Act.value(id) != nullptr
                        && QFileInfo(ActLib::Act.value(id)->pathid).fileName().compare(name, Qt::CaseInsensitive) == 0)
                    activity = ActLib::Act.value(id);
            check(activity != nullptr, "the activity is there: " + name);
            if (activity == nullptr)
                continue;
            route->activitySelected(activity);
            QString why;
            int files = 0, trains = 0;
            const bool same = RoundTrip(activity, why, files, trains);
            check(same, QString("%1: its run data (%2 files) written by Save's writers and read back as a client "
                                "reads it: the same input, its %3 trains alike%4")
                                .arg(name).arg(files).arg(trains).arg(same ? QString() : ": " + why));
        }
    } else {
        // simulation-ui's route.
        QTemporaryDir temp;
        const QString routePath = temp.path() + "/ROUTES/R";
        check(WriteSimulationUiRoute(temp.path()), "write the test route");
        TestTrack::Layout layout;
        MakeSimulationUiLayout(layout);
        const QScopedValueRollback<QString> root(Game::root, temp.path());
        const QScopedValueRollback<QString> routeName(Game::route, "R");
        const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
        TerrainLib terrain;
        const QScopedValueRollback<TerrainLib *> flat(Game::terrainLib, &terrain);
        EngLib engines;
        const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
        Route *route = new Route();
        route->loadPaths();
        Path *main = ActLib::GetPathByName("Main", routePath);
        Path *second = ActLib::GetPathByName("Second", routePath);
        for (auto [path, track] : {std::make_pair(main, 10), std::make_pair(second, 30)})
            if (path != nullptr) {
                PathTrack::Result made = PathTrack::startAt(&layout.db, {track, 100.0f});
                made = PathTrack::endAt(&layout.db, made.nodes, {track, 1900.0f});
                path->apply(made.nodes);
            }
        route->loadServices();
        route->loadTraffic();
        route->loadActivities();
        Activity *activity = nullptr;
        for (int id : route->activityId)
            if (ActLib::Act.value(id) != nullptr && ActLib::Act.value(id)->header != nullptr
                    && ActLib::Act.value(id)->header->name == "Simulated")
                activity = ActLib::Act.value(id);
        check(activity != nullptr && activity->loaded == 1, "the activity loads");
        if (activity != nullptr) {
            QString why;
            int files = 0, trains = 0;
            const bool same = RoundTrip(activity, why, files, trains);
            check(same && files == 6, QString("its run data (%1 files: the activity, its traffic, two services, "
                                              "two paths) written and read back: the same input, its %2 trains "
                                              "alike%3").arg(files).arg(trains).arg(same ? QString() : ": " + why));
        }
        check(!RouteFiles::serving(), "back to the route's own files");
    }
    qInfo().noquote() << QString("simulation-wire: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
