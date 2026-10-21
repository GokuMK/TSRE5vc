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
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/OrTimetable.h>

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
    // train referred to that is not in it.
    {
        QDir root(dir.path());
        root.mkpath("route/Paths");
        root.mkpath("trains/consists");
        writeFile(dir.filePath("route/Paths/p1.pat"), "x", Encoding::Utf8);
        writeFile(dir.filePath("trains/consists/C1.con"), "x", Encoding::Utf8);
        const QString file = dir.filePath("check.timetable-or");
        writeFile(file, ";T1;T2\n#path;p1;p9\n#consist;c1;c1 + c9\n#start;05:00;06:00\n#note;$attach=T1;$follow=T7\n",
                  Encoding::Utf8);
        OrTimetable c;
        c.load(file);
        const QVector<OrTimetableProblem> found = CheckTimetable(c, dir.filePath("route"),
                                                                 OrTimetable::FindPath(dir.path(), "TRAINS/CONSISTS"), nullptr);
        QStringList texts;
        for (const OrTimetableProblem &p : found)
            texts << p.train + ": " + p.text;
        check(found.size() == 4 && texts.join("\n").contains("path p9") && texts.join("\n").contains("consist c9")
                  && texts.join("\n").contains("$attach") && texts.join("\n").contains("t7"),
              "the check (folders' case not minded): " + texts.join("; "));
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
    const QString folder = OrTimetable::FindPath(routeDirectory, "ACTIVITIES/OPENRAILS");
    const QString consists = OrTimetable::FindPath(Game::root, "TRAINS/CONSISTS");
    QStringList files;
    for (const QString &entry : QDir(folder).entryList(QDir::Files))
        if (entry.endsWith(".timetable-or", Qt::CaseInsensitive) || entry.endsWith(".timetable_or", Qt::CaseInsensitive))
            files << entry;
    if (files.isEmpty()) {
        qInfo().noquote() << "[tests:timetable-route] the route has no Open Rails timetables";
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
    qInfo().noquote() << QString("timetable-route: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
