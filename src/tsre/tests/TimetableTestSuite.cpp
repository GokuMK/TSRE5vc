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
#include <routeEditor/activity/OrTrainProperties.h>
#include <tsre/Undo.h>
#include <QApplication>
#include <QTreeWidget>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/trains/OrTimetableCommands.h>
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
        // Phase 8: a station's command with a qualifier, in its next column
        // (Open Rails cuts the first cell at a '/'), written there; a
        // request stop and one it may leave early keep their marks when
        // edited (one time, the mark in place of the ':').
        OrTimetable marks;
        marks.loadText(";;T1\n#path;;p1\n#consist;;c1\n#start;;05:00\nA;$keepclear /rear=50;05:10\nB;;05x20\nC;;05*30\n");
        const QString markText = marks.toText();
        OrTimetable marksBack;
        marksBack.loadText(markText);
        const bool qualifierKept = marksBack.stations.size() == 3 && marksBack.stations[0].commands.size() == 1
                && marksBack.stations[0].commands[0].qualifier("rear") == QStringList({"50"})
                && fingerprint(marksBack) == fingerprint(marks) && !markText.split("\r\n").value(5).section(';', 0, 0).contains('/');
        marks.setStopTimes(0, 1, 5 * 3600 + 1500, 5 * 3600 + 1560);
        marks.setStopTimes(0, 2, -1, 5 * 3600 + 2100);
        OrTimetable edits;
        edits.loadText(marks.toText());
        const OrTimetable::Timing request = edits.trains.value(0).stops.value(1).timing;
        const OrTimetable::Timing early = edits.trains.value(0).stops.value(2).timing;
        check(qualifierKept && request.request && request.arrival == 5 * 3600 + 1500 && request.departure == request.arrival
                  && early.early && early.arrival == 5 * 3600 + 2100 && marks.trains[0].stops[1].text == "05x25"
                  && marks.trains[0].stops[2].text == "05*35",
              QString("a station's qualified command written in its column and read back (%1); request and early stops "
                      "edited keep their marks: %2, %3")
                  .arg(qualifierKept).arg(marks.trains[0].stops[1].text, marks.trains[0].stops[2].text));
        // Phase 8: commands edited, as typed (a first one without its '$';
        // a separator made a space), whether TSRE runs them or not: a
        // train's #start (its time kept), #note, #dispose (emptied), speed
        // row, a stop's, a station's; written and read back the same. A
        // train static by its column's head has no start cell to edit.
        OrTimetable typed = t;
        const int c1 = typed.trainNamed("T1");
        typed.setTrainCommands(c1, OrTimetable::TrainCommands::Start, "create=04:40 /ahead=T2");
        typed.setTrainCommands(c1, OrTimetable::TrainCommands::Note, "$acc=3;$wait=T2 /maxdelay=4");
        typed.setTrainCommands(c1, OrTimetable::TrainCommands::Dispose, "  ");
        typed.setTrainCommands(c1, OrTimetable::TrainCommands::Speed, "$max=72 $creep=5");
        typed.setStopCommands(c1, 0, "$hold $keepclear /rear=30");
        typed.setStationCommands(1, "$terminal");
        typed.setTrainCommands(1, OrTimetable::TrainCommands::Start, "$create");
        OrTimetable typedBack;
        typedBack.loadText(typed.toText());
        const int typedT1 = typedBack.trainNamed("T1");
        const OrTimetable::Train *typedTrain = typedT1 >= 0 ? &typedBack.trains[typedT1] : nullptr;
        check(typedTrain != nullptr && typedTrain->start == "05:00 $create=04:40 /ahead=T2" && typedTrain->startTime == 5 * 3600
                  && typedTrain->startCommands.value(0).qualifier("ahead") == QStringList({"t2"}) && typedTrain->notes.size() == 2
                  && typedTrain->notes[0].values == QStringList({"3"}) && typedTrain->notes[1].name == "wait"
                  && typedTrain->notes[1].qualifier("maxdelay") == QStringList({"4"}) && typedTrain->dispose.isEmpty()
                  && std::fabs(typedTrain->speed - 20.0f) < 1e-3f && typedTrain->speedCommands.size() == 2
                  && typedTrain->stops.value(0).commands.size() == 2 && typedTrain->stops[0].commands[1].qualifier("rear") == QStringList({"30"})
                  && typedTrain->stops[0].timing.arrival == 5 * 3600 + 600 && typedBack.stations.value(1).commands.size() == 1
                  && typedBack.stations[1].commands[0].name == "terminal" && typedBack.trains[1].start == "$static"
                  && typed.modified && fingerprint(typedBack) == fingerprint(typed),
              "commands edited (start, note, dispose, speed row, a stop's, a station's) written and read back the same: "
                  + (typedTrain != nullptr ? typedTrain->start + " | " + OrTimetable::CommandsText(typedTrain->notes) + " | "
                                             + typedTrain->speedText + " | " + typedTrain->stops.value(0).text
                                       : QString()));
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
        // Phase 8: what TSRE reads and does not run is reported too: a path
        // cell's command, qualifiers of commands it runs that it does not,
        // passing times, #restartdelay; a station's first cell after a '/'
        // (not read, nor by Open Rails). Request stops, leaving early,
        // `$create /ahead`, `$follow /maxdelay` run (not reported).
        const QString more = dir.filePath("more.timetable-or");
        writeFile(more,
                  ";T1;T2\n#path;p1 $endatlastsignal /reverse;p1\n#consist;c1;c1\n"
                  "#start;05:00 $create=04:30 /ahead=T2;06:00\n#note;$follow=T2 /maxdelay=5;\n"
                  "#dispose;$static /closeup;$triggers=T1 /runround=rr\n#restartdelay;$station /fix=10;\n"
                  "A;05x10;05*20\nB;05P30;06:30\nC /ignored;;\n",
                  Encoding::Utf8);
        OrTimetable m;
        m.load(more);
        QStringList moreTexts;
        for (const OrTimetableProblem &p : CheckTimetable(m, dir.filePath("route"), dir.filePath("TRAINS/CONSISTS"), nullptr))
            moreTexts << QString("%1%2: %3").arg(p.kind == OrTimetableProblem::Unsupported ? "~" : "", p.train, p.text);
        const QString all = moreTexts.join("\n");
        check(moreTexts.size() == 6 && all.contains("~T1: path: $endatlastsignal is not run")
                  && all.contains("~T1: dispose: $static runs, its /closeup")
                  && all.contains("~T2: dispose: $triggers runs, its /runround")
                  && all.contains("~T1: passing times (P) at B") && all.contains("~T1: #restartdelay")
                  && all.contains("'/ignored'") && !all.contains("/ahead") && !all.contains("/maxdelay"),
              "the check reports what TSRE does not run (qualifiers, P, #restartdelay, a path's command) and a "
              "station cell cut at '/': " + moreTexts.join("; "));
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

    // The user's review: trains added, copied, removed, their path (the
    // cell's commands kept) and consist set, stops added and removed;
    // written and read back the same.
    {
        OrTimetable edited;
        edited.loadText(";#comment;A;$static\n"
                        "#path;;PA $endatlastsignal;PS\n"
                        "#consist;;CA;CS\n"
                        "#start;;06:00;\n"
                        "One;;06:10;\n"
                        "Two;;06:20;\n");
        edited.setTrainPath(0, "PB");
        edited.setTrainConsist(0, "CA + CB $reverse");
        const int fresh = edited.addTrain(0, "N", 7 * 3600);
        const int copy = edited.cloneTrain(2, "SC");
        const int stop = edited.addStop(fresh, 1, 7 * 3600 + 600, 7 * 3600 + 660);
        edited.addStop(0, 1, 6 * 3600 + 1200, -1);
        edited.removeStop(0, 0);
        OrTimetable back;
        back.loadText(edited.toText());
        const int a = back.trainNamed("A"), n = back.trainNamed("N"), sc = back.trainNamed("SC");
        check(fresh == 1 && copy == 3 && stop == 0 && back.trains.size() == 4 && a == 0 && n == 1 && sc == 3
                  && back.trains[a].path == "PB" && back.trains[a].pathText == "PB $endatlastsignal"
                  && back.trains[a].consists.size() == 2 && back.trains[a].consists[1].reverse
                  && back.trains[a].stops.size() == 1 && back.trains[a].stops[0].station == 1
                  && back.trains[n].startTime == 7 * 3600 && back.trains[n].stops.size() == 1
                  && back.trains[n].stops[0].timing.departure == 7 * 3600 + 660 && back.trains[sc].isStatic
                  && back.trains[sc].path == "PS" && back.trains[2].isStatic && back.trains[2].header == "$static",
              "trains added (N), copied (the unnamed static as SC, static), paths and consists set, stops added and "
              "removed: read back the same");
        edited.removeTrain(n);
        check(edited.trains.size() == 3 && edited.trainNamed("N") < 0, "a train removed");
    }

    // The command picker's catalogue: the manual's 48 (44 names: the speed
    // row's $attach and $detach, #dispose's $static and $pool apart), every
    // command TSRE runs there, each described; no name twice at one place;
    // every place has some.
    {
        const QStringList run = {"acc", "dec", "follow", "nowaitsignal", "create", "hold", "nohold", "forcehold",
                                 "static", "triggers", "closeupsignal", "extendplatformtosignal", "forcewait", "max",
                                 "waitsignal", "next", "terminal", "stoptime", "endstop", "restrictplatformtosignal",
                                 "keepclear", "req", "wait", "cruise", "maxdelay", "activate", "activated", "forms",
                                 "creep", "doo"};
        QStringList missing;
        for (const QString &name : run)
            if (!OrTimetableCommandRun(name))
                missing << name + " (not run)";
        QSet<QString> names;
        bool described = true;
        bool once = true;
        for (const OrCommandSpec &spec : OrCommandSpecs()) {
            names.insert(spec.name);
            described = described && !spec.description.isEmpty() && spec.places != 0;
            for (const OrCommandSpec::QualifierSpec &qualifier : spec.qualifiers)
                described = described && !qualifier.description.isEmpty();
            for (const OrCommandSpec &other : OrCommandSpecs())
                once = once && (&other == &spec || other.name != spec.name || (other.places & spec.places) == 0);
        }
        for (const QString &name : run)
            if (!names.contains(name))
                missing << name;
        bool places = true;
        for (int place : {OrCommandSpec::Start, OrCommandSpec::Note, OrCommandSpec::Dispose, OrCommandSpec::SpeedRow,
                          OrCommandSpec::Stop, OrCommandSpec::Station}) {
            int count = 0;
            for (const OrCommandSpec &spec : OrCommandSpecs())
                count += (spec.places & place) != 0;
            places = places && count > 0;
        }
        const OrCommandSpec *speedAttach = OrCommandSpecAt("attach", OrCommandSpec::SpeedRow);
        const OrCommandSpec *stopAttach = OrCommandSpecAt("attach", OrCommandSpec::Stop);
        check(missing.isEmpty() && described && once && places && names.size() == 44 && OrCommandSpecs().size() == 48
                  && speedAttach != nullptr
                  && stopAttach != nullptr && speedAttach->value == OrCommandSpec::Speed
                  && stopAttach->value == OrCommandSpec::Train
                  && OrCommandSpecAt("keepclear", OrCommandSpec::Station) == nullptr,
              QString("the command catalogue: %1 commands (%2 entries), every one TSRE runs, described; missing: %3")
                  .arg(names.size()).arg(OrCommandSpecs().size()).arg(missing.join(", ")));
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
    // Line 70: platform Zeta (1000-1200), T5's path P5 from 400 m reversing
    // inside it at 1100 m, back to 500 m.
    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/R";
    const QString folder = routePath + "/ACTIVITIES/OPENRAILS";
    QDir().mkpath(folder);
    QDir().mkpath(routePath + "/PATHS");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    QDir().mkpath(temp.path() + "/TRAINS/TRAINSET/TEST");
    bool written = true;
    for (const QString &name : {"P1", "P2", "P3", "P4", "P5"})
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
                                   ";T1;T2;T3;$static;T5\n"
                                   "#path;P1.pat;P2.;P3;P4;P5\n"
                                   "#consist;Two;Two;Two;Two;Two\n"
                                   "#start;08:00;08:00;08:00;;08:00\n"
                                   "Alpha;08:05;;;;\n"
                                   "Beta;08:10-08:12;;;;\n"
                                   "Gamma;08:20;;;;\n"
                                   "Delta;;08:06;;;\n"
                                   "Epsilon;;;08:01;;\n"
                                   "Zeta;;;;;08:04-08:06\n"
                                   "Nowhere;;08:30;;;\n",
                                   Encoding::Utf8);
    check(temp.isValid() && written, "write the test route");

    TestTrack::Layout layout;
    for (int k = 0; k < 4; ++k) {
        const int line = 10 + 20 * k;
        layout.end(2 * k + 1, line);
        layout.track(line, 2 * k + 1, 2 * k + 2, 200);
        layout.end(2 * k + 2, line);
    }
    TestTrack::platform(layout, 10, 101, 100.0f, 300.0f, "Alpha");
    TestTrack::platform(layout, 10, 103, 1000.0f, 1200.0f, "Beta");
    TestTrack::platform(layout, 10, 105, 1700.0f, 1900.0f, "Gamma");
    // Omega: a platform T1 passes that the timetable does not have.
    TestTrack::platform(layout, 10, 107, 1400.0f, 1500.0f, "Omega");
    TestTrack::platform(layout, 30, 201, 1000.0f, 1200.0f, "Delta");
    TestTrack::platform(layout, 50, 301, 100.0f, 300.0f, "Epsilon");
    TestTrack::platform(layout, 70, 501, 1000.0f, 1200.0f, "Zeta");
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
    if (Path *path = ActLib::Paths.value(ActLib::AddPath(routePath + "/PATHS", "P5.pat"))) {
        PathTrack::Result made = PathTrack::startAt(&layout.db, {70, 400.0f});
        made = PathTrack::reverseAt(&layout.db, made.nodes, {70, 1100.0f});
        made = PathTrack::endAt(&layout.db, made.nodes, {70, 500.0f});
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
    check(input != nullptr && input->trains.size() == 4 && input->startTime == 8 * 3600,
          QString("the running trains made, the run from the first one's start (08:00): %1 trains; warnings: %2")
              .arg(input != nullptr ? int(input->trains.size()) : -1)
              .arg(input != nullptr ? input->warnings.join("; ") : QString()));
    if (input == nullptr || input->trains.size() != 4) {
        qInfo().noquote() << QString("timetable-trains: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }
    const Sim::TrainPlan *t1 = trainNamed("T1");
    const Sim::TrainPlan *t2 = trainNamed("T2");
    const Sim::TrainPlan *t3 = trainNamed("T3");
    const Sim::TrainPlan *t5 = trainNamed("T5");
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
    // T5: Zeta, the platform its path reverses inside (one item before the
    // reversal, the other beyond): its stop before the reversal, centred on
    // the platform up to it (600-700 m along the path from its start, the
    // train 40 m).
    check(t5 != nullptr && t5->stops.size() == 1 && t5->stops[0].platform == "Zeta" && t5->stops[0].part == 0
              && t5->route.parts.size() == 2 && std::fabs(t5->stops[0].platformStart - t5->route.pathStart - 600.0) < 0.5
              && std::fabs(t5->stops[0].platformEnd - t5->route.pathStart - 700.0) < 0.5
              && std::fabs(t5->stops[0].front - t5->route.pathStart - 670.0) < 0.5,
          QString("T5: at Zeta, reversing inside it, on the part before the reversal (%1 to %2 m, front %3; "
                  "the path from %4 m, %5 parts)")
              .arg(t5 != nullptr && !t5->stops.empty() ? t5->stops[0].platformStart : -1.0, 0, 'f', 1)
              .arg(t5 != nullptr && !t5->stops.empty() ? t5->stops[0].platformEnd : -1.0, 0, 'f', 1)
              .arg(t5 != nullptr && !t5->stops.empty() ? t5->stops[0].front : -1.0, 0, 'f', 1)
              .arg(t5 != nullptr ? t5->route.pathStart : -1.0, 0, 'f', 1)
              .arg(t5 != nullptr ? int(t5->route.parts.size()) : -1));
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
    bool t5Finished = false;
    for (const Sim::TrainEvent &e : events)
        t5Finished = t5Finished || (t5 != nullptr && e.train == t5->id && e.kind == Sim::TrainEvent::Finished);
    const double leftZeta = at(t5, Sim::TrainEvent::Departed, 0);
    check(at(t5, Sim::TrainEvent::Arrived, 0) > 8 * 3600 && std::fabs(leftZeta - (8 * 3600 + 360)) <= 1.0 && t5Finished,
          QString("T5 stops at Zeta, leaves at %1 s (booked 29160), reverses and ends").arg(leftZeta));

    // The user's review: a train's stations along its path, as its stops
    // are found (Alpha behind T1's start, Omega passed, not in the
    // timetable; Delta beyond T2's end; none for T3, Epsilon behind a
    // signal; Zeta, T5 reversing inside it).
    QStringList keys;
    for (const OrTimetable::Station &station : set->timetables[0]->stations)
        keys << station.key;
    const auto alongPath = [&](const QString &name) {
        QStringList out;
        for (const Sim::PathStation &station : Sim::TimetablePathStations(&layout.db, ActLib::GetPathByName(name, routePath), keys))
            out << station.name;
        return out.join(", ");
    };
    check(alongPath("P1") == "Alpha, Beta, Omega, Gamma" && alongPath("P2") == "Delta" && alongPath("P3").isEmpty()
              && alongPath("P5") == "Zeta",
          QString("the stations along the paths: P1 %1; P2 %2; P3 %3; P5 %4")
              .arg(alongPath("P1"), alongPath("P2"), alongPath("P3"), alongPath("P5")));
    // The Timetable window's stops list: T1's path's stations, Omega not
    // in the timetable (a time typed there adds it after Beta and the
    // stop); T3's Epsilon not on its path; T2's Nowhere at no platform.
    {
        OrTrainProperties panel;
        QTreeWidget *rows = panel.findChild<QTreeWidget *>("orTrainStops");
        OrTimetable &tt = *set->timetables[0];
        const auto shown = [&](const QString &train) {
            panel.showTrain(set, 0, tt.trainNamed(train), {});
            QStringList out;
            for (int i = 0; rows != nullptr && i < rows->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = rows->topLevelItem(i);
                const QColor tint = item->background(0).color();
                out << item->text(0) + (item->text(1).isEmpty() ? QString(" -") : QString())
                        + (item->font(0).italic() ? QString(" (new)") : QString())
                        + (item->background(0).style() == Qt::NoBrush ? QString()
                           : tint.green() > 100 ? QString(" (yellow)") : QString(" (red)"));
            }
            return out.join(", ");
        };
        const QString t1Rows = shown("T1"), t3Rows = shown("T3"), t2Rows = shown("T2");
        check(t1Rows == "Alpha, Beta, Omega - (new), Gamma" && t3Rows == "Epsilon (yellow)"
                  && t2Rows == "Delta, Nowhere (red)",
              QString("the stops lists: T1 %1; T3 %2; T2 %3").arg(t1Rows, t3Rows, t2Rows));
        // TSRE_ACTIVITY_UI_SHOTS: T1 with a stop off its path and one at
        // no platform as well, for a look.
        if (qEnvironmentVariableIsSet("TSRE_ACTIVITY_UI_SHOTS")) {
            const int t1Index = tt.trainNamed("T1");
            const int epsilon = tt.addStop(t1Index, 4, 8 * 3600 + 1500, -1);
            const int nowhere = tt.addStop(t1Index, 6, 8 * 3600 + 1800, -1);
            shown("T1");
            panel.resize(640, 640);
            rows->grab().save(qEnvironmentVariable("TSRE_ACTIVITY_UI_SHOTS") + "/timetable-or-path-stations.png");
            tt.removeStop(t1Index, nowhere);
            tt.removeStop(t1Index, epsilon);
            tt.modified = false;
        }
        shown("T1");
        for (int i = 0; rows != nullptr && i < rows->topLevelItemCount(); ++i)
            if (rows->topLevelItem(i)->text(0) == "Omega")
                rows->topLevelItem(i)->setText(1, "08:15");
        QApplication::processEvents();
        const int omega = tt.stations.size() > 2 ? 2 : -1;
        const OrTimetable::Train &one = tt.trains[tt.trainNamed("T1")];
        const OrTimetable::Train &two = tt.trains[tt.trainNamed("T2")];
        check(omega == 2 && tt.stations[2].name == "Omega" && tt.stations[3].name == "Gamma" && one.stops.size() == 4
                  && one.stops[2].station == 2 && one.stops[2].timing.arrival == 8 * 3600 + 900
                  && one.stops[3].station == 3 && two.stops[0].station == 4 && tt.stations[4].name == "Delta"
                  && shown("T1") == "Alpha, Beta, Omega, Gamma",
              "a time typed at Omega: the station added after Beta, T1 stops there, the other stops kept");
        Undo::UndoLast();
        check(set->timetables[0]->stations.size() == 7 && set->timetables[0]->stations[2].name == "Gamma",
              "undo: Omega not in the timetable again");
    }

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

// Task 08, phase 8: the commands added from the catalogue, each on a line of
// its own (2 km, track 10 + 20k), run in the core: `$stoptime` (the
// station's row's winning), `$terminal`, `$keepclear /front /rear`,
// `$restrictplatformtosignal`, leaving early (`*`), request stops (`x`,
// `$req /pu /pd`), `$endstop` (read; run past as Open Rails' AI does),
// `$cruise` and `$maxdelay`, `$activate`
// (start, arrival, departure, end) and `$activated`, `$forms`, `$wait`
// with `/notstarted`, `/maxdelay`, `/owndelay`, `$create /ahead`.
int TsreTests::runTimetableMoreCommandsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-commands-more] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-commands-more] FAIL" << name; }
    };
    const auto done = [&] {
        qInfo().noquote() << QString("timetable-commands-more: %1 passed, %2 failed").arg(passed).arg(failed);
        return failed == 0 ? 0 : 1;
    };

    // The trains (columns) and their lines, paths (from, to on the line),
    // starts, notes, dispose, speed row; the stations (rows) with one
    // train's cell each, their platforms (line, from, to).
    struct Line {
        QString train;
        int k;
        float from, to;
        QString start, note, dispose, speed;
    };
    const QVector<Line> lines = {
        {"ST", 0, 100, 1900, "08:00", "", "", ""},
        {"ST2", 1, 100, 1900, "08:00", "", "", ""},
        {"TE", 2, 100, 1900, "08:00", "", "", ""},
        {"KC", 3, 100, 1900, "08:00", "", "", ""},
        {"RS", 4, 100, 1900, "08:00", "", "", ""},
        {"EA", 5, 100, 1900, "08:00", "", "", ""},
        {"RQ", 6, 100, 1900, "08:00", "", "", ""},
        {"ES", 7, 100, 1900, "08:00", "", "", ""},
        {"CR", 8, 100, 1900, "08:00", "", "", "$max=72 $cruise=36"},
        {"CD", 9, 100, 1900, "08:00", "", "", "$max=72 $cruise=36 $maxdelay=1"},
        {"TR", 10, 100, 1900, "08:00", "$activate=AS", "$activate=AD $activate=FD", ""},
        {"AV", 11, 100, 1900, "08:00 $activated", "", "", ""},
        {"AD", 12, 100, 1900, "08:00 $activated", "", "", ""},
        {"FA", 13, 100, 900, "08:00", "", "$forms=FB", ""},
        {"FB", 13, 900, 100, "08:00", "", "", ""},
        {"WA", 14, 100, 1900, "08:00", "$wait=WB", "", ""},
        {"WB", 14, 600, 1900, "08:00", "", "", ""},
        {"WC", 15, 100, 1900, "08:00", "$wait=WD /notstarted", "", ""},
        {"WD", 15, 600, 1900, "08:03", "", "", ""},
        {"CA", 16, 100, 1900, "08:20 $create=07:59 /ahead=CB", "", "", ""},
        {"CB", 16, 100, 1900, "08:20 $create=07:58", "", "", ""},
        {"AW", 17, 100, 1900, "08:00 $activated", "", "", ""},
        {"AS", 18, 100, 1900, "07:50 $activated", "", "", ""},
        {"WM", 19, 100, 1900, "08:00", "$wait=WN /maxdelay=1", "", ""},
        {"WN", 19, 600, 1900, "08:00", "", "", ""},
        {"WQ", 20, 100, 1900, "08:00", "$wait=WR /owndelay=10", "", ""},
        {"WR", 20, 600, 1900, "08:03", "", "", ""},
        {"FC", 21, 100, 900, "08:00", "", "$triggers=FD", ""},
        {"FD", 21, 900, 1900, "08:01 $activated", "", "", ""},
    };
    struct Station {
        QString row, train, cell;
        int k;
        float from, to;
    };
    const QVector<Station> stations = {
        {"Sto $stoptime=60", "ST", "08:01 $stoptime=40", 0, 500, 700},
        {"Sto2", "ST2", "08:01 $stoptime=40", 1, 500, 700},
        {"Ter", "TE", "08:02 $terminal", 2, 1000, 1200},
        {"Kc1", "KC", "08:02 $keepclear /front=100", 3, 500, 900},
        {"Kc2", "KC", "08:06 $keepclear /rear=50", 3, 1300, 1700},
        {"Rs", "RS", "08:02 $restrictplatformtosignal", 4, 500, 900},
        {"Ea", "EA", "08*10", 5, 500, 700},
        {"Rq1", "RQ", "08x02 $req /pu=99", 6, 500, 700},
        {"Rq2", "RQ", "08x05 $req /pu=0 /pd=0", 6, 1100, 1300},
        {"Es", "ES", "08:02 $endstop", 7, 500, 700},
        {"Cd", "CD", "08:00:30", 9, 1500, 1700},
        {"Tr", "TR", "08:02 $activate=AV $activate=AW /depart", 10, 500, 700},
        {"Wn", "WN", "07:59", 19, 1500, 1700},
        {"Wq", "WQ", "08:00:05", 20, 200, 300},
    };
    // Signals splitting a line (k, at): Rs's inside its platform; at 400 m
    // where two trains share a line.
    const QVector<QPair<int, float>> signalsAt = {{4, 700}, {14, 400}, {15, 400}, {19, 400}, {20, 400}};

    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/R";
    const QString folder = routePath + "/ACTIVITIES/OPENRAILS";
    QDir().mkpath(folder);
    QDir().mkpath(routePath + "/PATHS");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    QDir().mkpath(temp.path() + "/TRAINS/TRAINSET/TEST");
    bool written = writeFile(temp.path() + "/TRAINS/CONSISTS/Two.con", consistText, Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/loco.eng", QString(vehicleText).arg("loco", "Engine"),
                         Encoding::Utf16)
            && writeFile(temp.path() + "/TRAINS/TRAINSET/TEST/car.wag", QString(vehicleText).arg("car", "Carriage"),
                         Encoding::Utf16)
            && writeFile(temp.path() + "/sigcfg.dat", commandSigCfg, Encoding::Utf16);
    QStringList head{""}, path{"#path"}, consist{"#consist"}, start{"#start"}, note{"#note"}, dispose{"#dispose"},
            speed{"#speedkph"};
    for (const Line &line : lines) {
        head << line.train;
        path << "p" + line.train;
        consist << "Two";
        start << line.start;
        note << line.note;
        dispose << line.dispose;
        speed << line.speed;
        written = written && writeFile(routePath + "/PATHS/p" + line.train + ".pat", QString(pathText).arg("p" + line.train),
                                       Encoding::Utf16);
    }
    QStringList rows{head.join(';'), path.join(';'), consist.join(';'), start.join(';'), note.join(';'),
                     dispose.join(';'), speed.join(';')};
    for (const Station &station : stations) {
        QStringList row{station.row};
        for (const Line &line : lines)
            row << (line.train == station.train ? station.cell : QString());
        rows << row.join(';');
    }
    written = written && writeFile(folder + "/more.timetable-or", rows.join("\n") + "\n", Encoding::Utf8);
    check(temp.isValid() && written, "write the test route");

    TestTrack::Layout layout;
    for (int k = 0; k <= 21; ++k) {
        const int track = 10 + 20 * k;
        layout.end(1001 + 2 * k, track);
        layout.track(track, 1001 + 2 * k, 1002 + 2 * k, 200);
        layout.end(1002 + 2 * k, track);
    }
    for (int s = 0; s < stations.size(); ++s)
        TestTrack::platform(layout, 10 + 20 * stations[s].k, 3001 + 2 * s, stations[s].from, stations[s].to,
                            stations[s].row.section(' ', 0, 0));
    for (int s = 0; s < signalsAt.size(); ++s)
        TestTrack::signalHead(layout, 10 + 20 * signalsAt[s].first, 4001 + s, signalsAt[s].second, true, "Home");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "R");
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
    const int firstPath = ActLib::jestpath;
    for (const Line &line : lines) {
        Path *made = ActLib::Paths.value(ActLib::AddPath(routePath + "/PATHS", "p" + line.train + ".pat"));
        if (made == nullptr)
            continue;
        PathTrack::Result laid = PathTrack::startAt(&layout.db, {10 + 20 * line.k, line.from});
        if (line.from > line.to)
            laid = PathTrack::turnStart(&layout.db, laid.nodes);
        laid = PathTrack::endAt(&layout.db, laid.nodes, {10 + 20 * line.k, line.to});
        made->apply(laid.nodes);
    }
    const int id = ActLib::AddTimetableSet(folder, "more.timetable-or");
    Activity *set = ActLib::TimetableSets.value(id);
    const SigCfg signalConfig(temp.path() + "/sigcfg.dat");
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(&layout.db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(&layout.db, *geometry, &signalConfig, false);
    const std::shared_ptr<Sim::SimulationInput> input
            = set != nullptr ? Sim::MakeInput(set, &layout.db, 30.0f, geometry, network) : nullptr;
    check(input != nullptr && input->trains.size() == size_t(lines.size()),
          QString("the trains made: %1 of %2; warnings: %3")
              .arg(input != nullptr ? int(input->trains.size()) : -1).arg(lines.size())
              .arg(input != nullptr ? input->warnings.join("; ") : QString()));
    if (input == nullptr || input->trains.size() != size_t(lines.size()))
        return done();
    QMap<QString, const Sim::TrainPlan *> t;
    for (const Sim::TrainPlan &plan : input->trains)
        t[plan.service] = &plan;
    for (const Line &line : lines)
        if (!t.contains(line.train)) {
            check(false, "train " + line.train + " made");
            return done();
        }

    // The plans.
    const Sim::StopPlan *st = t["ST"]->stops.empty() ? nullptr : &t["ST"]->stops[0];
    const Sim::StopPlan *st2 = t["ST2"]->stops.empty() ? nullptr : &t["ST2"]->stops[0];
    check(st && st2 && st->boarding == 60 && st2->boarding == 40,
          QString("$stoptime: the least time standing (the station's 60 winning, the cell's 40): %1, %2")
              .arg(st ? st->boarding : -1).arg(st2 ? st2->boarding : -1));
    const Sim::StopPlan *te = t["TE"]->stops.empty() ? nullptr : &t["TE"]->stops[0];
    check(te && te->terminal && std::fabs(te->front - (te->platformStart + 15.0 + 40.0)) < 0.01,
          QString("$terminal, no junction before the platform: its rear 15 m inside the platform's start (front %1 "
                  "m past it)").arg(te ? te->front - te->platformStart : -1.0, 0, 'f', 1));
    const std::vector<Sim::StopPlan> &kc = t["KC"]->stops;
    check(kc.size() == 2 && std::fabs(kc[0].front - (kc[0].platformEnd - 100.0)) < 0.01
              && std::fabs(kc[1].front - (kc[1].platformStart + 50.0 + 40.0)) < 0.01,
          "$keepclear /front=100: 100 m of the platform clear ahead; /rear=50: 50 m behind");
    const Sim::TrainPlan *rs = t["RS"];
    const double inside = rs->projection.empty() || rs->projection[0].stopSignals.empty()
            ? -1.0 : rs->projection[0].stopSignals[0].at;
    check(rs->stops.size() == 1 && inside > 0.0 && std::fabs(rs->stops[0].platformEnd - (inside - 7.0)) < 0.01,
          "$restrictplatformtosignal: the platform's end 7 m short of the signal inside it");
    check(t["EA"]->stops.size() == 1 && t["EA"]->stops[0].departsEarly && t["RQ"]->stops.size() == 2
              && t["RQ"]->stops[0].request && t["RQ"]->stops[0].needed && t["RQ"]->stops[1].request
              && !t["RQ"]->stops[1].needed && t["ES"]->stops.size() == 1 && t["ES"]->stops[0].endStop,
          "08*10 may leave early; 08x02 with /pu=99 needed, 08x05 with /pu=0 /pd=0 not; $endstop");
    check(std::fabs(t["CR"]->cruiseSpeed - 10.0f) < 1e-3f && t["CR"]->cruiseMaxDelay < 0
              && t["CD"]->cruiseMaxDelay == 60 && std::fabs(t["CD"]->maxSpeed - 20.0f) < 1e-3f,
          "$cruise=36 km/h (10 m/s), $maxdelay=1 minute");
    const Sim::TrainPlan *tr = t["TR"];
    check(t["AV"]->activatedBy == tr->id && t["AV"]->activation == Sim::TrainPlan::Activation::Arrival
              && t["AV"]->activationStop == 0 && t["AW"]->activatedBy == tr->id
              && t["AW"]->activation == Sim::TrainPlan::Activation::Departure && t["AD"]->activatedBy == tr->id
              && t["AD"]->activation == Sim::TrainPlan::Activation::End && t["FD"]->activatedBy == tr->id
              && t["FD"]->formedBy == t["FC"]->id && t["AS"]->activatedBy == tr->id
              && t["AS"]->activation == Sim::TrainPlan::Activation::Start,
          "$activate at Tr (arriving; /depart: leaving), in #dispose (its end), in #note (its start)");
    check(t["FB"]->formedBy == t["FA"]->id && t["FB"]->formedOfVehicles && t["CA"]->createAhead == t["CB"]->id,
          "$forms=FB: formed of FA's vehicles; $create /ahead=CB");
    check(t["WA"]->follows.size() == 1 && t["WA"]->follows[0].train == t["WB"]->id && t["WC"]->follows.size() == 1
              && t["WC"]->follows[0].notStarted && t["WM"]->follows.size() == 1 && t["WM"]->follows[0].maxDelay == 60
              && t["WQ"]->follows.size() == 1 && t["WQ"]->follows[0].ownDelay == 600,
          QString("$wait: one point where the routes join (%1); /notstarted, /maxdelay=1, /owndelay=10")
              .arg(t["WA"]->follows.size()));

    // The run, 07:50 to 08:40: what happened when, the fastest each ran, the
    // ways each waited.
    Sim::SimulationCore core(input);
    std::vector<Sim::TrainEvent> events;
    QMap<int, double> fastest;
    QMap<int, double> fastestAfter;
    double frontsAhead = -1.0, frontAt0801WC = -1.0, frontAt0801WA = -1.0;
    bool fdStanding = false;
    const auto runTo = [&](double time) {
        while (core.time() < time - 1e-6) {
            core.step(Sim::SimulationCore::MaxStep);
            for (const Sim::TrainEvent &e : core.takeEvents())
                events.push_back(e);
            for (size_t i = 0; i < core.trains().size(); ++i) {
                const double v = core.trains()[i].speed;
                fastest[int(i)] = std::max(fastest.value(int(i)), v);
                if (core.time() > 8 * 3600 + 100)
                    fastestAfter[int(i)] = std::max(fastestAfter.value(int(i)), v);
            }
        }
    };
    const auto when = [&](const QString &train, Sim::TrainEvent::Kind kind, int index = -2) {
        for (const Sim::TrainEvent &e : events)
            if (e.train == t[train]->id && e.kind == kind && (index == -2 || e.index == index))
                return e.time;
        return -1.0;
    };
    const auto waitedFor = [&](const QString &train, Sim::WaitFor how) {
        for (const Sim::TrainEvent &e : events)
            if (e.train == t[train]->id && e.kind == Sim::TrainEvent::Blocked && e.waitFor == how)
                return true;
        return false;
    };
    runTo(7 * 3600 + 59 * 60 + 30);
    frontsAhead = core.trains()[size_t(t["CA"]->id)].front - core.trains()[size_t(t["CB"]->id)].front;
    check(std::fabs(frontsAhead - 42.5) < 0.01 && core.trains()[size_t(t["CA"]->id)].status == Sim::TrainStatus::Stopped,
          QString("$create /ahead=CB: CA put on its track 2.5 m ahead of CB's front (%1 m between the fronts)")
              .arg(frontsAhead, 0, 'f', 1));
    runTo(8 * 3600 + 60);
    frontAt0801WC = core.trains()[size_t(t["WC"]->id)].front;
    frontAt0801WA = core.trains()[size_t(t["WA"]->id)].front;
    runTo(8 * 3600 + 4 * 60);
    fdStanding = core.trains()[size_t(t["FD"]->id)].status == Sim::TrainStatus::Stopped
            && core.trains()[size_t(t["FD"]->id)].stoppedAt == Sim::StopKind::Created;
    runTo(8 * 3600 + 40 * 60);

    const double arrivedST = when("ST", Sim::TrainEvent::Arrived, 0), leftST = when("ST", Sim::TrainEvent::Departed, 0);
    const double arrivedST2 = when("ST2", Sim::TrainEvent::Arrived, 0), leftST2 = when("ST2", Sim::TrainEvent::Departed, 0);
    check(arrivedST > 0 && std::fabs(leftST - std::max(arrivedST + 60.0, 8 * 3600 + 60.0)) < 0.2 && arrivedST2 > 0
              && std::fabs(leftST2 - std::max(arrivedST2 + 40.0, 8 * 3600 + 60.0)) < 0.2,
          QString("$stoptime: ST stands 60 s (%1 to %2), ST2 40 s (%3 to %4), and leaves no earlier than 08:01")
              .arg(arrivedST).arg(leftST).arg(arrivedST2).arg(leftST2));
    const double arrivedEA = when("EA", Sim::TrainEvent::Arrived, 0), leftEA = when("EA", Sim::TrainEvent::Departed, 0);
    check(arrivedEA > 0 && std::fabs(leftEA - (arrivedEA + 180.0)) < 0.2 && leftEA < 8 * 3600 + 600,
          QString("08*10: EA leaves once it has stood its boarding time (180 s, at %1), before 08:10").arg(leftEA));
    check(when("RQ", Sim::TrainEvent::Arrived, 0) > 0 && when("RQ", Sim::TrainEvent::Arrived, 1) < 0
              && when("RQ", Sim::TrainEvent::Finished) > 0,
          "request stops: RQ stops where needed (Rq1), passes where no one is (Rq2) and ends");
    const double leftES = when("ES", Sim::TrainEvent::Departed, 0), endES = when("ES", Sim::TrainEvent::Finished);
    check(leftES > 0 && endES > leftES + 30.0,
          QString("$endstop: as Open Rails' AI, ES runs on past Es (left %1) to its path's end (%2)").arg(leftES).arg(endES));
    check(fastest.value(t["CR"]->id) <= 10.0 + 1e-3 && fastest.value(t["CR"]->id) > 9.0
              && fastestAfter.value(t["CD"]->id) > 15.0,
          QString("$cruise: CR runs at 10 m/s at most (%1); CD, late more than $maxdelay=1 minute, up to $max (%2 m/s)")
              .arg(fastest.value(t["CR"]->id), 0, 'f', 2).arg(fastestAfter.value(t["CD"]->id), 0, 'f', 2));
    const double startedAS = when("AS", Sim::TrainEvent::Started), startedTR = when("TR", Sim::TrainEvent::Started);
    const double atTr = when("TR", Sim::TrainEvent::Arrived, 0), leftTr = when("TR", Sim::TrainEvent::Departed, 0);
    const double endTR = when("TR", Sim::TrainEvent::Finished);
    check(startedTR > 0 && std::fabs(startedAS - startedTR) < 0.2 && std::fabs(when("AV", Sim::TrainEvent::Started) - atTr) < 0.2
              && std::fabs(when("AW", Sim::TrainEvent::Started) - leftTr) < 0.2
              && std::fabs(when("AD", Sim::TrainEvent::Started) - endTR) < 0.2 && atTr > 8 * 3600,
          QString("$activated trains start as TR starts (AS, not at its 07:50: %1), arrives at Tr (AV, %2), leaves it "
                  "(AW, %3), ends (AD, %4)")
              .arg(startedAS).arg(when("AV", Sim::TrainEvent::Started)).arg(when("AW", Sim::TrainEvent::Started))
              .arg(when("AD", Sim::TrainEvent::Started)));
    const std::vector<Sim::Vehicle> &fb = core.vehiclesOf(t["FB"]->id);
    const std::vector<Sim::Vehicle> &fa = t["FA"]->vehicles;
    check(when("FB", Sim::TrainEvent::Started) >= when("FA", Sim::TrainEvent::Finished) - 1e-6 && fb.size() == 2
              && fa.size() == 2 && fb[0].type == fa[1].type && fb[0].flip != fa[1].flip && fb[1].type == fa[0].type,
          "$forms=FB: FB formed when FA's run ends, of FA's vehicles turned (it starts back the other way)");
    check(std::fabs(frontAt0801WA - t["WA"]->startFront) > 1.0 && waitedFor("WA", Sim::WaitFor::Follow)
              && when("WA", Sim::TrainEvent::Finished) > when("WB", Sim::TrainEvent::Finished),
          "$wait=WB: WA waits where its route joins WB's until WB has passed");
    check(waitedFor("WC", Sim::WaitFor::Follow) && when("WC", Sim::TrainEvent::Finished) > when("WD", Sim::TrainEvent::Finished)
              && frontAt0801WC < 400.0 - 69.0,
          "$wait=WD /notstarted: WC waits for WD before WD has started (08:03), and after");
    check(!waitedFor("WM", Sim::WaitFor::Follow) && waitedFor("WM", Sim::WaitFor::Track)
              && when("WM", Sim::TrainEvent::Finished) > when("WN", Sim::TrainEvent::Finished),
          "$wait=WN /maxdelay=1: WN more than a minute late (booked at Wn 07:59), WM does not wait for it, only for "
          "the track it holds");
    check(fdStanding && std::fabs(when("FD", Sim::TrainEvent::Started) - when("FC", Sim::TrainEvent::Finished)) < 0.2
              && when("FD", Sim::TrainEvent::Finished) > when("TR", Sim::TrainEvent::Finished) + 10.0,
          QString("FD, formed of FC ($triggers) and $activated: on its track from FC's end (%1), standing past its "
                  "08:01 start until TR ends (%2)")
              .arg(when("FD", Sim::TrainEvent::Started)).arg(when("TR", Sim::TrainEvent::Finished)));
    check(when("CA", Sim::TrainEvent::Finished) > 0 && when("CB", Sim::TrainEvent::Finished) > 0
              && when("CA", Sim::TrainEvent::Finished) < when("CB", Sim::TrainEvent::Finished),
          "CA (ahead) and CB run from 08:20 in that order and end");
    check(!waitedFor("WQ", Sim::WaitFor::Follow) && when("WQ", Sim::TrainEvent::Departed, 0) > 0,
          "$wait=WR /owndelay=10: WQ, 200 s late, does not wait for WR");

    delete set;
    ActLib::TimetableSets.remove(id);
    for (int pathId = firstPath; pathId < ActLib::jestpath; ++pathId)
        ActLib::Paths[pathId] = nullptr;
    return done();
}
