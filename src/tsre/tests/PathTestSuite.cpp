#include <tsre/tests/PathTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/Undo.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/ActivityNotifier.h>
#include <QScopedValueRollback>
#include <functional>

using PathTrack::Node;
using PathTrack::NodeType;
using PathTrack::Note;
using PathTrack::Result;

namespace {

// Ids for the test's own section and track shapes, clear of the global
// tsection.dat's.
constexpr int SectionId = 900000;
constexpr int ShapeMainFirst = 900000;
constexpr int ShapeMainSecond = 900001;

// A small track database built by hand: end nodes, junctions with their
// main route, and vector nodes of 10 m sections laid along x.
struct Layout {
    TSectionDAT sections{false, false};
    TSection piece{SectionId, 0, 10.0f, 0.0f};
    TrackShape mainFirst{ShapeMainFirst};
    TrackShape mainSecond{ShapeMainSecond};
    TDB db{&sections, false};
    float nextX = 0.0f;

    Layout() {
        sections.sekcja[SectionId] = &piece;
        mainFirst.mainroute = 0;
        mainSecond.mainroute = 1;
        sections.shape[ShapeMainFirst] = &mainFirst;
        sections.shape[ShapeMainSecond] = &mainSecond;
        db.loaded = true;
    }
    ~Layout() {
        sections.sekcja.erase(SectionId);
        sections.shape.erase(ShapeMainFirst);
        sections.shape.erase(ShapeMainSecond);
    }

    TRnode *add(int id, int type, int in, int out) {
        TRnode *node = new TRnode();
        node->typ = type;
        node->inputPinCount = in;
        node->outputPinCount = out;
        db.trackNodes[id] = node;
        db.iTRnodes = std::max(db.iTRnodes, id);
        return node;
    }
    void end(int id, int track) { add(id, 0, 1, 0)->pins[0].link = track; }
    // A junction: its input pin and two outputs, and which output is main.
    void junction(int id, int in, int first, int second, bool secondIsMain = false) {
        TRnode *node = add(id, 2, 1, 2);
        node->pins[0].link = in;
        node->pins[1].link = first;
        node->pins[2].link = second;
        node->junction.shapeIndex = secondIsMain ? ShapeMainSecond : ShapeMainFirst;
    }
    // A vector node from node `from` (its distance 0) to node `to`.
    void track(int id, int from, int to, int pieces) {
        TRnode *node = add(id, 1, 1, 1);
        node->pins[0].link = from;
        node->pins[1].link = to;
        node->iTrv = pieces;
        node->trVectorSection = new TrackVectorSection[pieces];
        for (int i = 0; i < pieces; ++i) {
            node->trVectorSection[i].sectionIndex = SectionId;
            node->trVectorSection[i].x = nextX;
            nextX += 10.0f;
        }
    }
};

QString describe(const QVector<Node> &nodes) {
    static const char *names[] = {"S", "E", "J", "R", "W", "H"};
    QStringList parts;
    for (const Node &node : nodes) {
        QString part = QString("%1%2").arg(names[int(node.type)]).arg(node.track);
        if (node.isPoint())
            part += QString("@%1%2").arg(node.distance, 0, 'f', 0).arg(node.forward ? "+" : "-");
        parts << part;
    }
    return parts.join(" ");
}

// The passing loop: track end 1, track 10 to junction 2, its main output
// track 11 and its other output track 12 (laid the other way) to junction 3,
// track 13 to track end 4.
void passingLoop(Layout &l) {
    l.end(1, 10);
    l.track(10, 1, 2, 10);
    l.junction(2, 10, 11, 12);
    l.track(11, 2, 3, 20);
    l.track(12, 3, 2, 22);
    l.junction(3, 13, 11, 12);
    l.track(13, 3, 4, 10);
    l.end(4, 13);
}

// A platform's two items on the track, `from` and `to` metres along it.
void platform(Layout &l, int track, int first, float from, float to, const QString &name) {
    TRnode *node = l.db.trackNodes[track];
    QVector<int> refs;
    for (int i = 0; i < node->iTri; ++i)
        refs.push_back(node->trItemRef[i]);
    for (int k = 0; k < 2; ++k) {
        TRitem *item = TRitem::newPlatformItem(first + k, k == 0 ? from : to);
        item->platformTrItemData[1] = first + 1 - k;
        item->platformName = name;
        item->stationName = name;
        l.db.trackItems[first + k] = item;
        refs.push_back(first + k);
    }
    delete[] node->trItemRef;
    node->iTri = refs.size();
    node->trItemRef = new int[refs.size()];
    for (int i = 0; i < refs.size(); ++i)
        node->trItemRef[i] = refs[i];
}

// The passes: platform name, far item, distance.
QString passes(Path &path) {
    QStringList parts;
    for (const Path::PathObject *pass : path.pathObjects)
        parts << QString("%1:%2@%3").arg(pass->name).arg(pass->trItemId).arg(pass->distanceDownPath, 0, 'f', 0);
    return parts.join(" ");
}

bool sameNodes(const QVector<Node> &a, const QVector<Node> &b) {
    if (a.size() != b.size())
        return false;
    for (int k = 0; k < a.size(); ++k) {
        if (a[k].type != b[k].type || a[k].track != b[k].track)
            return false;
        if (a[k].isPoint() && a[k].type != NodeType::Halfway
                && std::fabs(a[k].distance - b[k].distance) > 1.0f)
            return false;
    }
    return true;
}

// The nodes other than halfway nodes. Files leave out some halfway nodes
// between two junctions joined twice (the reading then takes the main
// route) and keep some before an end on the same track; the editor adds and
// drops them by its rule.
QVector<Node> withoutHalfways(const QVector<Node> &nodes, QVector<int> *where = nullptr) {
    QVector<Node> kept;
    for (int k = 0; k < nodes.size(); ++k)
        if (nodes[k].type != NodeType::Halfway) {
            kept << nodes[k];
            if (where != nullptr)
                *where << k;
        }
    return kept;
}

// The tracks a path runs over, in order.
QVector<int> trackSequence(TDB *tdb, const QVector<Node> &nodes) {
    QVector<int> tracks;
    for (const PathTrack::Leg &leg : PathTrack::legs(tdb, nodes))
        if (tracks.isEmpty() || tracks.back() != leg.track)
            tracks << leg.track;
    return tracks;
}

}

int TsreTests::runPathEditSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const QString &name, const QVector<Node> &nodes = {}) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo().noquote() << "PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "FAIL" << name << describe(nodes);
        }
    };
    auto types = [](const QVector<Node> &nodes) {
        QVector<NodeType> list;
        for (const Node &node : nodes)
            list << node.type;
        return list;
    };

    {
        Layout l;
        passingLoop(l);
        TDB *tdb = &l.db;

        // Start Here completes along main routes to a track end, with a
        // halfway node between the two junctions joined twice.
        Result start = PathTrack::startAt(tdb, {10, 20.0f});
        check(start.ok && start.note == Note::None
                  && types(start.nodes) == QVector<NodeType>{NodeType::Start, NodeType::Junction,
                                                             NodeType::Halfway, NodeType::Junction,
                                                             NodeType::End},
              "start completes to the track end", start.nodes);
        check(start.ok && start.nodes[2].track == 11 && std::fabs(start.nodes[2].distance - 100.0f) < 0.01f
                  && start.nodes[4].track == 13 && std::fabs(start.nodes[4].distance - 100.0f) < 0.01f,
              "halfway node on the main route, end at the track end", start.nodes);
        const QVector<PathTrack::Leg> legs = PathTrack::legs(tdb, start.nodes);
        bool legsOk = legs.size() == 4;
        for (const PathTrack::Leg &leg : legs)
            legsOk = legsOk && !leg.broken;
        check(legsOk, "legs of a completed path are all on track", start.nodes);
        check(PathTrack::junctionChoices(tdb, start.nodes, legs).value(2, -1) == 0,
              "main exit is the first output for services");

        // A start close to a track end runs away from it.
        Result nearEnd = PathTrack::startAt(tdb, {13, 80.0f});
        check(nearEnd.ok && !nearEnd.nodes[0].forward && nearEnd.nodes.back().track == 10,
              "start near a track end runs away from it", nearEnd.nodes);

        // Take Other Direction at the facing junction: the other track
        // rejoins the path at the next junction.
        Result other = PathTrack::otherExit(tdb, start.nodes, 1);
        check(other.ok && other.note == Note::Rejoined && other.nodes.size() == 5
                  && other.nodes[2].type == NodeType::Halfway && other.nodes[2].track == 12
                  && !other.nodes[2].forward && other.nodes[4].type == NodeType::End,
              "other exit rejoins, with a halfway node on the other track", other.nodes);
        check(other.ok
                  && PathTrack::junctionChoices(tdb, other.nodes, PathTrack::legs(tdb, other.nodes))
                         .value(2, -1) == 1,
              "other exit is the second output for services");
        check(PathTrack::otherExitPin(tdb, start.nodes, legs, 3) < 0,
              "no other exit at a trailing junction");
        check(!PathTrack::otherExit(tdb, start.nodes, 3).ok, "other exit refused at a trailing junction");

        // Take Other Direction finds its node from the track nearest the
        // pointer: the start on that track, or a facing junction at either
        // end of it, the nearer along the track.
        check(PathTrack::otherDirectionNode(tdb, start.nodes, legs, {10, 90.0f}) == 1,
              "the facing junction at the end of the track");
        check(PathTrack::otherDirectionNode(tdb, start.nodes, legs, {10, 25.0f}) == 0,
              "the start on the track, nearer than the junction");
        check(PathTrack::otherDirectionNode(tdb, start.nodes, legs, {12, 200.0f}) == 1,
              "the facing junction from the track it does not take");
        check(PathTrack::otherDirectionNode(tdb, start.nodes, legs, {11, 100.0f}) == -1,
              "nothing farther than the reach");
        check(PathTrack::otherDirectionNode(tdb, start.nodes, legs, {13, 50.0f}) == -1,
              "nothing on a track ending at a trailing junction");

        // Take Other Direction at the start turns it.
        Result turned = PathTrack::turnStart(tdb, start.nodes);
        check(turned.ok && turned.nodes.size() == 2 && !turned.nodes[0].forward
                  && turned.nodes[1].type == NodeType::End && turned.nodes[1].track == 10
                  && turned.nodes[1].distance < 0.01f,
              "turned start completes to the other track end", turned.nodes);

        // End Here on the path cuts it there; the halfway node goes.
        Result cut = PathTrack::endAt(tdb, start.nodes, {11, 50.0f});
        check(cut.ok && types(cut.nodes) == QVector<NodeType>{NodeType::Start, NodeType::Junction,
                                                             NodeType::End}
                  && cut.nodes[2].track == 11,
              "end on the path cuts it", cut.nodes);

        // End Here off the path routes there.
        Result routed = PathTrack::endAt(tdb, start.nodes, {12, 50.0f});
        check(routed.ok && types(routed.nodes) == QVector<NodeType>{NodeType::Start,
                                                                   NodeType::Junction, NodeType::End}
                  && routed.nodes[2].track == 12 && !routed.nodes[2].forward,
              "end off the path routes to it", routed.nodes);

        // End Here beyond the end extends the path.
        Result shortened = PathTrack::endAt(tdb, start.nodes, {13, 20.0f});
        Result extended = PathTrack::endAt(tdb, shortened.nodes, {13, 80.0f});
        check(shortened.ok && extended.ok && extended.nodes.size() == 5
                  && extended.nodes[4].type == NodeType::End
                  && std::fabs(extended.nodes[4].distance - 80.0f) < 0.01f
                  && extended.nodes[2].type == NodeType::Halfway,
              "end beyond the end extends the path", extended.nodes);

        // Behind the start there is no route without a reversal.
        Result behind = PathTrack::endAt(tdb, start.nodes, {10, 5.0f});
        check(!behind.ok && behind.note == Note::NoRoute, "no route behind the start", behind.nodes);

        // A reversal on the path: the path comes back over the junction to
        // the other track end.
        Result reverse = PathTrack::reverseAt(tdb, start.nodes, {11, 150.0f});
        check(reverse.ok && types(reverse.nodes) == QVector<NodeType>{NodeType::Start,
                                                                     NodeType::Junction,
                                                                     NodeType::Reverse,
                                                                     NodeType::Junction,
                                                                     NodeType::End}
                  && !reverse.nodes[2].forward && reverse.nodes[4].track == 10,
              "reversal comes back to the other track end", reverse.nodes);
        check(!PathTrack::reverseAt(tdb, start.nodes, {12, 50.0f}).ok,
              "reversal off the path refused");
        // Remove Last Reverse Point: the path runs on as if it had not
        // reversed.
        Result unreversed = PathTrack::removeLastReverse(tdb, reverse.nodes);
        check(unreversed.ok && types(unreversed.nodes) == types(start.nodes)
                  && unreversed.nodes.back().track == 13,
              "removing the reversal runs on to the far track end", unreversed.nodes);
        check(!PathTrack::removeLastReverse(tdb, start.nodes).ok
                  && PathTrack::removeLastReverse(tdb, start.nodes).note == Note::NoReverse,
              "no reversal to remove");

        // Wait points: inserted on the path without changing the route; the
        // wait tells the two tracks apart, so the halfway node goes, and
        // comes back when the wait is removed.
        Result wait = PathTrack::waitAt(tdb, start.nodes, {11, 150.0f}, 300);
        check(wait.ok && types(wait.nodes) == QVector<NodeType>{NodeType::Start, NodeType::Junction,
                                                               NodeType::Wait, NodeType::Junction,
                                                               NodeType::End}
                  && wait.nodes[2].waitTime == 300 && wait.nodes[4].track == 13,
              "a wait point keeps the route", wait.nodes);
        check(PathTrack::waitNode(wait.nodes, {11, 170.0f}) == 2
                  && PathTrack::waitNode(wait.nodes, {11, 20.0f}) == -1,
              "the wait point near the pointer's track point");
        Result longer = PathTrack::setWaitTime(tdb, wait.nodes, 2, 900);
        check(longer.ok && longer.nodes[2].waitTime == 900, "a wait time changed", longer.nodes);
        Result unwaited = PathTrack::removeWait(tdb, wait.nodes, 2);
        check(unwaited.ok && types(unwaited.nodes) == types(start.nodes)
                  && unwaited.nodes[2].track == 11,
              "removing the wait brings the halfway node back", unwaited.nodes);
        check(!PathTrack::waitAt(tdb, start.nodes, {12, 50.0f}, 60).ok,
              "a wait point off the path refused");

        // Rebuilt from its own nodes, a path comes out the same; repaired,
        // a path with a junction off the track database is routed again.
        Result again = PathTrack::rebuild(tdb, reverse.nodes, false);
        check(again.ok && again.note == Note::None && types(again.nodes) == types(reverse.nodes),
              "a path rebuilt from its nodes", again.nodes);
        QVector<Node> broken = reverse.nodes;
        broken[1].broken = true;
        check(!PathTrack::rebuild(tdb, broken, false).ok, "a broken path is not rebuilt as it is");
        Result repaired = PathTrack::rebuild(tdb, broken, true);
        check(repaired.ok && repaired.note == Note::Rerouted
                  && types(repaired.nodes) == types(reverse.nodes) && !repaired.nodes[1].broken,
              "a broken junction repaired", repaired.nodes);

        // End Here after the reversal routes from it.
        Result afterReverse = PathTrack::endAt(tdb, reverse.nodes, {10, 60.0f});
        check(afterReverse.ok && afterReverse.nodes.size() == 5
                  && afterReverse.nodes[4].type == NodeType::End
                  && std::fabs(afterReverse.nodes[4].distance - 60.0f) < 0.01f,
              "end after a reversal", afterReverse.nodes);
    }

    {
        // The main route of a junction is the one its track shape names.
        Layout l;
        l.end(1, 10);
        l.track(10, 1, 2, 5);
        l.junction(2, 10, 11, 12, true);
        l.track(11, 2, 3, 5);
        l.end(3, 11);
        l.track(12, 2, 4, 5);
        l.end(4, 12);
        Result start = PathTrack::startAt(&l.db, {10, 10.0f});
        check(start.ok && start.nodes.back().track == 12, "main route from the track shape",
              start.nodes);
    }

    {
        // A balloon: a track leaving a junction and coming back to it.
        Layout l;
        l.end(1, 10);
        l.track(10, 1, 2, 5);
        l.junction(2, 10, 11, 11);
        l.track(11, 2, 2, 30);
        Result start = PathTrack::startAt(&l.db, {10, 10.0f});
        check(start.ok && start.note == Note::None && start.nodes.back().type == NodeType::End
                  && start.nodes.back().track == 10 && !start.nodes.back().forward,
              "balloon: round the loop and back to the track end", start.nodes);
        bool legsOk = true;
        for (const PathTrack::Leg &leg : PathTrack::legs(&l.db, start.nodes))
            legsOk = legsOk && !leg.broken;
        check(legsOk, "balloon legs on track", start.nodes);
    }

    {
        // A circle entered from a spur: completion stops before it repeats.
        Layout l;
        l.end(1, 10);
        l.track(10, 1, 2, 5);
        l.junction(2, 11, 11, 10);
        l.track(11, 2, 2, 30);
        Result start = PathTrack::startAt(&l.db, {10, 10.0f});
        check(start.ok && start.note == Note::StoppedAtLoop && start.nodes.back().track == 11
                  && std::fabs(start.nodes.back().distance - 295.0f) < 0.01f,
              "circle: end before the loop closes", start.nodes);
    }

    {
        // The file the edits write: MSTS flags, one PDP per junction.
        Layout l;
        passingLoop(l);
        TDB *previous = Game::trackDB;
        Game::trackDB = &l.db;
        Result start = PathTrack::startAt(&l.db, {10, 20.0f});
        Result reverse = PathTrack::reverseAt(&l.db, start.nodes, {11, 150.0f});
        Path path("C:/tsre-path-test", "edited.pat", true);
        path.displayName = "Edited path";
        path.apply(reverse.nodes);
        const QString text = path.toText();
        const QStringList lines = text.split('\n');
        int junctionPdps = 0, pointPdps = 0;
        for (const QString &line : lines) {
            if (line.contains("TrackPDP (") && line.trimmed().endsWith("2 0 )"))
                ++junctionPdps;
            if (line.contains("TrackPDP (") && line.trimmed().endsWith("1 1 )"))
                ++pointPdps;
        }
        check(junctionPdps == 1 && pointPdps == 3, "one PDP per junction, points 1 1");
        check(text.contains("TrPathNode ( 00000001 3 4294967295 2 )"),
              "reversal flagged 1, linked to the next node");
        check(text.contains("TrPathNode ( 00000000 4294967295 4294967295 3 )"),
              "end node last");
        check(text.contains("Name ( \"Edited path\" )") && text.contains("TrPathNodes ( 5"),
              "names and node count");
        check(path.isModified(), "an edit marks the path modified");

        // Clone: the same nodes and file, under another name.
        Path copy("C:/tsre-path-test", "copied.pat", true);
        copy.copyFrom(path);
        QString copiedText = copy.toText();
        copiedText.replace("TrPathName ( copied )", "TrPathName ( edited )");
        check(copiedText == text && copy.isModified(), "a clone has the same nodes");

        // A new path without nodes is not written.
        Path empty("C:/tsre-path-test", "empty.pat", true);
        check(!empty.save() && empty.isModified(), "an empty path is not saved");

        // Saved, edited, saved, undone: the file holds the edit, so the
        // path restored from before it is unsaved.
        QTemporaryDir folder;
        Path saved(folder.path(), "saved.pat", true);
        saved.apply(start.nodes);
        const bool firstSave = saved.save();
        const auto clean = saved.snapshot();
        saved.apply(reverse.nodes);
        const bool secondSave = saved.save();
        saved.restore(*clean);
        check(firstSave && secondSave && saved.isModified(), "undo after a save leaves the path unsaved");
        saved.save();
        const auto afterSave = saved.snapshot();
        saved.apply(reverse.nodes);
        saved.restore(*afterSave);
        check(!saved.isModified(), "undo back to the saved state leaves the path saved");
        Game::trackDB = previous;
    }

    {
        // Platform passes, as MSTS counts a stop's SkipCount: a straight
        // 300 m track with platforms A (50-80), B (150-180) and two
        // overlapping ones C (220-250) and D (225-245).
        Layout l;
        l.end(1, 10);
        l.track(10, 1, 2, 30);
        l.end(2, 10);
        platform(l, 10, 100, 50, 80, "A");
        platform(l, 10, 102, 150, 180, "B");
        platform(l, 10, 104, 220, 250, "C");
        platform(l, 10, 106, 225, 245, "D");
        TDB *previous = Game::trackDB;
        Game::trackDB = &l.db;
        QTemporaryDir folder;
        const auto made = [&](const PathTrack::TrackPoint &start, QVector<std::function<Result(const QVector<Node>&)>> steps) {
            Result result = PathTrack::startAt(&l.db, start);
            for (const auto &step : steps)
                result = step(result.nodes);
            return result.nodes;
        };
        Path through(folder.path(), "through.pat", true);
        through.apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 290}); }}));
        check(passes(through) == "A:101@70 B:103@170 C:105@240", "a pass over each platform, overlapping ones once: " + passes(through));
        Path inside(folder.path(), "inside.pat", true);
        inside.apply(made({10, 60}, {[&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 120}); }}));
        check(passes(inside) == "A:101@20", "the platform the path starts in counts: " + passes(inside));
        Path back(folder.path(), "back.pat", true);
        back.apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 120}); },
                                   [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 20}); }}));
        check(passes(back) == "A:101@70 A:100@180", "a switchback passes a platform twice: " + passes(back));
        Path turn(folder.path(), "turn.pat", true);
        turn.apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 65}); },
                                   [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 20}); }}));
        check(passes(turn).isEmpty(), "a reversal inside a platform is no pass: " + passes(turn));
        // Passing paths: an edit keeps those it does not go through.
        Path branch(folder.path(), "branch.pat", true);
        branch.apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 120}); },
                                     [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 20}); }}));
        auto withSide = branch.snapshot();
        PathSnapshot::Passing side;
        side.from = 0;
        side.to = 1;
        withSide->passing = {side};
        branch.restore(*withSide);
        branch.apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 120}); },
                                     [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 40}); }}));
        check(branch.droppedByLastEdit() == 0 && branch.snapshot()->passing.size() == 1,
              "an edit after a passing path keeps it");
        branch.apply(made({10, 30}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 120}); },
                                     [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 40}); }}));
        check(branch.droppedByLastEdit() == 1 && branch.snapshot()->passing.isEmpty(),
              "an edit through a passing path drops it and counts it");

        // Services follow their path; activities and traffic keep their
        // times; undo takes it all back.
        const QString route = folder.path() + "/ROUTES/R";
        for (const QString &dir : {"PATHS", "SERVICES", "TRAFFIC", "ACTIVITIES"})
            QDir().mkpath(route + "/" + dir);
        const QScopedValueRollback<QString> root(Game::root, folder.path());
        const QScopedValueRollback<QString> routeName(Game::route, "R");
        const int firstPath = ActLib::jestpath, firstService = ActLib::jestservice;
        const int firstAct = ActLib::jestact, firstTraffic = ActLib::jesttraffic;
        Path *main = new Path(route + "/PATHS", "Main.pat", true);
        ActLib::AddNewPath(main);
        main->apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 290}); }}));
        Service *service = ActLib::Services.value(ActLib::AddService(route + "/SERVICES", "S.srv", true));
        service->setNewPath("Main");
        service->disableStationStop(2);
        check(service->stationStop.size() == 2 && service->stationStop[1].skipCount == 1,
              "the service stops at A and B");
        Activity *activity = ActLib::Act.value(ActLib::AddAct(route + "/ACTIVITIES", "a.act", true));
        activity->init("R", "a");
        activity->header->startTime[0] = 8;
        activity->createNewPlayerService("S", 8 * 3600);
        ActivityTimetable *timetable = activity->playerServiceDefinition->trafficDefinition;
        timetable->arrivalTime = {29000, 29600};
        timetable->departTime = {29060, 29660};
        Traffic *traffic = ActLib::Traffics.value(ActLib::AddTraffic(route + "/TRAFFIC", "T.trf", true));
        ActivityTimetable *instance = new ActivityTimetable("S", 30000);
        instance->routePath = route;
        traffic->addInstance(instance);
        instance->setTime(30000);
        instance->arrivalTime = {30100, 30700};
        instance->departTime = {30160, 30760};
        QStringList report;
        const QMetaObject::Connection reported = QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::pathFollowed,
                [&](const QString &, const QStringList &lines) { report = lines; });
        // The path now ends before B.
        Undo::Clear();
        Undo::StateBegin();
        Undo::PushPathData(main);
        main->apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 140}); }}));
        Undo::StateEnd();
        check(service->stationStop.size() == 1 && service->stationStop[0].platformStartID == 101,
              "a stop the path no longer passes is removed");
        check(timetable->arrivalTime == QVector<int>{29000} && timetable->departTime == QVector<int>{29060}
              && instance->arrivalTime == QVector<int>{30100} && instance->departTime == QVector<int>{30160},
              "the activity and traffic keep the times of the stop kept");
        check(report.size() == 2, "the edit is reported: the service, the timetables following");
        Undo::UndoLast();
        // Undo reads the activity and traffic back: new objects.
        timetable = activity->playerServiceDefinition->trafficDefinition;
        instance = traffic->service.value(0);
        check(service->stationStop.size() == 2 && timetable->arrivalTime == QVector<int>{29000, 29600}
              && instance->departTime == QVector<int>{30160, 30760},
              "undo takes the path, the service, the activity and the traffic back");
        // A switchback: the stop stays at its pass.
        Undo::StateBegin();
        Undo::PushPathData(main);
        main->apply(made({10, 10}, {[&](const QVector<Node> &n) { return PathTrack::reverseAt(&l.db, n, {10, 120}); },
                                    [&](const QVector<Node> &n) { return PathTrack::endAt(&l.db, n, {10, 20}); }}));
        Undo::StateEnd();
        check(service->stationStop.size() == 1 && service->stationStop[0].skipCount == 0
              && std::fabs(service->stationStop[0].distanceDownPath - 70) < 0.5f,
              "a stop keeps the first pass of a switchback platform");
        service->enableStationStop(1);
        check(service->stationStop.size() == 2 && service->stationStop[1].skipCount == 1
              && timetable->arrivalTime.size() == 2 && timetable->arrivalTime[0] == 29000
              && timetable->arrivalTime[1] == timetable->departTime[0],
              "a new stop at the second pass takes the time of the stop before");
        Undo::Clear();

        // Times move with a new start.
        activity->setStartTime(9, 0, 0);
        check(timetable->time == 9 * 3600 && timetable->arrivalTime[0] == 29000 + 3600,
              "the activity's start moves the player's timetable");
        traffic->setInstanceTime(instance, 31000);
        check(instance->time == 31000 && instance->arrivalTime[0] == 30100 + 1000,
              "a new start time moves a traffic instance's timetable");

        QObject::disconnect(reported);
        for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
        for (int id = firstService; id < ActLib::jestservice; ++id) ActLib::Services[id] = nullptr;
        for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
        for (int id = firstPath; id < ActLib::jestpath; ++id) ActLib::Paths[id] = nullptr;
        Game::trackDB = previous;
    }

    qInfo().noquote() << QString("path-edit: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runPathRoundTripSuite(const QString &outputDirectory, bool verbose) {
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "path-roundtrip requires a route with one TDB (--game-root, --route)";
        return 1;
    }
    const QString originalName = Game::routeName;
    TDB *originalTrackDB = Game::trackDB;
    Game::routeName = databases.first().completeBaseName();
    TSectionDAT sections(false, false);
    if (QFileInfo::exists(sourceRoute.filePath("tsection.dat")))
        sections.loadRoute(false);
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded) {
        Game::routeName = originalName;
        return 1;
    }
    Game::trackDB = &database;

    QTemporaryDir temporary;
    const QString written = outputDirectory.isEmpty() ? temporary.path() : outputDirectory;
    QDir().mkpath(written);
    const QDir paths(sourceRoute.filePath("PATHS"));
    int files = 0, roundTripFailures = 0, brokenPaths = 0, replayed = 0, replayable = 0,
        repairable = 0, repairedPaths = 0,
        replayFailures = 0;
    for (const QFileInfo &info : paths.entryInfoList({"*.pat"}, QDir::Files, QDir::Name)) {
        ++files;
        Path original(paths.path(), info.fileName());
        if (original.loaded != 1) {
            ++roundTripFailures;
            qWarning().noquote() << "FAIL read" << info.fileName();
            continue;
        }
        // Every node in the file is read.
        {
            QFile source(info.filePath());
            if (!source.open(QIODevice::ReadOnly))
                continue;
            const QString sourceText = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(source.readAll().constData() + 2),
                (source.size() - 2) / 2);
            const int inFile = sourceText.count("TrPathNode (", Qt::CaseInsensitive);
            if (inFile != original.trPathNode.size()) {
                ++roundTripFailures;
                qWarning().noquote() << "FAIL read" << info.fileName() << original.trPathNode.size()
                                     << "of" << inFile << "nodes";
                continue;
            }
        }
        const QVector<Node> nodes = original.nodes();
        const bool broken = original.isBroken();
        brokenPaths += broken ? 1 : 0;
        if (verbose) {
            int brokenNodes = 0, brokenLegs = 0;
            for (const Node &node : nodes)
                brokenNodes += node.broken ? 1 : 0;
            for (const PathTrack::Leg &leg : original.legs())
                brokenLegs += leg.broken ? 1 : 0;
            qInfo().noquote() << "path" << info.fileName() << nodes.size() << "nodes," << brokenNodes
                              << "not on track," << brokenLegs << "legs broken";
        }

        // Written and read again: the same nodes, flags, links and places.
        QFile out(written + "/" + info.fileName());
        if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream stream(&out);
            stream.setEncoding(QStringConverter::Utf16);
            stream.setGenerateByteOrderMark(true);
            stream << original.toText();
        }
        out.close();
        Path again(written, info.fileName());
        QStringList problems;
        if (again.trPathNode.size() != original.trPathNode.size())
            problems << QString("nodes %1 -> %2").arg(original.trPathNode.size()).arg(again.trPathNode.size());
        // Node by node along the main links: files may list their nodes in
        // another order than the path runs; the editor writes them in path
        // order.
        auto chain = [](const Path &p) {
            QVector<int> order;
            QSet<int> seen;
            for (unsigned int k = 0; k < unsigned(p.trPathNode.size()) && !seen.contains(int(k));) {
                seen.insert(int(k));
                order << int(k);
                k = p.trPathNode[int(k)][1];
            }
            return order;
        };
        const QVector<int> chainA = chain(original);
        const QVector<int> chainB = chain(again);
        if (chainA.size() != chainB.size())
            problems << QString("path nodes %1 -> %2").arg(chainA.size()).arg(chainB.size());
        for (int k = 0; problems.isEmpty() && k < chainA.size(); ++k) {
            const unsigned int *a = original.trPathNode[chainA[k]];
            const unsigned int *b = again.trPathNode[chainB[k]];
            if (a[0] != b[0] || (a[2] == 4294967295u) != (b[2] == 4294967295u))
                problems << QString("node %1 flags or passing link").arg(k);
            if (a[3] >= unsigned(original.trackPdp.size()) || b[3] >= unsigned(again.trackPdp.size()))
                continue;
            const float *pa = original.trackPdp[int(a[3])];
            const float *pb = again.trackPdp[int(b[3])];
            for (int i = 0; i < 7; ++i)
                if (std::fabs(pa[i] - pb[i]) > (i >= 2 && i <= 4 ? 0.01f : 0.0f)) {
                    problems << QString("node %1 PDP value %2: %3 -> %4").arg(k).arg(i).arg(pa[i]).arg(pb[i]);
                    break;
                }
        }
        if (again.trPathName != original.trPathName || again.displayName != original.displayName
                || again.trPathStart != original.trPathStart || again.trPathEnd != original.trPathEnd
                || again.trPathFlags != original.trPathFlags)
            problems << "names or flags";
        if (!problems.isEmpty()) {
            ++roundTripFailures;
            qWarning().noquote() << "FAIL round trip" << info.fileName() << problems.join("; ");
        }

        // Rebuilt with the tool's edits (PathTrack::rebuild), for paths on
        // track without passing paths; checked here node by node and track
        // by track, halfway nodes left out.
        if (broken) {
            // Repair Path on it: a whole path, routed again where it broke.
            // A repair may be refused (a start off the track) but never
            // leaves broken track.
            const Result repaired = PathTrack::rebuild(&database, nodes, true);
            bool onTrack = repaired.ok;
            for (const PathTrack::Leg &leg : PathTrack::legs(&database, repaired.nodes))
                onTrack = onTrack && !leg.broken;
            ++repairable;
            const bool whole = onTrack && repaired.note != Note::RepairStopped;
            repairedPaths += whole ? 1 : 0;
            qInfo().noquote() << "repair" << info.fileName()
                              << (whole ? "whole" : onTrack ? "stopped" : repaired.ok ? "BROKEN" : "refused")
                              << "note" << int(repaired.note) << nodes.size() << "->"
                              << repaired.nodes.size() << "nodes";
            if (repaired.ok && !onTrack) {
                ++replayFailures;
                qWarning().noquote() << "FAIL repair leaves broken track" << info.fileName();
            }
        }
        const bool simple = !broken && nodes.size() >= 2 && nodes.first().type == NodeType::Start
                && nodes.last().type == NodeType::End && original.trPathNode.size() == nodes.size();
        if (!simple)
            continue;
        ++replayable;
        const QVector<Node> target = withoutHalfways(nodes);
        const Result built = PathTrack::rebuild(&database, nodes, false);
        QString why;
        if (!built.ok)
            why = QString("stopped (note %1): ours %2").arg(int(built.note))
                    .arg(describe(withoutHalfways(built.nodes).mid(0, 6)));
        else if (!sameNodes(withoutHalfways(built.nodes), target))
            why = "nodes differ";
        else if (trackSequence(&database, built.nodes) != trackSequence(&database, nodes))
            why = "tracks differ";
        if (built.ok && why.isEmpty()) {
            ++replayed;
            if (verbose)
                qInfo().noquote() << "PASS rebuilt" << info.fileName();
        } else {
            ++replayFailures;
            qWarning().noquote() << "FAIL rebuild" << info.fileName()
                                 << (why.isEmpty() ? QString("note %1").arg(int(built.note)) : why);
        }
    }
    Game::trackDB = originalTrackDB;
    Game::routeName = originalName;
    qInfo().noquote() << QString("path-roundtrip %1: %2 paths, %3 broken, round trip failures %4; "
                                 "rebuilt %5 of %6, repaired %8 of %9, failures %7")
                         .arg(Game::route).arg(files).arg(brokenPaths).arg(roundTripFailures)
                         .arg(replayed).arg(replayable).arg(replayFailures).arg(repairedPaths).arg(repairable);
    return roundTripFailures == 0 && replayFailures == 0 ? 0 : 1;
}
