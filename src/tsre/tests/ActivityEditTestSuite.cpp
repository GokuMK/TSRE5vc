#include <tsre/tests/ActivityEditTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <routeEditor/activity/ActivityEdit.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityCheck.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ActivityOverlay.h>
#include <tsre/trains/ActivityTrains.h>
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

// TSRE's older forms: a sound and a weather preset in the outcomes, each
// with a copy at event level; and an event-level sound of its own.
const char *legacyActivity = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Legacy" )
		StartTime ( 8 0 0 )
		Duration ( 1 0 )
	)
	Tr_Activity_File (
		NextServiceUID ( 1 )
		NextActivityObjectUID ( 32768 )
		Events (
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					ORTSActSoundFile ( "a.wav" Cab )
					ORTSWeatherChange ( "rain" )
				)
				Name ( "Older" )
				Time ( 60 )
				ORTSActSoundFile ( "a.wav" Cab )
				ORTSWeatherChange (
					Name ( "rain" )
					ORTSOvercast ( 0.9 60 )
					ORTSFog ( 300 120 )
				)
			)
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 2 )
				Activation_Level ( 1 )
				Outcomes ( )
				Name ( "Own" )
				Time ( 90 )
				ORTSActSoundFile ( "b.wav" Everywhere )
			)
		)
	)
)
)";

// An activity and its Open Rails overlay: the horn, an event's outcomes
// and ORTSContinue added; the horn pattern the same; an event of another
// kind and one the activity has not left out.
const char *overlaidActivity = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Overlaid" )
		StartTime ( 8 0 0 )
		Duration ( 1 0 )
	)
	Tr_Activity_File (
		NextServiceUID ( 1 )
		NextActivityObjectUID ( 32768 )
		ORTSAICrossingHornPattern ( US )
		Events (
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					DisplayMessage ( "Base" )
				)
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

const char *overlayText = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Tr_Activity_File (
		ORTSAIHornAtCrossings ( 1 )
		ORTSAICrossingHornPattern ( US )
		Events (
			EventCategoryTime (
				ID ( 1 )
				Outcomes (
					DisplayMessage ( "Overlay" )
					ORTSWeatherChange (
						ORTSOvercast ( 0.5 30 )
					)
				)
				ORTSContinue ( 10 )
			)
			EventCategoryLocation (
				ID ( 2 )
			)
			EventCategoryTime (
				ID ( 9 )
				Time ( 30 )
			)
		)
	)
)
)";

// A traffic pattern two activities share: Stops at 10:00 (in A), at 8:20
// (in B only), and at 11:06:40, named STOPS (A names it stops).
const char *sharedTraffic = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( Shared
	Serial ( 1 )
	Service_Definition ( Stops 36000
		ArrivalTime ( 36100 )
		DepartTime ( 36160 )
		SkipCount ( 1 )
		DistanceDownPath ( 100 )
		PlatformStartID ( 12 )
		ArrivalTime ( 36900 )
		DepartTime ( 36900 )
		SkipCount ( 2 )
		DistanceDownPath ( 900 )
		PlatformStartID ( 14 )
	)
	Service_Definition ( Stops 30000
		ArrivalTime ( 30100 )
		DepartTime ( 30160 )
		SkipCount ( 1 )
		DistanceDownPath ( 100 )
		PlatformStartID ( 12 )
	)
	Service_Definition ( STOPS 40000
		ArrivalTime ( 40100 )
		DepartTime ( 40160 )
		SkipCount ( 1 )
		DistanceDownPath ( 100 )
		PlatformStartID ( 12 )
	)
)
)";

QString trainsActivity(const QString &name, const QString &trains) {
    return QString(R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "%1" )
		StartTime ( 8 0 0 )
		Duration ( 1 0 )
	)
	Tr_Activity_File (
		Player_Service_Definition ( Stops
			Player_Traffic_Definition ( 28800
				ArrivalTime ( 28900 )
				DepartTime ( 28960 )
				SkipCount ( 1 )
				DistanceDownPath ( 100 )
				PlatformStartID ( 12 )
			)
			UiD ( 0 )
			Efficiency ( 0.9 )
			SkipCount ( 1 )
			DistanceDownPath ( 100 )
			PlatformStartID ( 12 )
		)
		NextServiceUID ( 3 )
		NextActivityObjectUID ( 32768 )
		Traffic_Definition ( Shared
%2		)
	)
)
)").arg(name, trains);
}

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
          && writeSimis(route + "/ACTIVITIES/Undo.act", activityText)
          && writeSimis(route + "/ACTIVITIES/Legacy.act", legacyActivity)
          && writeSimis(route + "/ACTIVITIES/Overlaid.act", overlaidActivity)
          && QDir().mkpath(route + "/ACTIVITIES/OPENRAILS")
          && writeSimis(route + "/ACTIVITIES/OPENRAILS/Overlaid.act", overlayText)
          && writeSimis(route + "/ACTIVITIES/OPENRAILS/legacy.ACT", overlayText)
          && writeSimis(route + "/TRAFFIC/Shared.trf", sharedTraffic)
          && writeSimis(route + "/ACTIVITIES/TrainsA.act", trainsActivity("Trains A",
                 "\t\t\tService_Definition ( Stops 36000\n\t\t\t\tUiD ( 1 )\n\t\t\t)\n"
                 "\t\t\tService_Definition ( stops 40000\n\t\t\t\tUiD ( 2 )\n\t\t\t)\n"))
          && writeSimis(route + "/ACTIVITIES/TrainsB.act", trainsActivity("Trains B",
                 "\t\t\tService_Definition ( Stops 30000\n\t\t\t\tUiD ( 1 )\n\t\t\t)\n")),
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
    // TSRE's older forms become Open Rails' outcomes; its event-level copies
    // go; an event-level form of its own stays as read.
    {
        Activity legacy(route + "/ACTIVITIES/Legacy.act", route + "/ACTIVITIES", QString("Legacy.act"));
        const ActivityEvent *older = legacy.event.value(0), *own = legacy.event.value(1);
        const QString text = legacy.toText();
        check(older && older->ortsActSoundFileDeprecated.isEmpty() && older->ortsWeatherChangeDeprecated == nullptr,
              "TSRE's event-level copies of its outcomes are dropped");
        const ActivityEvent::Outcome *weather = older ? older->outcomes.value(1) : nullptr;
        check(weather && weather->value.toString().isEmpty() && weather->weather[0] == 0.9f
              && weather->weatherTime[1] == 120,
              "a weather preset takes its values from its copy");
        check(text.count("ORTSActivitySound") == 1 && text.count("ORTSActSoundFile") == 2
              && text.count("ORTSWeatherChange") == 1 && text.contains("ORTSOvercast ( 0.9 60 )"),
              "written in Open Rails' outcome forms; the other event's own sound as read");
        check(own && own->ortsActSoundFileDeprecated == QVector<QString>({"b.wav", "Everywhere"}),
              "an event-level sound of its own stays");
    }
    // Open Rails' additions are written only when set.
    {
        Activity fresh(route + "/ACTIVITIES", "fresh.act", true);
        fresh.init("TEST", "fresh");
        ActivityEvent *event = fresh.newEvent(ActivityEvent::CategoryLocation);
        const QString text = fresh.toText();
        check(!text.contains("FuelWood") && !text.contains("ORTS") && !text.contains("Reversible"),
              "a new activity has no Open Rails attributes");
        fresh.setFuelWood(50);
        fresh.setOrtsHornPattern("Single");
        event->setTriggeringTrain("AI", -1);
        const QString set = fresh.toText();
        check(set.contains("FuelWood ( 50 )") && set.contains("ORTSAICrossingHornPattern ( Single )")
              && set.contains("ORTSTriggeringTrain ( AI )") && set.indexOf("ORTSAICrossingHornPattern") > set.indexOf("NextActivityObjectUID"),
              "set, they are written; the horn pattern after NextActivityObjectUID");
    }

    // An Open Rails overlay: found by the activity's file name in
    // OPENRAILS, not in another letter case (that is for the content-case
    // converter), shown before it is merged, merged as one undo step,
    // renamed when the activity is saved.
    {
        Activity *overlaid = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "Overlaid.act"));
        const QString overlay = route + "/ACTIVITIES/OPENRAILS/Overlaid.act";
        Activity *legacy = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "Legacy.act"));
        // A filesystem that ignores letter case (Windows) finds legacy.ACT
        // for Legacy.act itself: the check holds where it does not (Linux).
        const bool caseIgnored = QFileInfo(route + "/ACTIVITIES/OPENRAILS/LEGACY.ACT").isFile();
        check(overlaid && overlaid->loaded == 1 && ActivityOverlay::PathOf(overlaid) == overlay && legacy != nullptr
                  && ActivityOverlay::PathOf(legacy).isEmpty(),
              QString("the overlay is found by the activity's file name, legacy.ACT for Legacy.act not")
                  + (caseIgnored ? " (this filesystem ignores letter case, as Windows does: it finds legacy.ACT "
                                   "for Legacy.act itself; the check needs a case-sensitive one, as on Linux)"
                                 : ""));
        const auto codes = [](Activity *checked) {
            QStringList found;
            for (const ActivityFinding &finding : CheckActivity(checked))
                found << finding.code;
            return found;
        };
        if (overlaid) {
            const QString original = overlaid->toText();
            overlaid->save();
            check(QFile::exists(overlay) && !QFile::exists(overlay + ".merged"),
                  "a save without a merge leaves the overlay alone");
            const QStringList found = codes(overlaid);
            check(found.contains("overlay.present") && found.count("overlay.skipped") == 3,
                  "Check Activity notes the overlay and what a merge leaves out: " + found.join(", "));
            const ActivityOverlay::Result preview = ActivityOverlay::Merge(overlaid, overlay, false);
            check(preview.readable && preview.added.size() == 4 && preview.skipped.size() == 3
                  && overlaid->toText() == original && !overlaid->isUnSaved(),
                  QString("the preview lists 4 additions, 3 left out, and changes nothing: %1 | %2")
                  .arg(preview.added.join("; "), preview.skipped.join("; ")));
            {
                ActivityEdit edit(overlaid);
                ActivityOverlay::Merge(overlaid, overlay, true);
                overlaid->mergedOverlay = overlay;
            }
            const QString merged = overlaid->toText();
            const ActivityEvent *one = overlaid->event.value(0);
            check(overlaid->isUnSaved() && merged.contains("ORTSAIHornAtCrossings ( 1 )")
                  && merged.contains("ORTSContinue ( 10 )") && merged.contains("DisplayMessage ( Overlay )") && !merged.contains("Base")
                  && merged.contains("ORTSOvercast ( 0.5 30 )") && one && one->outcomes.size() == 2
                  && merged.count("ORTSAICrossingHornPattern") == 1,
                  "merged: the horn, ORTSContinue, the message replaced, the weather added");
            check(!codes(overlaid).contains("overlay.present"), "a merged overlay is no longer noted");
            check(ActivityOverlay::Merge(overlaid, overlay, false).added.isEmpty(), "merging again adds nothing");
            Undo::UndoLast();
            check(overlaid->toText() == original && overlaid->mergedOverlay.isEmpty() && !overlaid->isUnSaved(),
                  "undo takes the merge out");
            {
                ActivityEdit edit(overlaid);
                ActivityOverlay::Merge(overlaid, overlay, true);
                overlaid->mergedOverlay = overlay;
            }
            QString renamed;
            QMetaObject::Connection told = QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::overlayRenamed,
                                                            [&](const QString &path) { renamed = path; });
            overlaid->save();
            QObject::disconnect(told);
            check(!QFile::exists(overlay) && QFile::exists(overlay + ".merged") && renamed == overlay
                  && overlaid->mergedOverlay.isEmpty() && ActivityOverlay::PathOf(overlaid).isEmpty(),
                  "saving renames the overlay to .merged");
            Activity reread(route + "/ACTIVITIES/Overlaid.act", route + "/ACTIVITIES", QString("Overlaid.act"));
            check(reread.toText() == overlaid->toText(), "the merged activity reads back as saved");
            Undo::UndoLast();
            check(overlaid->mergedOverlay.isEmpty() && overlaid->isUnSaved(),
                  "undo after the save leaves the renamed overlay alone");
        }
    }

    // The activity's trains (the Timetable window's rows): by start time,
    // the pattern's runs it lacks among them; adding, removing, an own copy
    // of the shared pattern.
    {
        ActLib::AddService(route + "/SERVICES", "Stops.srv");
        Traffic *shared = ActLib::Traffics.value(ActLib::AddTraffic(route + "/TRAFFIC", "Shared.trf"));
        Activity *a = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "TrainsA.act"));
        Activity *other = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "TrainsB.act"));
        check(shared && shared->service.size() == 3 && a && a->loaded == 1 && other && other->loaded == 1,
              "a shared pattern and its two activities load");
        if (shared && a && other) {
            using ActivityTrains::Train;
            const auto kinds = [](const QVector<Train> &trains) {
                QString text;
                for (const Train &train : trains)
                    text += QString("%1:%2:%3 ").arg(int(train.kind)).arg(train.service).arg(train.time);
                return text.trimmed();
            };
            QVector<Train> trains = ActivityTrains::List(a);
            check(kinds(trains) == "0:Stops:28800 2:Stops:30000 1:Stops:36000 1:stops:40000",
                  "the player's, the pattern's run it lacks, its AI trains, by start time: " + kinds(trains));
            const Train *ai = trains.size() == 4 ? &trains[2] : nullptr;
            check(ai && ai->timetable == shared->getInstance("Stops", 36000) && ai->path == "Other"
                  && ai->firstStop.endsWith("10:02:40") && ai->lastStop.endsWith("10:15:00")
                  && trains[1].lastStop.isEmpty()
                  && !ai->problems.contains(qtTrId("activity.trains.name.case").arg("Stops")),
                  "an AI train's times are the pattern's run; its first departure and last arrival");
            check(trains.size() == 4 && trains[3].problems.contains(qtTrId("activity.trains.name.case").arg("STOPS"))
                  && trains[1].timetable == shared->service.value(1),
                  "a name differing in case from the pattern's is a problem: " + (trains.size() == 4 ? trains[3].problems.join("; ") : QString()));
            check(ActivityTrains::OtherUsers(a).size() == 1 && ActivityTrains::OtherUsers(a).first().startsWith("TrainsB"),
                  "the pattern's other user");
            QStringList codes;
            for (const ActivityFinding &finding : CheckActivity(a))
                codes << finding.code;
            check(codes.contains("instance.name.case"), "Check Activity warns of the name's case");

            // Remove and add, each one undo step.
            const QString original = a->toText();
            {
                ActivityEdit edit(a);
                a->removeAITrain(0);
            }
            trains = ActivityTrains::List(a);
            check(a->isUnSaved() && kinds(trains) == "0:Stops:28800 2:Stops:30000 2:Stops:36000 1:stops:40000",
                  "a removed AI train is the pattern's run again: " + kinds(trains));
            Undo::UndoLast();
            check(a->toText() == original && !a->isUnSaved(), "undo puts it back");
            {
                ActivityEdit edit(a);
                a->addAITrain(shared->service[1]);
            }
            trains = ActivityTrains::List(a);
            const QString added = a->toText();
            check(kinds(trains) == "0:Stops:28800 1:Stops:30000 1:Stops:36000 1:stops:40000"
                  && added.contains("Service_Definition ( Stops 30000") && added.contains("UiD ( 3 )")
                  && added.contains("NextServiceUID ( 4 )"),
                  "an added run is an AI train with the next UiD: " + kinds(trains));
            Undo::UndoLast();
            check(a->toText() == original, "undo takes it out");

            // Map and 3D: the chosen train first, a service once.
            a->shownAITrain = 1;
            const QVector<int> chosenOnly = a->shownAITrains();
            a->showAllAITrains = true;
            const QVector<int> all = a->shownAITrains();
            a->showAllAITrains = false;
            a->shownAITrain = -1;
            check(chosenOnly == QVector<int>({1}) && all == QVector<int>({1}) && a->shownAITrains().isEmpty()
                  && a->aiTrainService(1) != nullptr && a->aiTrainService(1) == a->aiTrainService(0),
                  "the chosen AI train is shown; all of them shows each service once");

            // An own copy: the same runs, the activity naming it; the shared
            // pattern untouched.
            Traffic *copy = nullptr;
            {
                ActivityEdit edit(a);
                copy = ActivityTrains::CopyPattern(a, "Own");
            }
            trains = ActivityTrains::List(a);
            check(copy && copy->service.size() == 3 && a->traffic->name == "Own" && ActivityTrains::Pattern(a) == copy
                  && trains.size() == 4 && copy->service.contains(trains[2].timetable)
                  && ActivityTrains::OtherUsers(a).isEmpty() && !shared->isModified() && copy->isModified()
                  && copy->toText().contains("Traffic_Definition ( Own") && copy->toText().contains("Service_Definition ( Stops 36000"),
                  "Make Own Copy: a new pattern with the runs, named by the activity");
            check(ActivityTrains::CopyPattern(a, "Shared") == nullptr, "a taken name makes no copy");
            Undo::UndoLast();
            check(a->traffic->name == "Shared" && ActivityTrains::Pattern(a) == shared && a->toText() == original,
                  "undo names the shared pattern again");
        }
    }

    Undo::Clear();
    for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
    for (int id = firstPath; id < ActLib::jestpath; ++id)
        ActLib::Paths[id] = nullptr;
    qInfo().noquote() << QString("activity-edit: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
