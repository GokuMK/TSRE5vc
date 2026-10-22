#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <algorithm>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>

namespace {

// A timetable's text written as a file: UTF-16 with its byte order mark,
// UTF-8 with or without one.
enum class Encoding { Utf16, Utf8Bom, Utf8 };

bool writeFile(const QString &path, const QString &text, Encoding encoding) {
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (encoding == Encoding::Utf16) {
        out.write("\xFF\xFE");
        QStringEncoder encoder(QStringConverter::Utf16LE);
        out.write(encoder.encode(text));
    } else {
        if (encoding == Encoding::Utf8Bom)
            out.write("\xEF\xBB\xBF");
        out.write(text.toUtf8());
    }
    return true;
}

// A path file (its nodes laid on the test track after), a consist of an
// engine and a carriage of 20 m each (no mass: driven simply), the
// vehicles.
const char *pathText = R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 100 0 0 2 0 )
	TrackPDP ( 0 0 1900 0 0 2 0 )
)
TrackPath (
	TrPathName ( %1 )
	Name ( "%1" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 2
		TrPathNode ( 00000000 1 4294967295 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
	)
)
)";

const char *consistText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Train (
	TrainCfg ( "Two"
		Name ( "Two" )
		Serial ( 1 )
		MaxVelocity ( 20 0.5 )
		NextWagonUID ( 2 )
		Durability ( 1 )
		Engine (
			UiD ( 0 )
			EngineData ( loco TEST )
		)
		Wagon (
			UiD ( 1 )
			WagonData ( car TEST )
		)
	)
)
)";

// What a timetable says, as one text: its description, stations and
// their commands, every train's cells as read and what they say (a train
// static by its column's head named after its column, which moves: not its
// name).
QString fingerprint(const OrTimetable &t) {
    QStringList out{t.description};
    const auto commands = [](const QVector<OrTimetable::Command> &list) {
        QStringList texts;
        for (const OrTimetable::Command &c : list)
            texts << c.text;
        return texts.join(" | ");
    };
    for (const OrTimetable::Station &station : t.stations)
        out << "station " + station.name + " [" + commands(station.commands) + "]";
    for (const OrTimetable::Train &train : t.trains) {
        out << QString("train %1 static %2 comment %3 path %4 (%5) consist %6 start %7 (%8) [%9] notes [%10] "
                       "dispose [%11] speed %12 (%13) delay %14 briefing %15")
                   .arg(train.header.startsWith('$') ? QString("(static)") : train.name).arg(train.isStatic).arg(train.comment, train.path, train.pathText, train.consist,
                                                             train.start).arg(train.startTime)
                   .arg(commands(train.startCommands), commands(train.notes), commands(train.dispose))
                   .arg(train.speed).arg(train.speedText, train.restartDelay, train.briefing);
        for (const OrTimetable::Stop &stop : train.stops)
            out << QString("  stop %1 %2-%3 P%4 %5%6 [%7] %8")
                       .arg(t.stations[stop.station].name).arg(stop.timing.arrival).arg(stop.timing.departure)
                       .arg(stop.timing.passing).arg(stop.timing.early).arg(stop.timing.request)
                       .arg(commands(stop.commands), stop.text);
    }
    return out.join("\n");
}

const char *vehicleText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( %1
	Type ( %2 )
	Size ( 3m 4m 20m )
	WagonShape ( missing.s )
)
)";

}

int TsreTests::runTimetableReadSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-read] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-read] FAIL" << name; }
    };
    QTemporaryDir dir;
    check(dir.isValid(), "a scratch folder");

    // The rows and columns Open Rails knows, in UTF-16 with ';' (as
    // PRUDNIK's): a description, a static train, consists of several parts,
    // start commands with qualifiers, notes, dispose, a speed row in km/h,
    // station commands, every kind of timing, a continuation column and a
    // continuation row.
    const QStringList rows = {
        ";#comment;T1;;$static;T2;T3",
        "#comment;Test timetable;;;;;",
        "#path /binary;;p1;;ps;p2;p3",
        "#consist;;c1 + c2 $reverse;;cs;<a+b>$reverse + c3;c1",
        "#start;;05:00 $create=04:30 /ahead=T2;;;05:10:30;06:00 $next",
        "#note;;$acc=2 $dec=1.5;;;$follow=T1 /maxdelay=5;",
        "#dispose;;$static $triggers=T2;;;$forms=T3 /runround=rr /rrtime=07:00;",
        "#speedkph;;$max=90;;;;",
        "#briefing;;one<br>two;;;;",
        "A $hold $nowaitsignal;;05:10-05:12;$closeupsignal;;05P20;x",
        "B;;05:30;;;05*30;06x40",
        ";;$forcewait;;;;",
        "#bogus;;;;;;",
        "C;;bad;;;12:00;",
    };
    const QString text = rows.join("\r\n") + "\r\n";
    const QString utf16 = dir.filePath("test.timetable-or");
    writeFile(utf16, text, Encoding::Utf16);
    OrTimetable t;
    const bool loaded = t.load(utf16);
    check(loaded && t.separator == ';' && t.description == "Test timetable", "read in UTF-16 with ';': the description");
    check(t.trains.size() == 4 && t.stations.size() == 3, QString("4 trains, 3 stations (%1, %2)").arg(t.trains.size()).arg(t.stations.size()));
    const OrTimetable::Train *t1 = t.trainNamed("t1") >= 0 ? &t.trains[t.trainNamed("t1")] : nullptr;
    check(t1 != nullptr && t1->path == "p1" && t1->consists.size() == 2 && t1->consists[0].name == "c1"
              && !t1->consists[0].reverse && t1->consists[1].name == "c2" && t1->consists[1].reverse,
          "a consist of two parts, the second reversed");
    check(t1 != nullptr && t1->startTime == 5 * 3600 && t1->startCommands.size() == 1 && t1->startCommands[0].name == "create"
              && t1->startCommands[0].values == QStringList({"04:30"}) && t1->startCommands[0].qualifier("ahead") == QStringList({"t2"}),
          "the start time and $create=04:30 /ahead=T2");
    check(t1 != nullptr && t1->notes.size() == 2 && t1->notes[0].name == "acc" && t1->notes[1].values == QStringList({"1.5"}),
          "notes: $acc and $dec with their values");
    check(t1 != nullptr && t1->dispose.size() == 2 && t1->dispose[1].name == "triggers" && t1->dispose[1].values == QStringList({"t2"}),
          "dispose: $static $triggers=T2");
    check(t1 != nullptr && std::fabs(t1->speed - 25.0f) < 1e-3f && t1->briefing == "one\ntwo", "the speed row in km/h (90 = 25 m/s); the briefing's breaks");
    // The continuation column after T1 adds to its cells.
    const OrTimetable::Stop *atA = t1 != nullptr ? t1->stopAt(0) : nullptr;
    check(atA != nullptr && atA->timing.arrival == 5 * 3600 + 600 && atA->timing.departure == 5 * 3600 + 720
              && atA->commands.size() == 1 && atA->commands[0].name == "closeupsignal",
          "a stop's arrival and departure; a continuation column's command added to it");
    check(t.stations.size() == 3 && t.stations[0].name == "A" && t.stations[0].commands.size() == 2
              && t.stations[0].commands[0].name == "hold" && t.stations[0].commands[1].name == "nowaitsignal",
          "a station's commands");
    const OrTimetable::Stop *atB = t1 != nullptr ? t1->stopAt(1) : nullptr;
    check(atB != nullptr && atB->timing.arrival == 5 * 3600 + 1800 && atB->timing.departure == atB->timing.arrival
              && atB->commands.size() == 1 && atB->commands[0].name == "forcewait",
          "one time: arrival and departure; a continuation row's command added to the stop");
    const OrTimetable::Train *s = t.trains.size() > 1 ? &t.trains[1] : nullptr;
    check(s != nullptr && s->isStatic && s->name == "S4" && s->start == "$static" && s->path == "ps",
          QString("a train named $static: static, named after its column (%1)").arg(s != nullptr ? s->name : QString()));
    const OrTimetable::Train *t2 = t.trainNamed("T2") >= 0 ? &t.trains[t.trainNamed("T2")] : nullptr;
    check(t2 != nullptr && t2->path == "p2" && t2->startTime == 5 * 3600 + 630 && t2->consists.size() == 2
              && t2->consists[0].name == "a+b" && t2->consists[0].reverse && t2->consists[1].name == "c3",
          "a start with seconds; <a+b>$reverse + c3");
    const OrTimetable::Stop *passA = t2 != nullptr ? t2->stopAt(0) : nullptr;
    const OrTimetable::Stop *earlyB = t2 != nullptr ? t2->stopAt(1) : nullptr;
    check(passA != nullptr && passA->timing.passing == 5 * 3600 + 1200 && passA->timing.arrival < 0
              && earlyB != nullptr && earlyB->timing.early && earlyB->timing.arrival == 5 * 3600 + 1800,
          "a passing time (05P20) and may depart early (05*30)");
    const OrTimetable::Train *t3 = t.trainNamed("T3") >= 0 ? &t.trains[t.trainNamed("T3")] : nullptr;
    const OrTimetable::Stop *requestA = t3 != nullptr ? t3->stopAt(0) : nullptr;
    const OrTimetable::Stop *requestB = t3 != nullptr ? t3->stopAt(1) : nullptr;
    check(requestA != nullptr && requestA->timing.request && requestA->timing.arrival < 0 && requestB != nullptr
              && requestB->timing.request && requestB->timing.arrival == 6 * 3600 + 2400
              && t3->startCommands.size() == 1 && t3->startCommands[0].name == "next",
          "request stops (x, 06x40); $next");
    check(t.problems.size() == 2 && t.problems[0].contains("#bogus") && t.problems[1].contains("bad"),
          "an unknown row and a timing that is no time reported: " + t.problems.join("; "));

    // UTF-8 with ',' and with a tab (and its byte order mark).
    {
        const QString comma = dir.filePath("comma.timetable_or");
        writeFile(comma, ",X,Y\n#path,px,py\n#consist,cx,cy\n#start,07:00,08:00\nA,07:10,08:10\n", Encoding::Utf8);
        OrTimetable c;
        const QString tab = dir.filePath("tab.timetable-or");
        writeFile(tab, "\tX\n#path\tpx\n#consist\tcx\n#start\t07:00\nA\t07:10\n", Encoding::Utf8Bom);
        OrTimetable d;
        check(c.load(comma) && c.separator == ',' && c.trains.size() == 2 && c.trains[1].stops.size() == 1
                  && d.load(tab) && d.separator == '\t' && d.trains.size() == 1,
              "UTF-8 with ',', and with a tab after a byte order mark");
        const QString wrong = dir.filePath("wrong.timetable-or");
        writeFile(wrong, "X;Y\n", Encoding::Utf8);
        OrTimetable w;
        const bool read = w.load(wrong);
        check(!read && !w.problems.isEmpty(), "a first cell not empty: not a timetable (" + w.problems.join("; ") + ")");
    }

    // The writer (phase 6): written in TSRE's layout and read again, the
    // same timetable; edits written; saving keeps the original once.
    {
        OrTimetable again;
        const QString written = t.toText();
        check(again.loadText(written) && fingerprint(again) == fingerprint(t) && t.trains.size() == 4,
              "written in TSRE's layout and read again: the same timetable");
        OrTimetable edited = t;
        const int t1 = edited.trainNamed("T1");
        edited.setStartTime(t1, 5 * 3600 + 300);
        edited.setStopTimes(t1, 0, 5 * 3600 + 660, 5 * 3600 + 900);
        OrTimetable back;
        back.loadText(edited.toText());
        const int b1 = back.trainNamed("T1");
        check(edited.modified && b1 >= 0 && back.trains[b1].startTime == 5 * 3600 + 300
                  && back.trains[b1].startCommands.size() == 1 && back.trains[b1].stops[0].timing.arrival == 5 * 3600 + 660
                  && back.trains[b1].stops[0].timing.departure == 5 * 3600 + 900 && back.trains[b1].stops[0].commands.size() == 1
                  && back.trains[b1].stops[0].text == "05:11-05:15 $closeupsignal",
              "edits written: T1's start 05:05 ($create kept), its stop at A 05:11-05:15: " + edited.trains[t1].start);
        const QString copy = dir.filePath("copy.timetable-or");
        QFile::copy(utf16, copy);
        QFile original(utf16);
        original.open(QIODevice::ReadOnly);
        const QByteArray bytes = original.readAll();
        OrTimetable saved;
        saved.load(copy);
        saved.setStopTimes(saved.trainNamed("T2"), 0, 5 * 3600 + 1260, 5 * 3600 + 1260);
        QString error;
        const bool first = saved.save(&error);
        saved.save(&error);
        QFile kept(copy + ".original");
        kept.open(QIODevice::ReadOnly);
        OrTimetable reread;
        reread.load(copy);
        check(first && !saved.modified && kept.readAll() == bytes && fingerprint(reread) == fingerprint(saved),
              "saved: the original kept once as .original, the file read again the same " + error);
    }

    // A timetable group: its name and timetables.
    {
        const QString list = dir.filePath("all.timetablelist-or");
        writeFile(list, "#All services\r\none.timetable-or\r\ntwo.timetable-or\r\n", Encoding::Utf8);
        OrTimetableGroup group;
        check(group.load(list) && group.name == "All services"
                  && group.timetables == QStringList({"one.timetable-or", "two.timetable-or"}),
              "a timetable group: its name and two timetables");
    }

    // The check: a path and a consist not there, a command not run yet, a
    // train referred to that is not in it. A path by the route's path names
    // as the simulation finds it (in any case; a cell naming the file, or
    // ending in a '.', is the path: Open Rails adds ".pat" where there is no
    // extension); a consist by its file's name as spelled (C2.con is not
    // c2: another case is for the content-case converter).
    {
        QDir root(dir.path());
        root.mkpath("route/PATHS");
        root.mkpath("TRAINS/CONSISTS");
        writeFile(dir.filePath("route/PATHS/p1.pat"), QString(pathText).arg("p1"), Encoding::Utf16);
        writeFile(dir.filePath("TRAINS/CONSISTS/c1.con"), "x", Encoding::Utf8);
        writeFile(dir.filePath("TRAINS/CONSISTS/C2.con"), "x", Encoding::Utf8);
        const int path = ActLib::AddPath(dir.filePath("route/PATHS"), "p1.pat");
        const QString file = dir.filePath("check.timetable-or");
        writeFile(file,
                  ";T1;T2;T3;T4\n#path;p1;p9;P1.pat;p1.\n#consist;c1;c1 + c9;c1;c2\n#start;05:00;06:00;07:00;08:00\n"
                  "#note;$attach=T1;$follow=T7\n",
                  Encoding::Utf8);
        OrTimetable c;
        c.load(file);
        const QVector<OrTimetableProblem> found
                = CheckTimetable(c, dir.filePath("route"), dir.filePath("TRAINS/CONSISTS"), nullptr);
        QStringList texts;
        for (const OrTimetableProblem &p : found)
            texts << p.train + ": " + p.text;
        check(found.size() == 5 && texts.join("\n").contains("path p9") && texts.join("\n").contains("consist c9")
                  && texts.join("\n").contains("T4: consist c2") && texts.join("\n").contains("$attach")
                  && texts.join("\n").contains("t7") && c.trains.size() == 4 && c.trains[2].pathName() == "P1"
                  && c.trains[3].pathName() == "p1",
              "the check (P1.pat and p1. the path p1, consist c2 not C2.con): " + texts.join("; "));
        ActLib::Paths[path] = nullptr;
    }

    // A timetable set as an activity (phase 2): its name the timetable's
    // description, its start the first running train's; its trains listed
    // as the editor and the simulation see an activity's, the one chosen
    // the player's.
    {
        const int id = ActLib::AddTimetableSet(dir.path(), "test.timetable-or");
        Activity *set = ActLib::TimetableSets.value(id);
        check(set != nullptr && set->isTimetables() && set->loaded == 1 && set->header != nullptr
                  && set->header->name == "Test timetable" && set->header->startTime[0] == 5
                  && set->header->startTime[1] == 0 && set->timetables.size() == 1 && !set->isUnSaved(),
              "a timetable set as an activity: its name and the first train's start (05:00)");
        if (set != nullptr) {
            set->playerTrain = "t2";
            const QVector<ActivityTrains::Train> trains = ActivityTrains::List(set);
            QStringList listed;
            for (const ActivityTrains::Train &train : trains)
                listed << QString("%1%2%3").arg(train.service, train.kind == ActivityTrains::Player ? "*" : "",
                                                train.staticTrain ? "(s)" : "");
            const ActivityTrains::Train *t1 = nullptr;
            for (const ActivityTrains::Train &train : trains)
                if (train.service == "T1")
                    t1 = &train;
            check(trains.size() == 4 && listed.join(" ") == "S4(s) T1 T2* T3" && t1 != nullptr
                      && t1->firstStop == "A 05:12:00" && t1->lastStop == "B 05:30:00" && t1->path == "p1",
                  "its trains in start order, the static one first, the chosen one the player's: " + listed.join(" "));
            check(t1 != nullptr && !t1->problems.isEmpty() && t1->problems.join(" ").contains("p1"),
                  "a train's problems from the check: " + (t1 != nullptr ? t1->problems.join("; ") : QString()));
            delete set;
            ActLib::TimetableSets.remove(id);
        }
        writeFile(dir.filePath("two.timetable-or"), ";X;Y\n#path;px;py\n#consist;cx;cy\n#start;04:10;04:20\n", Encoding::Utf8);
        writeFile(dir.filePath("both.timetablelist-or"), "#Both\ntest.timetable-or\ntwo.timetable-or\n", Encoding::Utf8);
        const int group = ActLib::AddTimetableSet(dir.path(), "both.timetablelist-or");
        Activity *both = ActLib::TimetableSets.value(group);
        check(both != nullptr && both->header->name == "Both" && both->timetables.size() == 2
                  && ActivityTrains::List(both).size() == 6 && both->header->startTime[0] == 4 && both->header->startTime[1] == 10,
              "a group as an activity: the trains of its two timetables, the first start 04:10");
        delete both;
        ActLib::TimetableSets.remove(group);
    }

    qInfo().noquote() << QString("timetable-read: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runTimetableRouteSuite(bool verbose) {
    const QString routeDirectory = Game::root + "/ROUTES/" + Game::route;
    const QDir sourceRoute(routeDirectory);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "timetable-route requires a route with one TDB (--game-root, --route)";
        return 1;
    }
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-route] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-route] FAIL" << name; }
    };
    const QScopedValueRollback<QString> routeName(Game::routeName, databases.first().completeBaseName());
    TSectionDAT sections(false, false);
    if (QFileInfo::exists(sourceRoute.filePath("tsection.dat")))
        sections.loadRoute(false);
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded)
        return 1;
    const QString folder = routeDirectory + "/ACTIVITIES/OPENRAILS";
    const QString consists = Game::root + "/TRAINS/CONSISTS";
    // The route's paths, as the editor loads them, for the check.
    const int firstPath = ActLib::jestpath;
    for (const QString &entry : QDir(routeDirectory + "/PATHS").entryList({"*.pat"}, QDir::Files))
        ActLib::AddPath(routeDirectory + "/PATHS", entry);
    QStringList files;
    for (const QString &entry : QDir(folder).entryList(QDir::Files))
        if (entry.endsWith(".timetable-or", Qt::CaseInsensitive) || entry.endsWith(".timetable_or", Qt::CaseInsensitive))
            files << entry;
    const auto forgetPaths = [&] {
        for (int id = firstPath; id < ActLib::jestpath; ++id)
            ActLib::Paths[id] = nullptr;
    };
    if (files.isEmpty()) {
        qInfo().noquote() << "[tests:timetable-route] the route has no Open Rails timetables";
        forgetPaths();
        return 0;
    }
    check(true, QString("%1 timetables in %2").arg(files.size()).arg(folder));
    for (const QString &name : files) {
        QElapsedTimer timer;
        timer.start();
        OrTimetable timetable;
        const bool loaded = timetable.load(folder + "/" + name);
        const qint64 ms = timer.elapsed();
        int statics = 0, stops = 0, speeds = 0;
        for (const OrTimetable::Train &train : timetable.trains) {
            statics += train.isStatic ? 1 : 0;
            stops += int(train.stops.size());
            speeds += train.speed > 0.0f ? 1 : 0;
        }
        check(loaded && !timetable.trains.isEmpty(),
              QString("%1 (%2) read in %3 ms: %4 trains (%5 static, %6 with a top speed), %7 stations, %8 stops; "
                      "%9 problems reading it")
                  .arg(name, timetable.description).arg(ms).arg(timetable.trains.size()).arg(statics).arg(speeds)
                  .arg(timetable.stations.size()).arg(stops).arg(timetable.problems.size()));
        // Written in TSRE's layout (phase 6) and read again: the same
        // timetable. TSRE_TIMETABLE_WRITE=<folder>: also written there (for
        // Open Rails; never into the route).
        {
            OrTimetable again;
            const QString written = timetable.toText();
            check(again.loadText(written) && fingerprint(again) == fingerprint(timetable),
                  QString("%1 written in TSRE's layout and read again: the same timetable (%2 KiB)")
                      .arg(name).arg(written.toUtf8().size() / 1024));
            const QString out = qEnvironmentVariable("TSRE_TIMETABLE_WRITE");
            if (!out.isEmpty() && !QFileInfo(out).absoluteFilePath().startsWith(QFileInfo(routeDirectory).absoluteFilePath())) {
                OrTimetable copy = timetable;
                copy.file = out + "/" + name;
                QString error;
                check(copy.save(&error), "written to " + copy.file + " " + error);
            }
        }
        const QVector<OrTimetableProblem> problems = CheckTimetable(timetable, routeDirectory, consists, &database);
        // What the check found, by kind (the text up to its first name).
        QMap<QString, int> kinds;
        for (const OrTimetableProblem &p : problems) {
            QString kind = p.text;
            kind.replace(QRegularExpression("\\$[a-z_]+ is not run"), "$… is not run");
            kinds[kind.section(' ', 0, 0) + " … " + kind.section(' ', -3)]++;
        }
        qInfo().noquote() << "[timetable]" << name << "|" << problems.size() << "problems";
        for (auto it = kinds.begin(); it != kinds.end(); ++it)
            qInfo().noquote() << "[timetable]   " << it.value() << "×" << it.key();
        if (verbose)
            for (const OrTimetableProblem &p : problems)
                qInfo().noquote() << "[timetable]     " << (p.train.isEmpty() ? QString("-") : p.train) << ":" << p.text;
    }
    forgetPaths();
    qInfo().noquote() << QString("timetable-route: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runTimetableTrainsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-trains] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-trains] FAIL" << name; }
    };

    // Three 2 km lines (tests/TrackLayoutSupport). Line 10: platforms
    // Alpha (100-300 m), Beta (1000-1200), Gamma (1700-1900); T1's path
    // P1 from 350 m to 1950 m, so Alpha is behind its start. Line 30: T2's
    // path P2 from 100 to 800 m, platform Delta beyond its end (1000-1200),
    // the static train's path P4 from 1500 m. Line 50: platform Epsilon
    // (100-300) behind T3's path P3 from 400 m, a signal between at 330 m.
    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/R";
    const QString folder = routePath + "/ACTIVITIES/OPENRAILS";
    QDir().mkpath(folder);
    QDir().mkpath(routePath + "/PATHS");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    QDir().mkpath(temp.path() + "/TRAINS/TRAINSET/TEST");
    bool written = true;
    for (const QString &name : {"P1", "P2", "P3", "P4"})
        written = written && writeFile(routePath + "/PATHS/" + name + ".pat", QString(pathText).arg(name), Encoding::Utf16);
    written = written && writeFile(temp.path() + "/TRAINS/CONSISTS/Two.con", consistText, Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/loco.eng", QString(vehicleText).arg("loco", "Engine"),
                         Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/car.wag", QString(vehicleText).arg("car", "Carriage"),
                         Encoding::Utf16);
    // The path cells name P1 by its file and P2 with a trailing '.' (Open
    // Rails reads both); the stops: Alpha a departure only, Beta booked
    // 2 minutes, Gamma one time.
    written = written && writeFile(folder + "/test.timetable-or",
                                   ";T1;T2;T3;$static\n"
                                   "#path;P1.pat;P2.;P3;P4\n"
                                   "#consist;Two;Two;Two;Two\n"
                                   "#start;08:00;08:00;08:00;\n"
                                   "Alpha;08:05;;;\n"
                                   "Beta;08:10-08:12;;;\n"
                                   "Gamma;08:20;;;\n"
                                   "Delta;;08:06;;\n"
                                   "Epsilon;;;08:01;\n",
                                   Encoding::Utf8);
    check(temp.isValid() && written, "write the test route");

    TestTrack::Layout layout;
    for (int k = 0; k < 3; ++k) {
        const int line = 10 + 20 * k;
        layout.end(2 * k + 1, line);
        layout.track(line, 2 * k + 1, 2 * k + 2, 200);
        layout.end(2 * k + 2, line);
    }
    TestTrack::platform(layout, 10, 101, 100.0f, 300.0f, "Alpha");
    TestTrack::platform(layout, 10, 103, 1000.0f, 1200.0f, "Beta");
    TestTrack::platform(layout, 10, 105, 1700.0f, 1900.0f, "Gamma");
    TestTrack::platform(layout, 30, 201, 1000.0f, 1200.0f, "Delta");
    TestTrack::platform(layout, 50, 301, 100.0f, 300.0f, "Epsilon");
    TestTrack::signalHead(layout, 50, 401, 330.0f, true, "Test");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "R");
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
    const int firstPath = ActLib::jestpath;
    const std::pair<const char *, std::pair<PathTrack::TrackPoint, PathTrack::TrackPoint>> laid[] = {
        {"P1.pat", {{10, 350.0f}, {10, 1950.0f}}}, {"P2.pat", {{30, 100.0f}, {30, 800.0f}}},
        {"P3.pat", {{50, 400.0f}, {50, 1500.0f}}}, {"P4.pat", {{30, 1500.0f}, {30, 1700.0f}}}};
    for (const auto &[file, ends] : laid) {
        Path *path = ActLib::Paths.value(ActLib::AddPath(routePath + "/PATHS", file));
        if (path == nullptr)
            continue;
        PathTrack::Result made = PathTrack::startAt(&layout.db, ends.first);
        made = PathTrack::endAt(&layout.db, made.nodes, ends.second);
        path->apply(made.nodes);
    }

    const int id = ActLib::AddTimetableSet(folder, "test.timetable-or");
    Activity *set = ActLib::TimetableSets.value(id);
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(&layout.db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(&layout.db, *geometry, nullptr, false);
    const std::shared_ptr<Sim::SimulationInput> input
            = set != nullptr ? Sim::MakeInput(set, &layout.db, 30.0f, geometry, network) : nullptr;
    const auto trainNamed = [&](const QString &name) -> const Sim::TrainPlan * {
        for (const Sim::TrainPlan &plan : input->trains)
            if (plan.service == name)
                return &plan;
        return nullptr;
    };
    check(input != nullptr && input->trains.size() == 3 && input->startTime == 8 * 3600,
          QString("the running trains made, the run from the first one's start (08:00): %1 trains; warnings: %2")
              .arg(input != nullptr ? int(input->trains.size()) : -1)
              .arg(input != nullptr ? input->warnings.join("; ") : QString()));
    if (input == nullptr || input->trains.size() != 3) {
        qInfo().noquote() << QString("timetable-trains: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }
    const Sim::TrainPlan *t1 = trainNamed("T1");
    const Sim::TrainPlan *t2 = trainNamed("T2");
    const Sim::TrainPlan *t3 = trainNamed("T3");
    check(t1 != nullptr && t2 != nullptr && t3 != nullptr && std::fabs(t1->length - 40.0) < 0.01
              && t1->startTime == 8 * 3600 && t1->traffic >= 0 && !t1->player,
          "each an AI train of the consist (40 m), from its start time");
    // T1: Alpha behind its path's start, in the same track section: the
    // train is at that station from its start (Open Rails); Beta's booked
    // dwell under the platform's least waiting time (180 s); Gamma's one
    // time, the least waiting time. No stop holds its exit signal (Open
    // Rails' timetable mode, without `$hold`).
    QStringList stops;
    if (t1 != nullptr)
        for (const Sim::StopPlan &stop : t1->stops)
            stops << QString("%1 %2-%3 %4s").arg(stop.platform).arg(stop.arrival).arg(stop.departure).arg(stop.boarding);
    check(t1 != nullptr && t1->stops.size() == 3 && t1->stops[0].platform == "Alpha" && t1->stops[0].part == 0
              && std::fabs(t1->stops[0].front - t1->startFront) < 0.01 && t1->stops[0].departure == 8 * 3600 + 300
              && t1->stops[1].platform == "Beta" && t1->stops[1].boarding == 120 && t1->stops[2].platform == "Gamma"
              && t1->stops[2].boarding == 180
              && std::none_of(t1->stops.begin(), t1->stops.end(), [](const Sim::StopPlan &stop) { return stop.holdsExit; }),
          "T1: at Alpha (behind its start) from its start, then Beta and Gamma: " + stops.join(", "));
    // T2: Delta beyond its path's end: it runs on to the platform's far
    // end and stops on it.
    check(t2 != nullptr && t2->stops.size() == 1 && t2->stops[0].platform == "Delta" && t2->route.parts.size() == 1
              && std::fabs(t2->route.parts.back().length() - t2->stops[0].platformEnd) < 0.01
              && std::fabs(t2->stops[0].platformEnd - t2->stops[0].platformStart - 200.0) < 0.01
              && std::fabs(t2->stops[0].front - (t2->stops[0].platformEnd - 80.0)) < 0.01,
          QString("T2 (path P2.): on to Delta beyond its path's end, centred on it (route %1 m, front %2)")
              .arg(t2 != nullptr ? t2->route.parts.back().length() : -1.0, 0, 'f', 1)
              .arg(t2 != nullptr && !t2->stops.empty() ? t2->stops[0].front : -1.0, 0, 'f', 1));
    // T3: Epsilon behind its start with a signal between: another track
    // section, not on its route.
    check(t3 != nullptr && t3->stops.empty() && input->warnings.join("\n").contains("T3: the stop at Epsilon is not on its path"),
          "T3: Epsilon behind a signal, not on its route: " + input->warnings.join("; "));
    // The static train: a standing consist from its path's start.
    double from = 1e9, to = -1e9;
    for (const Sim::StaticConsist &standing : input->staticConsists)
        for (const Sim::RouteInterval &piece : standing.track) {
            from = std::min({from, double(piece.from), double(piece.to)});
            to = std::max({to, double(piece.from), double(piece.to)});
        }
    check(input->staticConsists.size() == 1 && input->staticConsists[0].name == "S4"
              && std::fabs(from - 1500.0) < 0.01 && std::fabs(to - 1540.0) < 0.01,
          QString("the static train stands from its path's start: %1 to %2 m").arg(from).arg(to));

    // The run: T1 leaves Alpha at its booked 08:05, stands at Beta to
    // 08:12; T2 stops at Delta.
    Sim::SimulationCore core(input);
    std::vector<Sim::TrainEvent> events;
    for (int k = 0; k < 25 * 60 * 10 && core.time() < 8 * 3600 + 25 * 60; ++k) {
        core.step(Sim::SimulationCore::MaxStep);
        for (const Sim::TrainEvent &e : core.takeEvents())
            events.push_back(e);
    }
    const auto at = [&](const Sim::TrainPlan *plan, Sim::TrainEvent::Kind kind, int index) {
        for (const Sim::TrainEvent &e : events)
            if (plan != nullptr && e.train == plan->id && e.kind == kind && e.index == index)
                return e.time;
        return -1.0;
    };
    const double leftAlpha = at(t1, Sim::TrainEvent::Departed, 0);
    const double atBeta = at(t1, Sim::TrainEvent::Arrived, 1), leftBeta = at(t1, Sim::TrainEvent::Departed, 1);
    check(at(t1, Sim::TrainEvent::Arrived, 0) >= 0.0 && std::fabs(leftAlpha - (8 * 3600 + 300)) <= 1.0 && atBeta > leftAlpha
              && std::fabs(leftBeta - (8 * 3600 + 720)) <= 1.0,
          QString("T1 leaves Alpha at %1 s and Beta at %2 s (booked 29100, 29520)").arg(leftAlpha).arg(leftBeta));
    bool t2Finished = false;
    for (const Sim::TrainEvent &e : events)
        t2Finished = t2Finished || (t2 != nullptr && e.train == t2->id && e.kind == Sim::TrainEvent::Finished);
    check(at(t2, Sim::TrainEvent::Arrived, 0) > 8 * 3600 && t2Finished,
          QString("T2 stops at Delta (%1 s) and ends there").arg(at(t2, Sim::TrainEvent::Arrived, 0)));

    delete set;
    ActLib::TimetableSets.remove(id);
    for (int path = firstPath; path < ActLib::jestpath; ++path)
        ActLib::Paths[path] = nullptr;
    qInfo().noquote() << QString("timetable-trains: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

namespace {

// A sigcfg.dat of one NORMAL signal type, STOP or CLEAR_2 (no script: the
// built-in rule, clear when cleared for a train into a clear block).
const char *commandSigCfg = R"(SIMISA@@@@@@@@@@JINX0G0t______

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
		SignalDrawStates ( 2
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Green" DrawLights ( 1 DrawLight ( 1 ) ) )
		)
		SignalAspects ( 2
			SignalAspect ( STOP "Red" )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( 2 )
	)
)
SignalShapes ( 0 )
ScriptFiles ( 0 )
)";

}

int TsreTests::runTimetableCommandsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-commands] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-commands] FAIL" << name; }
    };

    // Six 2 km lines (track 10 + 20k, k = 0..5) and a passing loop: on
    // lines 0 to 3 a platform from 500 to 700 m and a signal at 750 m;
    // standing trains beyond the signals of lines 2 and 3 (their blocks
    // never clear). Line 0: H holds its exit signal (`$hold`); line 1: G
    // does not (the default); line 2: W waits for its signal (the
    // default); line 3: N does not (`$nowaitsignal`); line 4: C is put on
    // its track at 08:00 and starts at 08:10 (`$create`), staying at its
    // path's end (`#dispose $static`); line 5: T1 forms T2 when its run
    // ends (`$triggers`). The loop: A, on its branch, follows B coming
    // along the main line (`$follow`). M starts before midnight and stops
    // after it; X starts after midnight with `$next`.
    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/R";
    const QString folder = routePath + "/ACTIVITIES/OPENRAILS";
    QDir().mkpath(folder);
    QDir().mkpath(routePath + "/PATHS");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    QDir().mkpath(temp.path() + "/TRAINS/TRAINSET/TEST");
    const QStringList pathNames = {"L0", "L1", "L2", "L3", "L4", "L5a", "L5b", "LA", "LB", "S2", "S3"};
    bool written = true;
    for (const QString &name : pathNames)
        written = written && writeFile(routePath + "/PATHS/" + name + ".pat", QString(pathText).arg(name), Encoding::Utf16);
    written = written && writeFile(temp.path() + "/TRAINS/CONSISTS/Two.con", consistText, Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/loco.eng", QString(vehicleText).arg("loco", "Engine"),
                         Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/car.wag", QString(vehicleText).arg("car", "Carriage"),
                         Encoding::Utf16)
            && writeFile(temp.path() + "/sigcfg.dat", commandSigCfg, Encoding::Utf16);
    written = written && writeFile(folder + "/commands.timetable-or",
                                   ";H;G;W;N;C;T1;T2;A;B;M;X;$static;$static\n"
                                   "#path;L0;L1;L2;L3;L4;L5a;L5b;LA;LB;L1;L0;S2;S3\n"
                                   "#consist;Two;Two;Two;Two;Two;Two;Two;Two;Two;Two;Two;Two;Two\n"
                                   "#start;08:00;08:00;08:00;08:00;08:10 $create=08:00;08:00;08:00;08:00;08:00;23:50;00:30 $next;;\n"
                                   "#note;$acc=2 $dec=3;;;;;;;$follow=B;;;;;\n"
                                   "#dispose;;;;;$static;$triggers=T2;;;;;;;\n"
                                   "Holdton;08:05-08:10 $hold;;;;;;;;;;;;\n"
                                   "Freeton;;08:05-08:10;;;;;;;;00:10;;;\n"
                                   "Waitton;;;08:05-08:06;;;;;;;;;;\n"
                                   "Nowaiton;;;;08:05-08:06 $nowaitsignal;;;;;;;;;\n",
                                   Encoding::Utf8);
    check(temp.isValid() && written, "write the test route");

    TestTrack::Layout layout;
    for (int k = 0; k < 6; ++k) {
        const int line = 10 + 20 * k;
        layout.end(1001 + 2 * k, line);
        layout.track(line, 1001 + 2 * k, 1002 + 2 * k, 200);
        layout.end(1002 + 2 * k, line);
    }
    layout.end(201, 120);
    layout.track(120, 201, 202, 30);
    layout.junction(202, 120, 121, 122);
    layout.track(121, 202, 203, 20);
    layout.track(122, 202, 203, 22);
    layout.junction(203, 123, 121, 122);
    layout.track(123, 203, 204, 30);
    layout.end(204, 123);
    const QStringList stations = {"Holdton", "Freeton", "Waitton", "Nowaiton"};
    for (int k = 0; k < 4; ++k) {
        TestTrack::platform(layout, 10 + 20 * k, 301 + 2 * k, 500.0f, 700.0f, stations[k]);
        TestTrack::signalHead(layout, 10 + 20 * k, 501 + k, 750.0f, true, "Home");
    }
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "R");
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
    const int firstPath = ActLib::jestpath;
    const std::pair<const char *, std::pair<PathTrack::TrackPoint, PathTrack::TrackPoint>> laid[] = {
        {"L0.pat", {{10, 100.0f}, {10, 1900.0f}}},   {"L1.pat", {{30, 100.0f}, {30, 1900.0f}}},
        {"L2.pat", {{50, 100.0f}, {50, 1900.0f}}},   {"L3.pat", {{70, 100.0f}, {70, 1900.0f}}},
        {"L4.pat", {{90, 100.0f}, {90, 1500.0f}}},   {"L5a.pat", {{110, 100.0f}, {110, 800.0f}}},
        {"L5b.pat", {{110, 800.0f}, {110, 1800.0f}}}, {"LA.pat", {{122, 150.0f}, {123, 250.0f}}},
        {"LB.pat", {{120, 50.0f}, {123, 280.0f}}},    {"S2.pat", {{50, 1000.0f}, {50, 1100.0f}}},
        {"S3.pat", {{70, 1000.0f}, {70, 1100.0f}}}};
    for (const auto &[file, ends] : laid) {
        Path *path = ActLib::Paths.value(ActLib::AddPath(routePath + "/PATHS", file));
        if (path == nullptr)
            continue;
        PathTrack::Result made = PathTrack::startAt(&layout.db, ends.first);
        made = PathTrack::endAt(&layout.db, made.nodes, ends.second);
        path->apply(made.nodes);
    }

    const int id = ActLib::AddTimetableSet(folder, "commands.timetable-or");
    Activity *set = ActLib::TimetableSets.value(id);
    const SigCfg signalConfig(temp.path() + "/sigcfg.dat");
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(&layout.db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(&layout.db, *geometry, &signalConfig, false);
    const std::shared_ptr<Sim::SimulationInput> input
            = set != nullptr ? Sim::MakeInput(set, &layout.db, 30.0f, geometry, network) : nullptr;
    const auto trainNamed = [&](const QString &name) -> const Sim::TrainPlan * {
        for (const Sim::TrainPlan &plan : input->trains)
            if (plan.service == name)
                return &plan;
        return nullptr;
    };
    check(input != nullptr && input->trains.size() == 11 && input->staticConsists.size() == 2,
          QString("the trains made: %1 and %2 static; warnings: %3")
              .arg(input != nullptr ? int(input->trains.size()) : -1)
              .arg(input != nullptr ? int(input->staticConsists.size()) : -1)
              .arg(input != nullptr ? input->warnings.join("; ") : QString()));
    if (input == nullptr || input->trains.size() != 11) {
        qInfo().noquote() << QString("timetable-commands: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }
    const Sim::TrainPlan *h = trainNamed("H"), *g = trainNamed("G"), *w = trainNamed("W"), *n = trainNamed("N");
    const Sim::TrainPlan *c = trainNamed("C"), *t1 = trainNamed("T1"), *t2 = trainNamed("T2");
    const Sim::TrainPlan *a = trainNamed("A"), *b = trainNamed("B"), *m = trainNamed("M"), *x = trainNamed("X");
    check(h && g && w && n && c && t1 && t2 && a && b && m && x, "every train by its name");
    if (!(h && g && w && n && c && t1 && t2 && a && b && m && x)) {
        qInfo().noquote() << QString("timetable-commands: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }
    check(h->accelerationFactor == 2.0f && h->decelerationFactor == 3.0f
              && std::fabs(h->acceleration - 2.0f * g->acceleration) < 1e-4f
              && std::fabs(h->deceleration - 3.0f * g->deceleration) < 1e-4f,
          "$acc=2 $dec=3: the rates times 2 and 3");
    check(h->stops.size() == 1 && h->stops[0].holdsExit && h->stops[0].waitsForSignal && g->stops.size() == 1
              && !g->stops[0].holdsExit && g->stops[0].waitsForSignal && n->stops.size() == 1
              && !n->stops[0].waitsForSignal && g->stops[0].exit == Sim::StopPlan::Exit::Timetable,
          "stops: $hold holds the exit signal, none by default; waiting for it by default, not with $nowaitsignal");
    check(c->createTime == 8 * 3600 && c->startTime == 8 * 3600 + 600 && c->staysAtEnd && t2->formedBy == t1->id
              && t1->formedBy < 0 && !a->follows.empty() && a->follows.front().train == b->id && b->follows.empty(),
          QString("$create=08:00, #dispose $static, $triggers=T2, $follow=B (%1 follow points)").arg(a->follows.size()));
    check(m->startTime == 23 * 3600 + 50 * 60 && m->stops.size() == 1 && m->stops[0].arrival == 86400 + 600
              && x->startTime == 86400 + 30 * 60,
          QString("midnight: M's 00:10 the next day (%1), X's $next start (%2)")
              .arg(m->stops.empty() ? -1 : m->stops[0].arrival).arg(x->startTime));

    Sim::SimulationCore core(input);
    std::vector<Sim::TrainEvent> events;
    const auto runTo = [&](int time) {
        while (core.time() < time - 1e-6) {
            core.step(Sim::SimulationCore::MaxStep);
            for (const Sim::TrainEvent &e : core.takeEvents())
                events.push_back(e);
        }
    };
    const auto signalOf = [&](int item) {
        const int head = network->headOfItem(item);
        return head >= 0 ? network->heads()[size_t(head)].signal : -1;
    };
    const auto eventOf = [&](const Sim::TrainPlan *plan, Sim::TrainEvent::Kind kind) -> const Sim::TrainEvent * {
        for (const Sim::TrainEvent &e : events)
            if (e.train == plan->id && e.kind == kind)
                return &e;
        return nullptr;
    };
    runTo(8 * 3600 + 5);
    const Sim::TrainState &created = core.trains()[size_t(c->id)];
    check(created.status == Sim::TrainStatus::Stopped && created.stoppedAt == Sim::StopKind::Created
              && eventOf(c, Sim::TrainEvent::Started) != nullptr
              && std::fabs(eventOf(c, Sim::TrainEvent::Started)->time - 8 * 3600) < 0.2,
          "$create: on its track from 08:00, standing until its start");
    runTo(8 * 3600 + 6 * 60);
    check(core.cleared()[size_t(signalOf(501))] != h->id && core.cleared()[size_t(signalOf(502))] == g->id,
          "at 08:06 H's held exit signal is not cleared for it; G's (not held) is");
    runTo(8 * 3600 + 9 * 60);
    check(core.cleared()[size_t(signalOf(501))] == h->id, "two minutes before its 08:10 departure H's signal clears");
    const double frontW = core.trains()[size_t(w->id)].front;
    check(std::fabs(core.trains()[size_t(c->id)].front - c->startFront) < 0.01, "C stands until 08:10");
    runTo(8 * 3600 + 20 * 60);
    check(core.trains()[size_t(w->id)].stoppedAt == Sim::StopKind::Station
              && std::fabs(core.trains()[size_t(w->id)].front - frontW) < 0.01
              && core.trains()[size_t(n->id)].front > frontW + 10.0 && eventOf(n, Sim::TrainEvent::Departed) != nullptr
              && eventOf(w, Sim::TrainEvent::Departed) == nullptr,
          QString("its signal at STOP, W waits at Waitton; N ($nowaitsignal) leaves and draws up to it (%1 m on)")
              .arg(core.trains()[size_t(n->id)].front - frontW, 0, 'f', 1));
    check(core.trains()[size_t(c->id)].status == Sim::TrainStatus::Stopped
              && core.trains()[size_t(c->id)].stoppedAt == Sim::StopKind::End
              && eventOf(c, Sim::TrainEvent::Finished) != nullptr,
          "C ran from 08:10 and stays at its path's end ($static)");
    const Sim::TrainEvent *ended = eventOf(t1, Sim::TrainEvent::Finished), *formed = eventOf(t2, Sim::TrainEvent::Started);
    check(ended != nullptr && formed != nullptr && formed->time >= ended->time - 1e-6,
          QString("T2 is put on its track when T1's run ends (%1, %2)")
              .arg(ended ? ended->time : -1.0, 0, 'f', 1).arg(formed ? formed->time : -1.0, 0, 'f', 1));
    bool followed = false;
    for (const Sim::TrainEvent &e : events)
        followed = followed || (e.train == a->id && e.kind == Sim::TrainEvent::Blocked && e.waitFor == Sim::WaitFor::Follow
                                && e.index == b->id);
    const Sim::TrainEvent *endA = eventOf(a, Sim::TrainEvent::Finished), *endB = eventOf(b, Sim::TrainEvent::Finished);
    check(followed && endA != nullptr && endB != nullptr && endB->time < endA->time,
          QString("A waits where its route joins B's until B has passed, then runs (B ends %1, A %2)")
              .arg(endB ? endB->time : -1.0, 0, 'f', 1).arg(endA ? endA->time : -1.0, 0, 'f', 1));

    delete set;
    ActLib::TimetableSets.remove(id);
    for (int path = firstPath; path < ActLib::jestpath; ++path)
        ActLib::Paths[path] = nullptr;
    qInfo().noquote() << QString("timetable-commands: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
