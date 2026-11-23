#include <tsre/tests/TrackShapeAnalysisTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <tsre/Game.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/ProceduralTrackPolicy.h>
#include <tsre/procedural/TrackProfileFilters.h>
#include <tsre/procedural/TrackShapeAnalysis.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>

namespace {

constexpr float Pi = 3.14159265358979323846f;

float radians(float degrees) {
    return degrees * Pi / 180.0f;
}

struct Checks {
    const char *suite;
    bool verbose;
    int passed = 0;
    int failed = 0;
    void operator()(bool condition, const QString &name) {
        if(condition){
            passed++;
            if(verbose)
                qInfo().noquote() << QString("[tests:%1] PASS").arg(suite) << name;
        } else {
            failed++;
            qWarning().noquote() << QString("[tests:%1] FAIL").arg(suite) << name;
        }
    }
    int finish() {
        qInfo().noquote() << QString("[tests:%1] cases=%2 passed=%3 failed=%4")
                .arg(suite).arg(passed + failed).arg(passed).arg(failed);
        return failed == 0 ? 0 : 1;
    }
};

// Track sections made here, as tsection.dat holds them.
struct Sections {
    std::unordered_map<int, TSection*> map;
    int next = 1;
    ~Sections() {
        for(auto &entry : map)
            delete entry.second;
    }
    int straight(float length) {
        const int id = next++;
        map[id] = new TSection(id, 0, length, 0);
        return id;
    }
    // Degrees, positive to the right.
    int curve(float radius, float degrees) {
        const int id = next++;
        map[id] = new TSection(id, 1, radians(degrees), radius);
        return id;
    }
};

struct PathSpec {
    float x = 0;
    float z = 0;
    float rotDeg = 0;
    QVector<int> sections;
};

// A track shape made here.
struct Shape {
    TrackShape shape;
    Shape(int id, const QVector<PathSpec> &paths, bool tunnel = false, bool road = false) {
        shape.id = id;
        shape.numpaths = paths.size();
        shape.path = new TrackShape::SectionIdx[std::max(1, int(paths.size()))];
        for(int i = 0; i < paths.size(); i++){
            TrackShape::SectionIdx &path = shape.path[i];
            path.pos[0] = paths[i].x;
            path.pos[1] = 0;
            path.pos[2] = paths[i].z;
            path.rotDeg = paths[i].rotDeg;
            path.n = paths[i].sections.size();
            for(int k = 0; k < path.n; k++)
                path.sect[k] = paths[i].sections[k];
        }
        shape.tunnelshape = tunnel;
        shape.roadshape = road;
    }
    ~Shape() {
        delete[] shape.path;
    }
    Shape(const Shape &) = delete;
    Shape &operator=(const Shape &) = delete;
};

PathSpec path(float x, float z, float rotDeg, const QVector<int> &sections) {
    PathSpec spec;
    spec.x = x;
    spec.z = z;
    spec.rotDeg = rotDeg;
    spec.sections = sections;
    return spec;
}

bool near(float a, float b, float tolerance = 0.001f) {
    return std::abs(a - b) <= tolerance;
}

const int GeometryKinds = TrackShapeAnalysis::Single | TrackShapeAnalysis::Parallel
        | TrackShapeAnalysis::Complex;

// One CSV line's fields (quotes as review.py's csv module writes them).
QStringList csvFields(const QString &line) {
    QStringList fields;
    QString field;
    bool quoted = false;
    for(int i = 0; i < line.size(); i++){
        const QChar c = line[i];
        if(quoted){
            if(c == '"' && i + 1 < line.size() && line[i + 1] == '"'){
                field.append('"');
                i++;
            } else if(c == '"'){
                quoted = false;
            } else {
                field.append(c);
            }
        } else if(c == '"'){
            quoted = true;
        } else if(c == ','){
            fields.append(field);
            field.clear();
        } else {
            field.append(c);
        }
    }
    fields.append(field);
    return fields;
}

// review.py on the content root's tsection.dat: its geometry groups against
// TrackShapeAnalysis at review.py's tie (2.6 m, no gap).
void reviewChecks(Checks &check) {
    const QString csvPath = qEnvironmentVariable("TSRE_TSECTION_REVIEW_CSV");
    const QString tsectionPath = Game::root + "/GLOBAL/tsection.dat";
    if(csvPath.isEmpty() || !QFileInfo(tsectionPath).isFile()){
        qInfo().noquote() << "[tests:track-shape-analysis] review.py comparison skipped"
                          << "(needs --game-root and TSRE_TSECTION_REVIEW_CSV)";
        return;
    }
    QFile csv(csvPath);
    check(csv.open(QIODevice::ReadOnly | QIODevice::Text), "review: CSV opens");
    if(!csv.isOpen())
        return;
    QTextStream in(&csv);
    const QStringList header = csvFields(in.readLine());
    const int idxColumn = header.indexOf("idx");
    const int fileColumn = header.indexOf("file");
    const int pathsColumn = header.indexOf("paths");
    const int switchesColumn = header.indexOf("switches");
    const int crossingsColumn = header.indexOf("rail_crossings");
    const int overlapColumn = header.indexOf("tie_overlap");
    check(idxColumn >= 0 && pathsColumn >= 0 && switchesColumn >= 0
            && crossingsColumn >= 0 && overlapColumn >= 0, "review: CSV columns");

    Game::route.clear();
    TSectionDAT *tsection = new TSectionDAT(false, false);
    int rows = 0, compared = 0, missing = 0, mismatches = 0;
    QHash<int, int> counts;
    QStringList examples;
    while(!in.atEnd()){
        const QStringList fields = csvFields(in.readLine());
        if(fields.size() < header.size())
            continue;
        rows++;
        const int idx = fields[idxColumn].toInt();
        const auto found = tsection->shape.find(idx);
        if(found == tsection->shape.end() || found->second == nullptr){
            missing++;
            continue;
        }
        const TrackShapeAnalysis analysis = TrackShapeAnalysis::analyse(
                *found->second, tsection->sekcja, TrackShapeAnalysis::DefaultTieLength);
        if(!analysis.valid){
            missing++;
            continue;
        }
        compared++;
        const int paths = fields[pathsColumn].toInt();
        int expected = TrackShapeAnalysis::Parallel;
        if(paths == 1)
            expected = TrackShapeAnalysis::Single;
        else if(fields[overlapColumn].toDouble() > 0 || fields[crossingsColumn].toInt() > 0
                || fields[switchesColumn].toInt() > 0)
            expected = TrackShapeAnalysis::Complex;
        const int actual = analysis.kinds & GeometryKinds;
        counts[actual]++;
        if(actual != expected){
            mismatches++;
            if(examples.size() < 20)
                examples.append(QString("%1 %2: review %3, TSRE %4").arg(idx)
                        .arg(fileColumn >= 0 ? fields[fileColumn] : QString())
                        .arg(TrackShapeAnalysis::kindNames(expected))
                        .arg(TrackShapeAnalysis::kindNames(actual)));
        }
    }
    qInfo().noquote() << QString("[tests:track-shape-analysis] review %1: %2 rows, %3 compared, "
            "%4 without geometry; Single %5, Parallel %6, Complex %7; %8 differ")
            .arg(QFileInfo(csvPath).fileName()).arg(rows).arg(compared).arg(missing)
            .arg(counts.value(TrackShapeAnalysis::Single))
            .arg(counts.value(TrackShapeAnalysis::Parallel))
            .arg(counts.value(TrackShapeAnalysis::Complex)).arg(mismatches);
    for(const QString &example : examples)
        qInfo().noquote() << "[tests:track-shape-analysis]   " << example;
    check(compared > 0, "review: shapes compared");
    check(mismatches == 0, "review: every shape's geometry kind as review.py's");

    // Named shapes (Build 00054 has them all): their reference lines as
    // tieplan.py draws them (the plans in assets-gen reports/complex-tracks).
    QHash<QString, const TrackShape*> byName;
    for(const auto &entry : tsection->shape)
        if(entry.second != nullptr)
            byName.insert(entry.second->filename.toLower(), entry.second);
    auto named = [&](const QString &name, const std::function<bool(const TrackShapeAnalysis &)> &test,
            const QString &what) {
        const TrackShape *shape = byName.value(name.toLower());
        if(shape == nullptr){
            qInfo().noquote() << "[tests:track-shape-analysis] not in this tsection.dat:" << name;
            return;
        }
        check(test(TrackShapeAnalysis::analyse(*shape, tsection->sekcja)), name + ": " + what);
    };
    using Type = TrackShapeAnalysis::Reference::Type;
    named("A1tPnt7_5dLft.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].paths.size() == 2
                && a.groups[0].reference.type == Type::Path
                && a.paths[a.groups[0].reference.path].first().x == 0
                && a.paths[a.groups[0].reference.path].last().x == 0;
    }, "one group, the straight on the axis its reference");
    named("A1tFrog7_5dLft.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::Path;
    }, "one group, a path its reference");
    named("A1tXover7_5d.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::Bisector
                && near(a.groups[0].reference.crossingAngle, 7.5f, 0.01f);
    }, "the bisector of a 7.5 degree diamond");
    named("A1tXover60d.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::Bisector
                && near(a.groups[0].reference.crossingAngle, 60.0f, 0.01f);
    }, "60 degrees still shared");
    named("A1tXover90d.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::None
                && near(a.groups[0].reference.crossingAngle, 90.0f, 0.01f);
    }, "90 degrees: no shared ties");
    named("A1tYPnt10d.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::Mean;
    }, "a wye: the mean of its two paths");
    named("A2tFrog7_5dDbl.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].paths.size() == 4 && a.independent.isEmpty();
    }, "the scissors middle: one group of four");
    named("A1tDblSlip7_5d.s", [](const TrackShapeAnalysis &a) {
        return a.groups.size() == 1 && a.groups[0].reference.type == Type::Bisector;
    }, "the double slip's diamond: a bisector");
    named("A2t25mStrt.s", [](const TrackShapeAnalysis &a) {
        return a.groups.isEmpty() && a.independent.size() == 2
                && (a.kinds & GeometryKinds) == TrackShapeAnalysis::Parallel;
    }, "two separate tracks");
}

}

int TsreTests::runTrackShapeAnalysisSuite(bool verbose) {
    Checks check{"track-shape-analysis", verbose};
    using Type = TrackShapeAnalysis::Reference::Type;

    // Sampling: MSTS curves turn right for positive angles.
    {
        Sections sections;
        Shape right(1, {path(0, 0, 0, {sections.curve(100, 90)})});
        Shape left(2, {path(0, 0, 0, {sections.curve(100, -90)})});
        const QVector<TrackShapeAnalysis::Point> r = TrackShapeAnalysis::samplePath(right.shape, 0, sections.map);
        const QVector<TrackShapeAnalysis::Point> l = TrackShapeAnalysis::samplePath(left.shape, 0, sections.map);
        check(!r.isEmpty() && near(r.last().x, 100) && near(r.last().z, 100)
                && near(r.last().heading, Pi / 2) && near(r.last().distance, 50 * Pi, 0.01f),
                "a 90 degree curve to the right ends 100 m right, 100 m on");
        check(!l.isEmpty() && near(l.last().x, -100) && near(l.last().z, 100),
                "a negative angle turns left");
        Shape turned(3, {path(1, 2, 90, {sections.straight(10)})});
        const QVector<TrackShapeAnalysis::Point> t = TrackShapeAnalysis::samplePath(turned.shape, 0, sections.map);
        check(!t.isEmpty() && near(t.last().x, 11) && near(t.last().z, 2),
                "a path's start and rotation (positive to the right)");
        const TrackShapeAnalysis::Point half = TrackShapeAnalysis::at(t, 5);
        check(near(half.x, 6) && near(half.z, 2), "a point along a sampled path");
    }

    // Single, parallel, flags, a missing section.
    {
        Sections sections;
        const int straight = sections.straight(25);
        Shape single(1, {path(0, 0, 0, {straight})});
        Shape parallel(2, {path(-2.25f, 0, 0, {straight}), path(2.25f, 0, 0, {straight})});
        Shape tunnel(3, {path(0, 0, 0, {straight})}, true);
        Shape road(4, {path(-1.75f, 0, 0, {straight}), path(1.75f, 0, 0, {straight})}, false, true);
        Shape missing(5, {path(0, 0, 0, {999})}, true);
        const TrackShapeAnalysis a = TrackShapeAnalysis::analyse(single.shape, sections.map);
        check(a.valid && a.kinds == TrackShapeAnalysis::Single && a.groups.isEmpty()
                && a.independent == QVector<int>({0}), "one path: Single");
        const TrackShapeAnalysis b = TrackShapeAnalysis::analyse(parallel.shape, sections.map);
        check(b.kinds == TrackShapeAnalysis::Parallel && b.groups.isEmpty()
                && b.independent == QVector<int>({0, 1}), "4.5 m apart: Parallel, both independent");
        const TrackShapeAnalysis c = TrackShapeAnalysis::analyse(tunnel.shape, sections.map);
        check(c.kinds == (TrackShapeAnalysis::Single | TrackShapeAnalysis::Tunnel), "TunnelShape: Tunnel");
        const TrackShapeAnalysis d = TrackShapeAnalysis::analyse(road.shape, sections.map);
        check(d.kinds == (TrackShapeAnalysis::Parallel | TrackShapeAnalysis::Road),
                "RoadShape: Road (lanes 3.5 m apart: Parallel)");
        const TrackShapeAnalysis e = TrackShapeAnalysis::analyse(missing.shape, sections.map);
        check(!e.valid && e.kinds == TrackShapeAnalysis::Tunnel && e.paths.isEmpty(),
                "a missing section: no geometry, the flags only");
    }

    // The group distance: a tie's length plus the gap between tie ends.
    {
        Sections sections;
        const int straight = sections.straight(20);
        Shape close(1, {path(0, 0, 0, {straight}), path(2.65f, 0, 0, {straight})});
        check(TrackShapeAnalysis::analyse(close.shape, sections.map).kinds == TrackShapeAnalysis::Complex,
                "2.65 m apart: closer than 2.6 + 0.1, one group");
        check(TrackShapeAnalysis::analyse(close.shape, sections.map, 2.6f).kinds == TrackShapeAnalysis::Parallel,
                "2.65 m apart at review.py's 2.6 m: separate");
    }

    // Points: the straight on the axis is the reference; a switch on one of
    // two tracks leaves the other independent.
    {
        Sections sections;
        const int straight = sections.straight(25);
        const int diverging = sections.curve(190.713f, -7.5f);
        Shape points(1, {path(0, 0, 0, {diverging}), path(0, 0, 0, {straight})});
        const TrackShapeAnalysis a = TrackShapeAnalysis::analyse(points.shape, sections.map);
        check(a.kinds == TrackShapeAnalysis::Complex && a.groups.size() == 1
                && a.groups[0].paths == QVector<int>({0, 1}) && a.independent.isEmpty(),
                "points: one group of both paths");
        check(a.groups.size() == 1 && a.groups[0].reference.type == Type::Path
                && a.groups[0].reference.path == 1,
                "points: the straight path on the axis is the reference");
        Shape twoTrack(2, {path(0, 0, 0, {diverging}), path(0, 0, 0, {straight}),
                           path(4.5f, 0, 0, {straight})});
        const TrackShapeAnalysis b = TrackShapeAnalysis::analyse(twoTrack.shape, sections.map);
        check(b.groups.size() == 1 && b.groups[0].paths == QVector<int>({0, 1})
                && b.independent == QVector<int>({2}),
                "points on one of two tracks: a group and an independent track");
    }

    // Diamonds: the bisector up to 60 degrees, no shared ties above.
    {
        Sections sections;
        const int straight = sections.straight(20);
        auto diamond = [&](int id, float degrees) {
            const float h = radians(degrees);
            return new Shape(id, {path(0, -10, 0, {straight}),
                                  path(-10 * std::sin(h), -10 * std::cos(h), degrees, {straight})});
        };
        Shape *small = diamond(1, 7.5f);
        const TrackShapeAnalysis a = TrackShapeAnalysis::analyse(small->shape, sections.map);
        bool throughCrossing = false;
        if(a.groups.size() == 1)
            for(const TrackShapeAnalysis::Point &p : a.groups[0].reference.line)
                throughCrossing = throughCrossing || (near(p.x, 0, 0.06f) && near(p.z, 0, 0.06f));
        check(a.groups.size() == 1 && a.groups[0].reference.type == Type::Bisector
                && near(a.groups[0].reference.crossingAngle, 7.5f, 0.001f)
                && near(a.groups[0].reference.line.first().heading, radians(3.75f), 1e-4f)
                && throughCrossing,
                "a 7.5 degree diamond: the bisector through the crossing");
        if(a.groups.size() == 1){
            const QVector<TrackShapeAnalysis::Point> &line = a.groups[0].reference.line;
            check(line.first().z < -10 && line.last().z > 9.9f,
                    "the bisector spans the whole group, a metre over");
        }
        Shape *sixty = diamond(2, 60);
        Shape *sixtyOne = diamond(3, 61);
        Shape *square = diamond(4, 90);
        const TrackShapeAnalysis b = TrackShapeAnalysis::analyse(sixty->shape, sections.map);
        const TrackShapeAnalysis c = TrackShapeAnalysis::analyse(sixtyOne->shape, sections.map);
        const TrackShapeAnalysis d = TrackShapeAnalysis::analyse(square->shape, sections.map);
        check(b.groups.size() == 1 && b.groups[0].reference.type == Type::Bisector,
                "60 degrees: still shared (decision 9)");
        check(c.groups.size() == 1 && c.groups[0].reference.type == Type::None,
                "61 degrees: the first track's ties through");
        check(d.groups.size() == 1 && d.groups[0].reference.type == Type::None
                && d.groups[0].reference.path == 0 && near(d.groups[0].reference.crossingAngle, 90, 0.001f),
                "90 degrees: path 0 (heading 0) keeps its ties");
        check(TrackShapeAnalysis::analyse(sixtyOne->shape, sections.map, 2.7f, 65).groups[0].reference.type
                == Type::Bisector, "MaxSharedAngle moves the limit");
        delete small;
        delete sixty;
        delete sixtyOne;
        delete square;
    }

    // A wye: no straight path, the mean of the two.
    {
        Sections sections;
        Shape wye(1, {path(0, 0, 0, {sections.curve(150, -10)}), path(0, 0, 0, {sections.curve(150, 10)})});
        const TrackShapeAnalysis a = TrackShapeAnalysis::analyse(wye.shape, sections.map);
        bool central = a.groups.size() == 1 && !a.groups[0].reference.line.isEmpty();
        if(central)
            for(const TrackShapeAnalysis::Point &p : a.groups[0].reference.line)
                central = central && near(p.x, 0, 1e-3f) && near(p.heading, 0, 1e-4f);
        check(a.groups.size() == 1 && a.groups[0].reference.type == Type::Mean
                && a.groups[0].reference.path == 0 && a.groups[0].reference.otherPath == 1 && central,
                "a symmetric wye: the mean line runs straight up the middle");
    }

    check(TrackShapeAnalysis::kindFromName("complex") == TrackShapeAnalysis::Complex
            && TrackShapeAnalysis::kindFromName(" Tunnel ") == TrackShapeAnalysis::Tunnel
            && TrackShapeAnalysis::kindFromName("Junction") == 0
            && TrackShapeAnalysis::kindNames(TrackShapeAnalysis::Single | TrackShapeAnalysis::Road)
               == "Single, Road", "kind names");

    reviewChecks(check);
    return check.finish();
}

int TsreTests::runTrackProfileFiltersSuite(bool verbose) {
    Checks check{"track-profile-filters", verbose};
    using Filters = TrackProfileFilters;

    // The design's example (docs/tasks/tracks/17, section 1).
    const QString example =
            "SIMISA@@@@@@@@@@JINX0p0t______\n"                                   // 1
            "\n"                                                                 // 2
            "TrackProfileFilters (\n"                                            // 3
            "    Filter (\n"                                                     // 4
            "        Shapes ( \"*Turntable*\" \"*Roundh*\" \"*trnsfer*\" )\n"
            "        Profile ( DISABLED )\n"
            "    )\n"
            "    Filter (\n"                                                     // 8
            "        Shapes ( \"A*Tun*\" )\n"
            "        Profile ( PL_60E1_Tunnel )\n"
            "    )\n"
            "    Filter (\n"                                                     // 12
            "        Shapes ( \"A*Brdg*\" )\n"
            "        Profile ( PL_60E1_Bridge )\n"
            "    )\n"
            "    Filter (\n"                                                     // 16
            "        Shapes ( \"*Pnt7_5d*Slip*\" )\n"
            "        Profile ( TrProfile_DB30 )\n"
            "        ExcludedParts ( Tie Fastener Bed )\n"
            "    )\n"
            "    Filter (\n"                                                     // 21
            "        Shapes ( \"*DblSlip7_5d*\" )\n"
            "        Profile ( TrProfile_DB30 )\n"
            "        IncludedTrackShape ( \"A1tPnt7_5dLftSlip.s\"   0      0  -10      0   )\n"
            "        IncludedTrackShape ( \"A1tPnt7_5dRgtSlip.s\"   1.632  0   -9.893 -7.5 )\n"
            "        IncludedTrackShape ( \"A1tPnt7_5dRgtSlip.s\"  -1.632  0   14.893 172.5 )\n"
            "        IncludedTrackShape ( \"A1tPnt7_5dLftSlip.s\"   0      0   15     180   )\n"
            "    )\n"
            "    Filter (\n"                                                     // 29
            "        Shapes ( \"DB/DB30_*\" )\n"
            "        Profile ( TrProfile_DB30 )\n"
            "    )\n"
            "    Filter (\n"                                                     // 33
            "        Shapes ( \"A*Concrete*\" )\n"
            "        Profile ( PL_60E1_PS94_3D )\n"
            "    )\n"
            "    Filter (\n"                                                     // 37
            "        Shapes ( \"A*\" )\n"
            "        Profile ( PL_49E1_Wood_3D )\n"
            "    )\n"
            ")\n";
    QStringList diagnostics;
    const QVector<Filters::Filter> filters = Filters::parse(example, &diagnostics);
    check(filters.size() == 8 && diagnostics.isEmpty(), "the design's example: 8 filters, no word");
    if(filters.size() == 8){
        check(filters[0].line == 4 && filters[1].line == 8 && filters[4].line == 21
                && filters[7].line == 37, "each filter's line");
        check(filters[0].disabled && filters[0].profile.isEmpty() && filters[0].shapes.size() == 3,
                "Profile ( DISABLED )");
        check(filters[1].profile == "PL_60E1_Tunnel" && filters[1].parts == Filters::AllParts
                && filters[1].kinds == 0, "a plain filter: every part, any kind");
        check(filters[3].parts == (Filters::Rail | Filters::ConductorRail | Filters::RackRail | Filters::Other),
                "ExcludedParts ( Tie Fastener Bed )");
        const QVector<Filters::IncludedTrackShape> &included = filters[4].includedTrackShapes;
        check(included.size() == 4 && included[1].shape == "A1tPnt7_5dRgtSlip.s"
                && near(included[1].position[0], 1.632f) && near(included[1].position[2], -9.893f)
                && near(included[1].rotationDeg, -7.5f) && near(included[3].rotationDeg, 180)
                && included[0].index == -1, "IncludedTrackShape ( shape x y z rotY )");
        check(filters[4].includedTrackShapeParts == Filters::DefaultIncludedTrackShapeParts
                && Filters::DefaultIncludedTrackShapeParts
                   == (Filters::Tie | Filters::Fastener | Filters::Bed | Filters::Other),
                "IncludedTrackShapeParts: no rails by default (decision 12)");
        check(filters[1].description() == "line 8: Shapes ( \"A*Tun*\" ) Profile ( PL_60E1_Tunnel )"
                && filters[0].description().endsWith("Profile ( DISABLED )"),
                "a filter's description (the properties panel's)");

        // The first match takes it (decision 25).
        Filters::set(filters);
        int asked = 0;
        auto kinds = [&]() { asked++; return int(TrackShapeAnalysis::Complex); };
        auto profileOf = [&](const QString &name) {
            const Filters::Filter *filter = Filters::match(name, kinds);
            return filter == nullptr ? QString("none")
                    : filter->disabled ? QString("DISABLED") : filter->profile;
        };
        check(profileOf("DB/DB30_A1tPnt7_5dLftSlip.s") == "TrProfile_DB30"
                && Filters::match("DB/DB30_A1tPnt7_5dLftSlip.s", kinds)->line == 16,
                "a slip piece: the slip filter, above the skin's");
        check(profileOf("DB\\DB30_A1t25mStrt.s") == "TrProfile_DB30", "'\\' read as '/'");
        check(profileOf("db/db30_a1t25mstrt.S") == "TrProfile_DB30", "any case (decision 21)");
        check(profileOf("A1t100mStrtTun.s") == "PL_60E1_Tunnel", "a tunnel by name");
        check(profileOf("A1t25mStrtConcrete.s") == "PL_60E1_PS94_3D", "concrete before the catch-all");
        check(profileOf("A1t25mStrt.s") == "PL_49E1_Wood_3D", "the catch-all last");
        check(profileOf("A1t30mTurntable.s") == "DISABLED", "turntables static");
        check(profileOf("SR_1tStr_c_025m.s") == "none", "no filter: Open Rails' guess decides");
        check(asked == 0, "no Kind in the filters: the shape is never analysed");
    }

    // Kind: a filter passed over when the shape is not of its kinds.
    {
        const QString text =
                "TrackProfileFilters (\n"
                "  Filter ( Shapes ( \"A*\" ) Kind ( Complex Tunnel ) Profile ( DISABLED ) )\n"
                "  Filter ( Shapes ( \"A*\" ) Profile ( Wood ) )\n"
                ")\n";
        QStringList words;
        Filters::set(Filters::parse(text, &words));
        int asked = 0;
        int shapeKinds = TrackShapeAnalysis::Single;
        auto kinds = [&]() { asked++; return shapeKinds; };
        const Filters::Filter *single = Filters::match("A1t25mStrt.s", kinds);
        shapeKinds = TrackShapeAnalysis::Complex;
        const Filters::Filter *complex = Filters::match("A1tPnt7_5dLft.s", kinds);
        shapeKinds = TrackShapeAnalysis::Single | TrackShapeAnalysis::Tunnel;
        const Filters::Filter *tunnel = Filters::match("A1t25mStrtTun.s", kinds);
        check(words.isEmpty() && single != nullptr && single->profile == "Wood"
                && complex != nullptr && complex->disabled && tunnel != nullptr && tunnel->disabled
                && asked == 3, "Kind ( Complex Tunnel ): any of, asked once a match");
        check(Filters::match("SR_1t.s", kinds) == nullptr && asked == 3,
                "a name no pattern matches: the kinds never asked");
    }

    // What the file says wrong is left out with a word.
    {
        const QString text =
                "TrackProfileFilters (\n"                                            // 1
                "  Filter ( Shapes ( \"A*\" ) Kind ( Junction ) Profile ( P ) )\n"   // 2
                "  Filter ( Shapes ( \"B*\" ) )\n"                                   // 3
                "  Filter ( Profile ( P ) )\n"                                       // 4
                "  Filter ( Shapes ( \"C*\" ) Profile ( P ) IncludedParts ( Rail Tie Blade ) ExcludedParts ( Tie ) )\n"
                "  Filter ( Shapes ( \"D*\" ) Profile ( P ) IncludedTrackShape ( \"x.s\" 1 2 3 ) )\n"
                "  Filter ( Shapes ( \"E*\" ) Profile ( P ) IncludedTrackShape ( 22037 1m 0 2 90deg ) Colour ( red ) )\n"
                "  Wildcard ( \"F*\" )\n"
                ")\n";
        QStringList words;
        const QVector<Filters::Filter> read = Filters::parse(text, &words);
        check(read.size() == 4, "filters without Shapes or Profile left out");
        check(words.size() == 7, QString("one word each (%1): %2").arg(words.size()).arg(words.join(" | ")));
        check(words.filter("line 2: Kind: unknown value \"Junction\"").size() == 1, "an unknown Kind, with its line");
        if(read.size() == 4){
            check(read[0].kinds == 0, "an unknown Kind matches as none given");
            check(read[1].parts == Filters::Rail, "IncludedParts less ExcludedParts; an unknown part ignored");
            check(read[2].includedTrackShapes.isEmpty(), "IncludedTrackShape needs five values");
            check(read[3].includedTrackShapes.size() == 1 && read[3].includedTrackShapes[0].index == 22037
                    && near(read[3].includedTrackShapes[0].position[0], 1)
                    && near(read[3].includedTrackShapes[0].rotationDeg, 90),
                    "IncludedTrackShape by tsection index, with units");
        }
        QStringList none;
        check(Filters::parse("Filters ( )", &none).isEmpty()
                && none.contains("Missing TrackProfileFilters block"), "no TrackProfileFilters block");
        check(Filters::partFromName("conductorrail") == Filters::ConductorRail
                && Filters::partNames(Filters::Rail | Filters::Bed) == "Rail Bed", "part names");
    }

    // Loaded with the route's profiles, again on Reload Track Profiles.
    {
        QTemporaryDir root;
        const QString route = root.path() + "/ROUTES/FILTERS";
        QDir().mkpath(route + "/TRACKPROFILES");
        auto write = [](const QString &path, const QString &text) {
            QFile file(path);
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            file.write(text.toUtf8());
        };
        const QString profile =
                "SIMISA@@@@@@@@@@JINX0p0t______\n"
                "TrProfile ( Name ( \"Wood\" ) ObjectType ( TRACK MAIN )\n"
                " LOD ( CutoffRadius ( 100 ) LODItem ( Name ( \"rails\" ) TexName ( \"r.ace\" ) ShaderName ( TexDiff )\n"
                "  Polyline ( Name ( \"top\" ) DeltaTexCoord ( 0 0.2 )\n"
                "   Vertex ( Position ( -0.8 0.2 0 ) Normal ( 0 1 0 ) TexCoord ( 0 0 ) )\n"
                "   Vertex ( Position ( 0.8 0.2 0 ) Normal ( 0 1 0 ) TexCoord ( 1 0 ) ) ) ) ) )\n";
        write(route + "/TRACKPROFILES/Wood.stf", profile);
        OrtsTrackProfileCatalog::load(route, true);
        check(Filters::filters().isEmpty(), "no trackprofiles.dat: no filters");
        write(route + "/trackprofiles.dat",
              "SIMISA@@@@@@@@@@JINX0p0t______\nTrackProfileFilters ( Filter ( Shapes ( \"A*\" ) Profile ( Wood ) ) )\n");
        OrtsTrackProfileCatalog::load(route, true);
        check(Filters::filters().size() == 1 && Filters::filters()[0].profile == "Wood",
                "trackprofiles.dat read with the route's profiles");
        write(route + "/trackprofiles.dat",
              "TrackProfileFilters ( Filter ( Shapes ( \"A*Tun*\" ) Profile ( DISABLED ) )"
              " Filter ( Shapes ( \"A*\" ) Profile ( Wood ) ) )\n");
        OrtsTrackProfileCatalog::load(route, false);
        check(Filters::filters().size() == 1, "the same route again: not read again");
        OrtsTrackProfileCatalog::load(route, true);
        check(Filters::filters().size() == 2, "a forced reload reads it again");
        // UTF-16, as MSTS tools write their files.
        QFile utf16(route + "/trackprofiles.dat");
        utf16.open(QIODevice::WriteOnly | QIODevice::Truncate);
        const QString text = "TrackProfileFilters ( Filter ( Shapes ( \"B*\" ) Profile ( Wood ) ) )\n";
        utf16.write("\xff\xfe", 2);
        utf16.write(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        utf16.close();
        OrtsTrackProfileCatalog::load(route, true);
        check(Filters::filters().size() == 1 && Filters::filters()[0].shapes == QStringList({"B*"}),
                "a UTF-16 file");

        // The shape's kinds, kept per shape until the next load.
        Sections sections;
        Shape points(77, {path(0, 0, 0, {sections.curve(190.713f, -7.5f)}),
                          path(0, 0, 0, {sections.straight(25)})});
        check(Filters::kindsOf(points.shape, sections.map) == TrackShapeAnalysis::Complex,
                "a junction's kind: Complex");
        points.shape.numpaths = 1;
        check(Filters::kindsOf(points.shape, sections.map) == TrackShapeAnalysis::Complex,
                "kept until the next load");
        OrtsTrackProfileCatalog::load(route, true);
        check(Filters::kindsOf(points.shape, sections.map) == TrackShapeAnalysis::Single,
                "found again after a load");
        OrtsTrackProfileCatalog::load(QString(), true);
    }

    // Forced: a road object without a template falls back to RdProfile, as
    // road DynTracks (decision 19); track to DefaultTrack.
    {
        const ProceduralTrackResolution road = ProceduralTrackPolicy::resolve(
                ProceduralTracksMode::Forced, QString(), {"RdProfile", "RdProfile_left"}, "RdProfile");
        const ProceduralTrackResolution noRoad = ProceduralTrackPolicy::resolve(
                ProceduralTracksMode::Forced, QString(), {"Asphalt"}, "RdProfile");
        const ProceduralTrackResolution track = ProceduralTrackPolicy::resolve(
                ProceduralTracksMode::Forced, QString(), {"DefaultTrack", "RdProfile"});
        const ProceduralTrackResolution filtered = ProceduralTrackPolicy::resolve(
                ProceduralTracksMode::Forced, "Wood", {"wood"}, "RdProfile");
        const ProceduralTrackResolution superelevation = ProceduralTrackPolicy::resolve(
                ProceduralTracksMode::Superelevation, QString(), {"RdProfile"}, "RdProfile");
        check(road.backend == ProceduralTrackBackend::Procedural && road.templateName == "RdProfile",
                "Forced road: RdProfile");
        check(noRoad.backend == ProceduralTrackBackend::Fallback && noRoad.missingDefault
                && noRoad.defaultName == "RdProfile", "no RdProfile: static, said once");
        check(track.backend == ProceduralTrackBackend::Procedural && track.templateName == "DefaultTrack",
                "Forced track: DefaultTrack");
        check(filtered.backend == ProceduralTrackBackend::Procedural && filtered.templateName == "wood",
                "a filter's family resolves as a template");
        check(superelevation.backend == ProceduralTrackBackend::Fallback,
                "Superelevation: no default for objects without a template");
    }

    Filters::set({});
    return check.finish();
}
