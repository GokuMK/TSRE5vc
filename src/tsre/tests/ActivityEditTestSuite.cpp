#include <tsre/tests/ActivityEditTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/EngLib.h>
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

// Three events, a comment kept, an AI instance of Keys.trf.
const char *activityText = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 1 )
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Undo" )
		Description ( "Text" )
		Briefing ( "Text" )
		CompleteActivity ( 1 )
		Type ( 0 )
		Mode ( 2 )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( Other )
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
		Player_Service_Definition ( Stops
			Player_Traffic_Definition ( 28800
			)
			UiD ( 0 )
		)
		NextServiceUID ( 2 )
		NextActivityObjectUID ( 32768 )
		ORTSAICrossingHornPattern ( US )
		Traffic_Definition ( Keys
			Service_Definition ( a1 23
				UiD ( 1 )
			)
		)
		Events (
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes ( )
				Name ( "One" )
				Time ( 60 )
			)
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 2 )
				Activation_Level ( 1 )
				Outcomes ( )
				Name ( "Two" )
				Time ( 120 )
			)
		)
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
    for (const QString &dir : {"PATHS", "SERVICES", "TRAFFIC", "ACTIVITIES"})
        QDir().mkpath(route + "/" + dir);
    // Other.pat is named "Different" inside; Trap.pat is named "Other".
    check(writeSimis(route + "/PATHS/Other.pat", pathText("Different"))
          && writeSimis(route + "/PATHS/Trap.pat", pathText("Other"))
          && writeSimis(route + "/SERVICES/Stops.srv", serviceText)
          && writeSimis(route + "/TRAFFIC/Keys.trf", trafficText)
          && writeSimis(route + "/ACTIVITIES/Undo.act", activityText),
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

    // Undo of activity data: the text kept, restored into the same objects.
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    const int firstAct = ActLib::jestact, firstTraffic = ActLib::jesttraffic;
    Activity *activity = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "Undo.act"));
    Traffic *keys = ActLib::Traffics.value(ActLib::AddTraffic(route + "/TRAFFIC", "Keys.trf"));
    check(activity && activity->loaded == 1 && activity->event.size() == 2 && keys && keys->loaded == 1,
          "the activity and its traffic load");
    if (activity && keys) {
        const QString original = activity->toText();
        int restoring = 0, restored = 0;
        QMetaObject::Connection before = QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restoring,
                                                          [&] { ++restoring; });
        QMetaObject::Connection after = QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::restored,
                                                         [&] { ++restored; });
        Undo::StateBegin();
        Undo::PushActivityData(activity);
        activity->setDisplayName("Renamed");
        activity->deleteEvent(activity->event[0]);
        activity->newEvent(ActivityEvent::CategoryAction);
        Undo::StateEnd();
        check(activity->isUnSaved() && activity->header->name == "Renamed", "the edit is made");
        Undo::UndoLast();
        check(activity->toText() == original && activity->event.size() == 2 && activity->event[0]->name == "One",
              "undo restores the activity's text, events and kept blocks");
        check(!activity->isUnSaved(), "undo back to the loaded state leaves it saved");
        check(restoring == 1 && restored == 1, "the windows are told before and after");

        // An edit that changes nothing is no step: undo goes to the one before.
        Undo::StateBegin();
        Undo::PushActivityData(activity);
        activity->setDisplayName("First");
        Undo::StateEnd();
        Undo::StateBegin();
        Undo::PushActivityData(activity);
        Undo::StateEnd();
        Undo::UndoLast();
        check(activity->header->name == "Undo", "an edit that changed nothing leaves no undo step");

        // Saved, then undone: what is restored is not on disk.
        const QString activityPath = activity->path, activityFile = activity->pathid;
        activity->path = temp.path();
        activity->pathid = temp.path() + "/Undo.act";
        Undo::StateBegin();
        Undo::PushActivityData(activity);
        activity->setDisplayName("Saved");
        Undo::StateEnd();
        activity->save();
        check(!activity->isUnSaved(), "saved");
        Undo::UndoLast();
        check(activity->header->name == "Undo" && activity->isUnSaved(), "undo after a save leaves the activity unsaved");
        activity->path = activityPath;
        activity->pathid = activityFile;

        // Traffic: its instances are new objects after undo; the activity
        // finds them again.
        QVector<ActivityServiceDefinition*> instances = activity->getServiceList();
        ActivityServiceDefinition *ai = nullptr;
        for (ActivityServiceDefinition *s : instances)
            if (!s->player)
                ai = s;
        check(ai && ai->trafficDefinition == keys->getInstance("a1", 23), "the AI instance points at its timetable");
        Undo::StateBegin();
        Undo::PushTrafficData(keys);
        keys->removeInstance(0);
        Undo::StateEnd();
        Undo::UndoLast();
        check(ai && ai->trafficDefinition == nullptr && keys->service.size() == 2 && !keys->isModified(),
              "undo restores the traffic; activities let go of its old instances");
        activity->getServiceList();
        check(ai && ai->trafficDefinition == keys->getInstance("a1", 23), "and find the new ones");

        // Services restore in place.
        Service *stops = ActLib::Services.value(ActLib::AddService(route + "/SERVICES", "Stops.srv"));
        if (stops) {
            const QString text = stops->toText();
            Undo::StateBegin();
            Undo::PushServiceData(stops);
            stops->disableStationStop(1);
            stops->setDisplayName("Changed");
            Undo::StateEnd();
            Undo::UndoLast();
            check(stops->toText() == text && stops->stationStop.size() == 2 && !stops->isModified(),
                  "undo restores a service");
        } else {
            check(false, "the service loads");
        }
        QObject::disconnect(before);
        QObject::disconnect(after);
    }
    Undo::Clear();
    for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
    for (int id = firstPath; id < ActLib::jestpath; ++id)
        ActLib::Paths[id] = nullptr;
    qInfo().noquote() << QString("activity-edit: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
