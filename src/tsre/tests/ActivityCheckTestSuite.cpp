#include <tsre/tests/ActivityCheckTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QScopedValueRollback>
#include <QSet>
#include <QTemporaryDir>
#include <QTranslator>
#include <QCoreApplication>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityCheck.h>
#include <tsre/trains/EngLib.h>
#include <tsre/world/Route.h>

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

// Two nodes; with a passing link from the first that never comes back.
QString pathText(const QString &name, bool dangling) {
    if (!dangling)
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
)").arg(name);
    return QString(R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 10 0 10 2 0 )
	TrackPDP ( 0 0 100 0 10 2 0 )
	TrackPDP ( 0 0 50 0 20 2 0 )
)
TrackPath (
	TrPathName ( "%1" )
	Name ( "%1" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 3
		TrPathNode ( 00000000 1 2 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
		TrPathNode ( 00000000 4294967295 4294967295 2 )
	)
)
)").arg(name);
}

QString serviceText(const QString &name, const QString &consist, const QString &path, int skip) {
    return QString(R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "%1" )
	Train_Config ( %2 )
	PathID ( "%3" )
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
			SkipCount ( %4 )
		)
	)
)
)").arg(name, consist, path).arg(skip);
}

const char *trafficText = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( Keys
	Serial ( 1 )
	Service_Definition ( AI 30000
		ArrivalTime ( 30100 )
		DepartTime ( 30160 )
		SkipCount ( 0 )
		DistanceDownPath ( 100 )
		PlatformStartID ( 12 )
	)
)
)";

const char *consistText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Train (
	TrainCfg ( "Train"
		Name ( "Train" )
		Serial ( 1 )
		MaxVelocity ( 20.00000 0.50000 )
		NextWagonUID ( 1 )
		Durability ( 1.00000 )
		Wagon (
			WagonData ( missing_wagon missing_folder )
			UiD ( 0 )
		)
	)
)
)";

// The player's service with its stop, the traffic's AI train; an event
// naming another; a car of the player's train.
const char *goodActivity = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 1 )
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Good" )
		Description ( "" )
		Briefing ( "" )
		CompleteActivity ( 1 )
		Type ( 0 )
		Mode ( 2 )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( Main )
		StartingSpeed ( 0 )
		Duration ( 1 0 )
		Difficulty ( 1 )
		Animals ( 0 )
		Workers ( 0 )
		FuelWater ( 100 )
		FuelCoal ( 100 )
		FuelDiesel ( 100 )
	)
	Tr_Activity_File (
		Player_Service_Definition ( Player
			Player_Traffic_Definition ( 28800
				ArrivalTime ( 29000 )
				DepartTime ( 29060 )
				SkipCount ( 0 )
				DistanceDownPath ( 100 )
				PlatformStartID ( 12 )
			)
			UiD ( 0 )
			Efficiency ( 0.9 )
			SkipCount ( 0 )
			DistanceDownPath ( 100 )
			PlatformStartID ( 12 )
		)
		NextServiceUID ( 2 )
		NextActivityObjectUID ( 32768 )
		Traffic_Definition ( Keys
			Service_Definition ( AI 30000
				UiD ( 1 )
				Efficiency ( 0.9 )
				SkipCount ( 0 )
				DistanceDownPath ( 100 )
				PlatformStartID ( 12 )
			)
		)
		Events (
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					ActivateEvent ( 2 )
				)
				Name ( "Start" )
				Time ( 60 )
			)
			EventCategoryAction (
				EventTypePickUpWagons ( )
				ID ( 2 )
				Activation_Level ( 0 )
				Outcomes ( )
				Name ( "Pick up" )
				Wagon_List (
					UiD ( 0 )
					SidingItem ( 4294967295 )
					Description ( "" )
				)
			)
		)
	)
)
)";

// Each kind of problem once.
const char *badActivity = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 1 )
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Bad" )
		Description ( "" )
		Briefing ( "" )
		CompleteActivity ( 1 )
		Type ( 0 )
		Mode ( 2 )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( Main )
		StartingSpeed ( 0 )
		Duration ( 1 0 )
		Difficulty ( 1 )
		Animals ( 0 )
		Workers ( 0 )
		FuelWater ( 100 )
		FuelCoal ( 100 )
		FuelDiesel ( 100 )
	)
	Tr_Activity_File (
		Player_Service_Definition ( Missing
			Player_Traffic_Definition ( 30000
				ArrivalTime ( 30100 )
				DepartTime ( 30000 )
				SkipCount ( 0 )
				DistanceDownPath ( 100 )
				PlatformStartID ( 12 )
				ArrivalTime ( 29000 )
				DepartTime ( 29100 )
				SkipCount ( 1 )
				DistanceDownPath ( 200 )
				PlatformStartID ( 14 )
			)
			UiD ( 0 )
		)
		NextServiceUID ( 4 )
		NextActivityObjectUID ( 32770 )
		Traffic_Definition ( Keys
			Service_Definition ( AI 99
				UiD ( 1 )
			)
			Service_Definition ( Ghost 31000
				UiD ( 2 )
			)
			Service_Definition ( NoPath 32000
				UiD ( 3 )
			)
			Service_Definition ( Dangling 33000
				UiD ( 4 )
				Efficiency ( 0.9 )
			)
		)
		Events (
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					IncActLevel ( 77 )
				)
				Name ( "Links" )
				Time ( 60 )
			)
			EventCategoryAction (
				EventTypePickUpWagons ( )
				ID ( 2 )
				Activation_Level ( 0 )
				Outcomes ( )
				Name ( "Cars" )
				Wagon_List (
					UiD ( 2162688000 )
					SidingItem ( 4294967295 )
					Description ( "" )
				)
			)
			EventCategoryAction (
				EventTypePickUpPassengers ( )
				ID ( 3 )
				Activation_Level ( 0 )
				Outcomes ( )
				Name ( "Passengers" )
				StationStop ( 5 )
			)
		)
		ActivityObjects (
			ActivityObject (
				ObjectType ( WagonsList )
				Train_Config (
					TrainCfg ( "loose"
						Name ( "Loose" )
						Serial ( 1 )
						MaxVelocity ( 0 0.001 )
						NextWagonUID ( 1 )
						Durability ( 1 )
						Wagon (
							WagonData ( missing_wagon missing_folder )
							UiD ( 0 )
						)
					)
				)
				Direction ( 1 )
				ID ( 32769 )
				Tile ( 0 0 10 10 )
			)
		)
	)
)
)";

}

int TsreTests::runActivityCheckSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:activity-check] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:activity-check] FAIL" << name; }
    };
    QTemporaryDir temp;
    const QString route = temp.path() + "/ROUTES/TEST";
    for (const QString &dir : {"PATHS", "SERVICES", "TRAFFIC", "ACTIVITIES"})
        QDir().mkpath(route + "/" + dir);
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    check(writeSimis(route + "/PATHS/Main.pat", pathText("Main", false))
          && writeSimis(route + "/PATHS/Dangling.pat", pathText("Dangling", true))
          && writeSimis(route + "/SERVICES/Player.srv", serviceText("Player", "Train", "Main", 0))
          && writeSimis(route + "/SERVICES/AI.srv", serviceText("AI", "Train", "Main", 0))
          && writeSimis(route + "/SERVICES/NoPath.srv", serviceText("NoPath", "Nope", "Gone", 70000))
          && writeSimis(route + "/SERVICES/Dangling.srv", serviceText("Dangling", "Train", "Dangling", 0))
          && writeSimis(route + "/TRAFFIC/Keys.trf", trafficText)
          && writeSimis(route + "/ACTIVITIES/Good.act", goodActivity)
          && writeSimis(route + "/ACTIVITIES/Bad.act", badActivity)
          && writeSimis(temp.path() + "/TRAINS/CONSISTS/Train.con", consistText),
          "write the test route");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "TEST");
    const QScopedValueRollback<TDB*> trackDB(Game::trackDB, nullptr);
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    const int firstAct = ActLib::jestact, firstService = ActLib::jestservice;
    const int firstTraffic = ActLib::jesttraffic, firstPath = ActLib::jestpath;
    const bool routeListed = ActLib::route.contains("TEST");
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();

    QMap<QString, Activity*> activities;
    for (int id : content->activityId)
        if (Activity *activity = ActLib::Act.value(id))
            activities[activity->nameid] = activity;
    check(activities.value("Good") && activities.value("Bad"), "the activities load");

    if (Activity *good = activities.value("Good")) {
        const QVector<ActivityFinding> findings = CheckActivity(good);
        QStringList errors;
        for (const ActivityFinding &finding : findings)
            if (finding.severity == ActivityFinding::Error)
                errors << finding.code + ": " + finding.text;
        check(errors.isEmpty(), "a sound activity has no errors " + errors.join("; "));
    }
    if (Activity *bad = activities.value("Bad")) {
        const QVector<ActivityFinding> findings = CheckActivity(bad);
        QSet<QString> codes;
        for (const ActivityFinding &finding : findings) {
            codes.insert(finding.code);
            if (verbose)
                qInfo().noquote() << "[tests:activity-check]" << finding.code << finding.text;
        }
        for (const char *code : {"player.service.missing", "instance.not.in.traffic", "instance.not.in.activity",
                                 "instance.service.missing", "service.path.missing", "service.consist.missing",
                                 "skipcount.narrow", "path.passing.unjoined", "stops.incomplete",
                                 "time.depart.before.arrival", "time.order", "player.start", "event.link.missing",
                                 "event.wagon.missing", "event.station.missing", "consist.cars.missing"})
            check(codes.contains(code), QString("found: ") + code);
        bool targets = true;
        for (const ActivityFinding &finding : findings) {
            if (finding.code == "event.link.missing")
                targets = targets && finding.target == ActivityFinding::Event && finding.eventId == 1;
            if (finding.code == "service.path.missing")
                targets = targets && finding.target == ActivityFinding::Service && finding.name == "NoPath";
            if (finding.code == "instance.not.in.traffic")
                targets = targets && finding.target == ActivityFinding::Traffic && finding.name == "Keys";
        }
        check(targets, "findings point at the event, service or traffic");
    }

    for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
    for (int id = firstService; id < ActLib::jestservice; ++id) ActLib::Services[id] = nullptr;
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
    for (int id = firstPath; id < ActLib::jestpath; ++id) ActLib::Paths[id] = nullptr;
    if (!routeListed)
        ActLib::route.remove("TEST");
    qInfo().noquote() << QString("activity-check: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runActivityCheckRouteSuite(bool verbose) {
    // The findings read as the editor shows them.
    QTranslator english;
    if (english.load(":/i18n/tsre_en.qm"))
        QCoreApplication::installTranslator(&english);
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "activity-check-route requires a route with one TDB (--game-root, --route)";
        return 1;
    }
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
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();
    QMap<QString, int> kinds;
    int activities = 0, errors = 0;
    for (int id : content->activityId) {
        Activity *activity = ActLib::Act.value(id);
        if (activity == nullptr || activity->loaded != 1)
            continue;
        ++activities;
        for (const ActivityFinding &finding : CheckActivity(activity)) {
            ++kinds[finding.code];
            if (finding.severity == ActivityFinding::Error)
                ++errors;
            if (verbose)
                qInfo().noquote() << "[tests:activity-check-route]" << activity->name << finding.code << finding.text;
        }
    }
    for (auto kind = kinds.begin(); kind != kinds.end(); ++kind)
        qInfo().noquote() << "[tests:activity-check-route]" << kind.value() << kind.key();
    qInfo().noquote() << QString("activity-check-route: %1 activities, %2 errors").arg(activities).arg(errors);
    return 0;
}
