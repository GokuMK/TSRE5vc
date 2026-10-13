#include <tsre/tests/ActivityEditTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <tsre/Game.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>

namespace {

bool writeSimis(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QString crlf = text;
    crlf.replace("\n", "\r\n");
    file.write("\xff\xfe", 2);
    file.write(reinterpret_cast<const char*>(crlf.utf16()), crlf.size() * 2);
    return true;
}

// A path file: its TrPathName may differ from the file's name.
QString pathText(const QString &trPathName) {
    return QString(R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 10 0 10 2 0 )
	TrackPDP ( 0 0 100 0 10 2 0 )
)
TrackPath (
	TrPathName ( "%1" )
	Name ( "%1" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 2
		TrPathNode ( 00000000 1 4294967295 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
	)
)
)").arg(trPathName);
}

const char *serviceText = R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "Stops" )
	Train_Config ( Train )
	PathID ( "Other" )
	MaxWheelAcceleration ( 0 )
	Efficiency ( 0.9 )
	TimeTable (
		StartingSpeed ( 0 )
		EndingSpeed ( 0 )
		StartInWorld ( 0 )
		EndInWorld ( 0 )
		StationStop (
			PlatformStartID ( 12 )
			DistanceDownPath ( 100 )
			SkipCount ( 1 )
		)
		StationStop (
			PlatformStartID ( 14 )
			DistanceDownPath ( 900 )
			SkipCount ( 2 )
		)
	)
)
)";

const char *trafficText = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( Keys
	Serial ( 1 )
	Service_Definition ( a1 23
	)
	Service_Definition ( a 123
	)
)
)";

}

int TsreTests::runActivityEditSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:activity-edit] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:activity-edit] FAIL" << name; }
    };

    QTemporaryDir temp;
    const QString route = temp.path() + "/ROUTES/TEST";
    for (const QString &dir : {"PATHS", "SERVICES", "TRAFFIC"})
        QDir().mkpath(route + "/" + dir);
    // Other.pat is named "Different" inside; Trap.pat is named "Other".
    check(writeSimis(route + "/PATHS/Other.pat", pathText("Different"))
          && writeSimis(route + "/PATHS/Trap.pat", pathText("Other"))
          && writeSimis(route + "/SERVICES/Stops.srv", serviceText)
          && writeSimis(route + "/TRAFFIC/Keys.trf", trafficText),
          "write the test route");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "TEST");
    const int firstPath = ActLib::jestpath;

    // PathID names the file; TrPathName only where no file has the name.
    const int trap = ActLib::AddPath(route + "/PATHS", "Trap.pat");
    const int other = ActLib::AddPath(route + "/PATHS", "Other.pat");
    Path *otherPath = ActLib::Paths.value(other);
    check(otherPath && ActLib::Paths.value(trap) && otherPath->loaded == 1,
          "the paths load");
    check(ActLib::GetPathByName("Other", route) == otherPath,
          "PathID finds the path by its file before another path's TrPathName");
    check(ActLib::GetPathByName("other", route) == otherPath, "in any case");
    check(ActLib::GetPathByName("Different", route) == otherPath,
          "a TrPathName still finds its path");
    check(otherPath && otherPath->isNamed("Other") && otherPath->isNamed("Different")
          && !otherPath->isNamed("Trap"), "a path is named by its file and its TrPathName");

    // Stops (skip counts 1 and 2): unticking marks the service changed;
    // ticking one past the path's platforms does nothing.
    Service service(route + "/SERVICES", "Stops.srv");
    check(service.loaded == 1 && service.stationStop.size() == 2 && !service.isModified(),
          "the service loads unchanged");
    service.disableStationStop(2);
    check(service.stationStop.size() == 1 && service.isModified(), "an unticked stop marks the service changed");
    Service ticked(route + "/SERVICES", "Stops.srv");
    ticked.enableStationStop(otherPath ? otherPath->pathObjects.size() : 0);
    check(ticked.stationStop.size() == 2 && !ticked.isModified(),
          "ticking a stop the path does not have changes nothing");
    Service cleared(route + "/SERVICES", "Stops.srv");
    cleared.setNewPath("");
    check(cleared.stationStop.isEmpty() && cleared.pathId.isEmpty() && cleared.isModified(),
          "a service without a path is a change");

    // Instances are the pair (service, time).
    Traffic traffic(route + "/TRAFFIC", "Keys.trf");
    ActivityTimetable *a1 = traffic.getInstance("a1", 23);
    ActivityTimetable *a = traffic.getInstance("a", 123);
    check(traffic.service.size() == 2 && a1 == traffic.service.value(0) && a == traffic.service.value(1),
          "\"a1\" at 23 and \"a\" at 123 are different instances");
    check(traffic.getInstance("A1", 23) == a1 && traffic.getInstance("a1", 123) == nullptr,
          "names in any case, times exactly");
    check(!traffic.isModified(), "the traffic loads unchanged");
    traffic.addInstance(new ActivityTimetable());
    check(traffic.service.size() == 3 && traffic.isModified(), "an added instance marks the traffic changed");
    Traffic removed(route + "/TRAFFIC", "Keys.trf");
    removed.removeInstance(0);
    check(removed.service.size() == 1 && removed.isModified(), "a removed instance marks the traffic changed");

    for (int id = firstPath; id < ActLib::jestpath; ++id)
        ActLib::Paths[id] = nullptr;
    qInfo().noquote() << QString("activity-edit: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
