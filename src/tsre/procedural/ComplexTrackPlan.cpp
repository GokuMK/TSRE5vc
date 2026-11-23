/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/ComplexTrackPlan.h>

#include <QHash>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tsre/procedural/TrackProfileFilters.h>

namespace {

using Point = ComplexTrackPlan::Point;
using Line = ComplexTrackPlan::Line;
using Tie = ComplexTrackPlan::Tie;
using Range = ComplexTrackPlan::Range;

constexpr float Pi = 3.14159265358979323846f;
// Two rail lines whose headings differ less than this (either way) can lie
// on each other; more, they cross.
constexpr float ParallelRadians = 3.0f * Pi / 180.0f;
// Bed cross-sections along a line.
constexpr float BedStep = 0.5f;

struct Corner {
    float x;
    float z;
};

std::array<Corner, 4> corners(const Tie &tie) {
    const float ux = std::cos(tie.heading);
    const float uz = -std::sin(tie.heading);
    const float vx = std::sin(tie.heading);
    const float vz = std::cos(tie.heading);
    const float hl = tie.length / 2;
    const float hw = tie.width / 2;
    std::array<Corner, 4> result;
    const int signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for(int i = 0; i < 4; i++)
        result[i] = Corner{tie.centre[0] + signs[i][0] * hl * ux + signs[i][1] * hw * vx,
                           tie.centre[1] + signs[i][0] * hl * uz + signs[i][1] * hw * vz};
    return result;
}

// The undirected angle between two headings, 0 to pi/2.
float undirected(float a, float b) {
    float d = std::remainder(a - b, Pi);
    return std::abs(d);
}

// The distance from a point to a polyline and the polyline's heading there.
float distanceTo(const Point &p, const Line &line, float *heading = nullptr) {
    float best = std::numeric_limits<float>::max();
    for(int i = 0; i + 1 < line.size(); i++){
        const Point &a = line[i];
        const Point &b = line[i + 1];
        const float rx = b.x - a.x;
        const float rz = b.z - a.z;
        const float r2 = rx * rx + rz * rz;
        float u = r2 > 1e-12f ? ((p.x - a.x) * rx + (p.z - a.z) * rz) / r2 : 0.0f;
        u = std::min(1.0f, std::max(0.0f, u));
        const float dx = p.x - (a.x + u * rx);
        const float dz = p.z - (a.z + u * rz);
        const float d = std::sqrt(dx * dx + dz * dz);
        if(d < best){
            best = d;
            if(heading != nullptr)
                *heading = a.heading + u * (b.heading - a.heading);
        }
    }
    return best;
}

// Runs of samples not hidden, as distance ranges; shorter than `least`
// dropped.
QVector<Range> runs(const Line &line, const QVector<bool> &hidden, float least) {
    QVector<Range> result;
    int start = -1;
    for(int i = 0; i <= line.size(); i++){
        const bool shown = i < line.size() && !hidden[i];
        if(shown && start < 0)
            start = i;
        if(!shown && start >= 0){
            const float from = line[start].distance;
            const float to = line[i - 1].distance;
            if(to - from >= least)
                result.append(Range(from, to));
            start = -1;
        }
    }
    return result;
}

bool lays(const ComplexTrackPlan::Path &path, unsigned part) {
    return (path.parts & part) != 0;
}

void planTies(const ComplexTrackPlan &plan, ComplexTrackPlan::Group &group) {
    const ComplexTrackPlan::Settings &st = plan.settings;
    QVector<int> members;
    for(int i : group.paths)
        if(lays(plan.paths[i], TrackProfileFilters::Tie))
            members.append(i);
    if(members.isEmpty())
        return;
    QVector<int> order;
    for(int i : group.reference.order)
        if(members.contains(i))
            order.append(i);
    const float overhang = (st.tieLength - st.gauge) / 2;
    QHash<int, QPair<Line, Line>> gaugeLines;
    for(int i : members)
        gaugeLines.insert(i, qMakePair(ComplexTrackPlan::offsetLine(plan.paths[i].line, -st.gauge / 2),
                                       ComplexTrackPlan::offsetLine(plan.paths[i].line, st.gauge / 2)));
    QVector<Tie> &ties = group.ties;
    auto fits = [&](const Tie &tie) {
        for(const Tie &other : ties)
            if(ComplexTrackPlan::overlaps(tie, other))
                return false;
        return true;
    };
    auto makeTie = [&](Tie::Kind kind, float x, float z, float heading, float length,
            const QVector<int> &paths) {
        Tie tie;
        tie.kind = kind;
        tie.centre[0] = x;
        tie.centre[1] = z;
        tie.heading = heading;
        tie.length = length;
        tie.width = st.tieWidth;
        tie.paths = paths;
        return tie;
    };

    if(group.reference.type == TrackShapeAnalysis::Reference::Type::None){
        // Steeper than maxSharedAngle: the first path's ties through,
        // lengthened under the other tracks' rails; the others' where they
        // fit.
        for(int k = 0; k < order.size(); k++){
            const int i = order[k];
            const Line &line = plan.paths[i].line;
            for(float s = st.tiePhase; s <= line.last().distance + 1e-6f; s += st.tieSpacing){
                const Point p = TrackShapeAnalysis::at(line, s);
                const float nx = std::cos(p.heading);
                const float nz = -std::sin(p.heading);
                float t0 = -st.tieLength / 2;
                float t1 = st.tieLength / 2;
                Tie::Kind kind = Tie::Kind::Own;
                QVector<int> paths = {i};
                if(k == 0 && st.steepLengthen){
                    QVector<QPair<float, QPair<float, int>>> others;
                    for(int j : members){
                        if(j == i)
                            continue;
                        for(const Line *rail : {&gaugeLines[j].first, &gaugeLines[j].second})
                            for(const ComplexTrackPlan::Hit &hit
                                    : ComplexTrackPlan::lineHits(p.x, p.z, nx, nz, *rail))
                                others.append(qMakePair(std::abs(hit.t), qMakePair(hit.t, j)));
                    }
                    std::sort(others.begin(), others.end());
                    for(const auto &other : others){
                        const float t = other.second.first;
                        const float a = std::min(t0, t - overhang);
                        const float b = std::max(t1, t + overhang);
                        if(b - a > st.maxBearerLength)
                            break;
                        if(a < t0 || b > t1 || std::abs(t) <= st.tieLength / 2){
                            t0 = a;
                            t1 = b;
                            kind = Tie::Kind::Bearer;
                            if(!paths.contains(other.second.second))
                                paths.append(other.second.second);
                        }
                    }
                }
                const float tc = (t0 + t1) / 2;
                const Tie tie = makeTie(kind, p.x + tc * nx, p.z + tc * nz, p.heading, t1 - t0, paths);
                if(fits(tie))
                    ties.append(tie);
            }
        }
        return;
    }

    const Line &reference = group.reference.line;
    const int referencePath = group.reference.type == TrackShapeAnalysis::Reference::Type::Path
            ? group.reference.path : -1;
    QHash<int, float> covered;
    for(int i : members)
        covered.insert(i, -1.0f);
    auto cover = [&](int i, float x, float z, float nx, float nz) {
        const QVector<ComplexTrackPlan::Hit> hits = ComplexTrackPlan::lineHits(x, z, nx, nz, plan.paths[i].line);
        if(!hits.isEmpty())
            covered[i] = std::max(covered[i], hits.first().distance);
    };
    for(float s = st.tiePhase; s <= reference.last().distance + 1e-6f; s += st.tieSpacing){
        const Point p = TrackShapeAnalysis::at(reference, s);
        const float nx = std::cos(p.heading);
        const float nz = -std::sin(p.heading);
        // Each path's rails on the cut line.
        QMap<int, QVector<float>> spans;
        for(int i : members)
            for(const Line *rail : {&gaugeLines[i].first, &gaugeLines[i].second})
                for(const ComplexTrackPlan::Hit &hit : ComplexTrackPlan::lineHits(p.x, p.z, nx, nz, *rail))
                    spans[i].append(hit.t);
        if(spans.isEmpty())
            continue;
        struct Unit {
            int path;
            QVector<float> t;
        };
        QVector<Unit> units;
        for(auto it = spans.begin(); it != spans.end(); ++it){
            QVector<float> t = it.value();
            std::sort(t.begin(), t.end());
            units.append(Unit{it.key(), t});
        }
        std::sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) { return a.t.first() < b.t.first(); });
        // Whole tracks clustered: must share where tie ends would collide.
        using Cluster = QVector<QPair<float, int>>;
        auto sorted = [](Cluster c) { std::sort(c.begin(), c.end()); return c; };
        auto clusterOf = [](const Unit &unit) {
            Cluster c;
            for(float t : unit.t)
                c.append(qMakePair(t, unit.path));
            return c;
        };
        QVector<Cluster> clusters = {clusterOf(units.first())};
        for(int u = 1; u < units.size(); u++){
            float last = -std::numeric_limits<float>::max();
            for(const auto &h : clusters.last())
                last = std::max(last, h.first);
            if(units[u].t.first() - last < 2 * overhang + st.tieGap)
                clusters.last() = sorted(clusters.last() + clusterOf(units[u]));
            else
                clusters.append(clusterOf(units[u]));
        }
        // Prefer shared: neighbours merged while the bearer stays short
        // enough.
        QVector<Cluster> merged = {clusters.first()};
        for(int c = 1; c < clusters.size(); c++){
            const float span = clusters[c].last().first - merged.last().first().first + 2 * overhang;
            if(span <= st.maxBearerLength)
                merged.last() = merged.last() + clusters[c];
            else
                merged.append(clusters[c]);
        }
        for(int c = 0; c < merged.size(); c++){
            const Cluster &cluster = merged[c];
            // Past a path's end the cut line may still cross one of its
            // rails: such a rail rides on another track's tie, but alone it
            // lays none (a stub).
            QHash<int, int> railsOf;
            for(const auto &h : cluster)
                railsOf[h.second]++;
            bool whole = false;
            for(int count : railsOf)
                whole = whole || count >= 2;
            if(!whole)
                continue;
            const float t0 = cluster.first().first - overhang;
            const float t1 = cluster.last().first + overhang;
            QVector<int> pathset;
            for(const auto &h : cluster)
                if(!pathset.contains(h.second))
                    pathset.append(h.second);
            std::sort(pathset.begin(), pathset.end());
            const float tc = (t0 + t1) / 2;
            // One track alone that is not the reference: its own tie, square
            // to itself, where that fits; else one square to the reference.
            if(st.ownTies && pathset.size() == 1 && pathset.first() != referencePath){
                const int i = pathset.first();
                const QVector<ComplexTrackPlan::Hit> hits =
                        ComplexTrackPlan::lineHits(p.x, p.z, nx, nz, plan.paths[i].line);
                if(!hits.isEmpty()){
                    const ComplexTrackPlan::Hit &hit = hits.first();
                    const Tie own = makeTie(Tie::Kind::Own, p.x + hit.t * nx, p.z + hit.t * nz,
                            hit.heading, st.tieLength, {i});
                    bool clear = fits(own);
                    for(int c2 = 0; clear && c2 < merged.size(); c2++){
                        if(c2 == c)
                            continue;
                        const float m = (merged[c2].first().first + merged[c2].last().first) / 2;
                        const Tie other = makeTie(Tie::Kind::Square, p.x + m * nx, p.z + m * nz, p.heading,
                                merged[c2].last().first - merged[c2].first().first + 2 * overhang, {});
                        clear = !ComplexTrackPlan::overlaps(own, other);
                    }
                    covered[i] = std::max(covered[i], hit.distance);
                    if(clear){
                        ties.append(own);
                        continue;
                    }
                }
            }
            ties.append(makeTie(pathset.size() > 1 ? Tie::Kind::Bearer : Tie::Kind::Square,
                    p.x + tc * nx, p.z + tc * nz, p.heading, t1 - t0, pathset));
            for(int i : pathset)
                cover(i, p.x, p.z, nx, nz);
        }
    }
    if(!st.ownTies)
        return;
    // Past the stations: each path's own ties where they fit.
    for(int i : order){
        if(i == referencePath)
            continue;
        const Line &line = plan.paths[i].line;
        const float start = covered[i] >= 0 ? covered[i] + st.tieSpacing : st.tiePhase;
        for(float s = start; s <= line.last().distance + 1e-6f; s += st.tieSpacing){
            const Point p = TrackShapeAnalysis::at(line, s);
            const Tie own = makeTie(Tie::Kind::Own, p.x, p.z, p.heading, st.tieLength, {i});
            if(fits(own))
                ties.append(own);
        }
    }
}

// Where a line of `path` lies on a line of an earlier path in `earlier`
// (headings within ParallelRadians, closer than `width`).
QVector<bool> lyingOn(const Line &line, const QVector<const Line*> &earlier, float width) {
    QVector<bool> hidden(line.size(), false);
    for(int k = 0; k < line.size(); k++)
        for(const Line *other : earlier){
            float heading = 0;
            if(distanceTo(line[k], *other, &heading) < width
                    && undirected(line[k].heading, heading) < ParallelRadians){
                hidden[k] = true;
                break;
            }
        }
    return hidden;
}

QVector<ComplexTrackPlan::BedSlice> slicesAlong(const Line &line, float from, float to) {
    QVector<ComplexTrackPlan::BedSlice> slices;
    if(to - from < 1e-4f)
        return slices;
    const int steps = std::max(1, int(std::ceil((to - from) / BedStep)));
    for(int k = 0; k <= steps; k++){
        const float s = from + (to - from) * k / steps;
        const Point p = TrackShapeAnalysis::at(line, s);
        ComplexTrackPlan::BedSlice slice;
        slice.position[0] = p.x;
        slice.position[1] = p.z;
        slice.heading = p.heading;
        slice.distance = s;
        slices.append(slice);
    }
    return slices;
}

// A path ending (or starting) less than half a metre short of the reference
// line's end plane, carried on straight to it: the next piece's bed starts
// at that plane (points' diverging track ends 0.1 m before the straight's).
Line reachingPlanes(const Line &line, const Line &reference) {
    constexpr float Reach = 0.5f;
    Line result = line;
    const Point &first = reference.first();
    const Point &last = reference.last();
    auto along = [](const Point &p, const Point &plane) {
        return (p.x - plane.x) * std::sin(plane.heading) + (p.z - plane.z) * std::cos(plane.heading);
    };
    const float shortAtEnd = -along(line.last(), last);
    if(shortAtEnd > 1e-4f && shortAtEnd <= Reach){
        Point p = line.last();
        const float cosine = std::max(0.2f, std::cos(p.heading - last.heading));
        const float d = shortAtEnd / cosine + 0.01f;
        p.x += d * std::sin(p.heading);
        p.z += d * std::cos(p.heading);
        p.distance += d;
        result.append(p);
    }
    const float shortAtStart = along(line.first(), first);
    if(shortAtStart > 1e-4f && shortAtStart <= Reach){
        Point p = line.first();
        const float cosine = std::max(0.2f, std::cos(p.heading - first.heading));
        const float d = shortAtStart / cosine + 0.01f;
        p.x -= d * std::sin(p.heading);
        p.z -= d * std::cos(p.heading);
        p.distance -= d;
        result.prepend(p);
    }
    return result;
}

void planBed(const ComplexTrackPlan &plan, ComplexTrackPlan::Group &group) {
    QVector<int> bedPaths;
    for(int i : group.paths)
        if(lays(plan.paths[i], TrackProfileFilters::Bed))
            bedPaths.append(i);
    if(bedPaths.isEmpty())
        return;
    if(group.reference.type == TrackShapeAnalysis::Reference::Type::None){
        // Steep crossings: a bed per path (they overlap, phase 1).
        for(int i : bedPaths)
            group.pathBeds.append(slicesAlong(plan.paths[i].line, 0, plan.paths[i].line.last().distance));
        return;
    }
    const Line &reference = group.reference.line;
    QHash<int, Line> bedLines;
    for(int i : bedPaths)
        bedLines.insert(i, reachingPlanes(plan.paths[i].line, reference));
    for(const ComplexTrackPlan::BedSlice &candidate : slicesAlong(reference, 0, reference.last().distance)){
        const float nx = std::cos(candidate.heading);
        const float nz = -std::sin(candidate.heading);
        float left = std::numeric_limits<float>::max();
        float right = -std::numeric_limits<float>::max();
        for(int i : bedPaths)
            for(const ComplexTrackPlan::Hit &hit
                    : ComplexTrackPlan::lineHits(candidate.position[0], candidate.position[1], nx, nz,
                                                 bedLines[i])){
                left = std::min(left, hit.t);
                right = std::max(right, hit.t);
            }
        if(left > right)
            continue;
        ComplexTrackPlan::BedSlice slice = candidate;
        slice.left = left;
        slice.right = right;
        group.bed.append(slice);
    }
    // A path's parts before the reference line's start or past its end: its
    // own bed there.
    const Point &first = reference.first();
    const Point &last = reference.last();
    for(int i : bedPaths){
        const Line &line = plan.paths[i].line;
        QVector<bool> inside(line.size(), true);
        for(int k = 0; k < line.size(); k++){
            const Point &p = line[k];
            const float before = (p.x - first.x) * std::sin(first.heading) + (p.z - first.z) * std::cos(first.heading);
            const float after = (p.x - last.x) * std::sin(last.heading) + (p.z - last.z) * std::cos(last.heading);
            inside[k] = before >= -0.01f && after <= 0.01f;
        }
        for(const Range &range : runs(line, inside, 0.25f))
            group.pathBeds.append(slicesAlong(line, range.first, range.second));
    }
}

using RailPiece = ComplexTrackPlan::RailPiece;
using RailLine = ComplexTrackPlan::RailLine;

// Distance ranges less the cut ones (both sorted, cut ones may overlap).
QVector<Range> without(const QVector<Range> &ranges, QVector<Range> cuts, float least) {
    std::sort(cuts.begin(), cuts.end());
    QVector<Range> result;
    for(Range range : ranges){
        float from = range.first;
        for(const Range &cut : cuts){
            if(cut.second <= from || cut.first >= range.second)
                continue;
            if(cut.first - from >= least)
                result.append(Range(from, cut.first));
            from = std::max(from, cut.second);
        }
        if(range.second - from >= least)
            result.append(Range(from, range.second));
    }
    return result;
}

// A line through points (headings and distances from its segments).
Line polyline(const QVector<QPair<float, float>> &points) {
    Line line;
    float distance = 0;
    for(int i = 0; i < points.size(); i++){
        Point p;
        p.x = points[i].first;
        p.z = points[i].second;
        if(i > 0)
            distance += std::hypot(p.x - line.last().x, p.z - line.last().z);
        p.distance = distance;
        const int a = std::min(i, int(points.size()) - 2);
        p.heading = std::atan2(points[a + 1].first - points[a].first, points[a + 1].second - points[a].second);
        line.append(p);
    }
    return line;
}

// The stretch of a line between two distances, its distances from 0.
Line stretchOf(const Line &line, float from, float to) {
    Line result;
    if(line.isEmpty() || to <= from)
        return result;
    result.append(TrackShapeAnalysis::at(line, from));
    for(const Point &p : line)
        if(p.distance > from && p.distance < to)
            result.append(p);
    result.append(TrackShapeAnalysis::at(line, to));
    for(Point &p : result)
        p.distance -= from;
    return result;
}

// Phase 2 (design section 7) for one group's rail lines (rails firstRail
// to the end): switch blades against their stock rails, crossings into
// frogs (noses, wing rails, check rails) and obtuse crossings, then the
// pieces to sweep.
void planRails(ComplexTrackPlan &plan, const ComplexTrackPlan::Group &group, int firstRail,
        float headWidth) {
    const ComplexTrackPlan::Settings &st = plan.settings;
    const float w = headWidth;
    const float f = st.flangeway;
    const int lastRail = plan.rails.size();
    auto laysPart = [&](int r, unsigned part) { return lays(plan.paths[plan.rails[r].path], part); };
    auto railOf = [&](int path, int side) {
        for(int r = firstRail; r < lastRail; r++)
            if(plan.rails[r].path == path && plan.rails[r].side == side)
                return r;
        return -1;
    };
    const QVector<int> &order = group.reference.order;

    // Switches: two paths starting together. Of each pair of rails lying
    // on each other at the toe, the one that ends between the two tracks'
    // centres is the blade, the other the stock rail; the main route's
    // blades lie closed.
    struct Blade {
        int stock = -1;
        bool closed = false;
    };
    QHash<int, Blade> blades;
    for(int a = 0; a < order.size(); a++)
        for(int b = a + 1; b < order.size(); b++){
            const int i = order[a], j = order[b];
            const Line &li = plan.paths[i].line, &lj = plan.paths[j].line;
            if(std::hypot(li[0].x - lj[0].x, li[0].z - lj[0].z) > 0.05f
                    || std::abs(std::remainder(li[0].heading - lj[0].heading, 2 * Pi)) > 1e-3f)
                continue;
            const float end = std::min(li.last().distance, lj.last().distance);
            const Point ci = TrackShapeAnalysis::at(li, end), cj = TrackShapeAnalysis::at(lj, end);
            const float span2 = (cj.x - ci.x) * (cj.x - ci.x) + (cj.z - ci.z) * (cj.z - ci.z);
            if(span2 < 0.04f)
                continue;
            const int main = plan.paths[i].main ? i : plan.paths[j].main ? j : i;
            for(int side : {-1, 1}){
                const int ri = railOf(i, side), rj = railOf(j, side);
                if(ri < 0 || rj < 0 || !laysPart(ri, TrackProfileFilters::Rail) || !laysPart(rj, TrackProfileFilters::Rail))
                    continue;
                auto between = [&](int r) {
                    const Line &line = plan.rails[r].line;
                    const float length = plan.paths[plan.rails[r].path].line.last().distance;
                    const Point x = TrackShapeAnalysis::at(line, end * line.last().distance / std::max(1e-3f, length));
                    const float t = ((x.x - ci.x) * (cj.x - ci.x) + (x.z - ci.z) * (cj.z - ci.z)) / span2;
                    return t > 0 && t < 1;
                };
                const int blade = between(ri) ? ri : between(rj) ? rj : -1;
                if(blade < 0 || blades.contains(blade))
                    continue;
                blades.insert(blade, Blade{blade == ri ? rj : ri, plan.rails[blade].path == main});
            }
        }

    // Lying on an earlier drawn line (dual gauge's common rail): drawn
    // once; a blade and its stock rail both drawn.
    QHash<int, QVector<Range>> drawn;
    QVector<int> earlier;
    for(int r = firstRail; r < lastRail; r++){
        if(!laysPart(r, TrackProfileFilters::Rail))
            continue;
        QVector<const Line*> lines;
        for(int e : earlier){
            const bool partners = (blades.contains(r) && blades[r].stock == e)
                    || (blades.contains(e) && blades[e].stock == r);
            if(!partners)
                lines.append(&plan.rails[e].line);
        }
        drawn.insert(r, runs(plan.rails[r].line, lyingOn(plan.rails[r].line, lines, w), 0.05f));
        earlier.append(r);
    }

    // Crossings between drawn rails of different paths.
    QHash<int, QVector<Range>> cuts;
    auto addPiece = [&](RailPiece::Kind kind, int path, int side, const Line &line) {
        if(line.size() < 2 || line.last().distance < 0.05f)
            return;
        RailPiece piece;
        piece.kind = kind;
        piece.path = path;
        piece.side = side;
        piece.line = line;
        piece.from = 0;
        piece.to = line.last().distance;
        plan.railPieces.append(piece);
    };
    // The inward normal of a rail at a heading (towards its track's centre).
    auto inward = [](int side, float heading) {
        return QPair<float, float>(-side * std::cos(heading), side * std::sin(heading));
    };
    // A check rail opposite a nose: the other rail of the nose's track,
    // inside the gauge by the flangeway, flared at both ends.
    auto checkRail = [&](int rail, float noseDistance) {
        const int path = plan.rails[rail].path;
        const int other = railOf(path, -plan.rails[rail].side);
        if(other < 0 || !laysPart(other, TrackProfileFilters::CheckRail))
            return;
        const Line &line = plan.rails[other].line;
        const float ratio = line.last().distance / std::max(1e-3f, plan.rails[rail].line.last().distance);
        const float centre = noseDistance * ratio;
        const float from = std::max(0.0f, centre - st.checkRailLength / 2);
        const float to = std::min(line.last().distance, centre + st.checkRailLength / 2);
        if(to - from < 1.0f)
            return;
        const int side = plan.rails[other].side;
        Line check = stretchOf(line, from, to);
        // Inward by the flangeway and a head; the ends 6 cm more over 0.6 m.
        constexpr float Flare = 0.06f, FlareLength = 0.6f;
        for(Point &p : check){
            const float d = std::min(p.distance, check.last().distance - p.distance);
            const float shift = w + f + (d < FlareLength ? Flare * (1 - d / FlareLength) : 0.0f);
            const auto n = inward(side, p.heading);
            p.x += n.first * shift;
            p.z += n.second * shift;
        }
        addPiece(RailPiece::Kind::CheckRail, path, side, check);
    };
    for(int ra = firstRail; ra < lastRail; ra++)
        for(int rb = ra + 1; rb < lastRail; rb++){
            const RailLine &A = plan.rails[ra];
            const RailLine &B = plan.rails[rb];
            if(A.path == B.path || !drawn.contains(ra) || !drawn.contains(rb))
                continue;
            for(int k = 0; k + 1 < A.line.size(); k++)
                for(int m = 0; m + 1 < B.line.size(); m++){
                    const Point &a0 = A.line[k], &a1 = A.line[k + 1];
                    const Point &b0 = B.line[m], &b1 = B.line[m + 1];
                    const float rx = a1.x - a0.x, rz = a1.z - a0.z;
                    const float sx = b1.x - b0.x, sz = b1.z - b0.z;
                    const float den = rx * sz - rz * sx;
                    if(std::abs(den) < 1e-9f)
                        continue;
                    const float qx = b0.x - a0.x, qz = b0.z - a0.z;
                    const float t = (qx * sz - qz * sx) / den;
                    const float u = (qx * rz - qz * rx) / den;
                    if(t < 0 || t >= 1 || u < 0 || u >= 1)
                        continue;
                    const float hA = a0.heading + t * (a1.heading - a0.heading);
                    const float hB = b0.heading + u * (b1.heading - b0.heading);
                    const float alpha = undirected(hA, hB);
                    if(alpha < ParallelRadians)
                        continue;
                    const float sA = a0.distance + t * (a1.distance - a0.distance);
                    const float sB = b0.distance + u * (b1.distance - b0.distance);
                    ComplexTrackPlan::Crossing crossing;
                    crossing.position[0] = a0.x + t * rx;
                    crossing.position[1] = a0.z + t * rz;
                    crossing.angle = alpha * 180.0f / Pi;
                    crossing.railA = ra;
                    crossing.railB = rb;
                    // The acute pair of half-lines (A+, B+) and the sector
                    // between them: a nose where both gauge faces look out of
                    // it, a throat where both look in, else obtuse.
                    const float dAx = std::sin(hA), dAz = std::cos(hA);
                    float dBx = std::sin(hB), dBz = std::cos(hB);
                    const int signB = dAx * dBx + dAz * dBz >= 0 ? 1 : -1;
                    dBx *= signB;
                    dBz *= signB;
                    const float mx = dAx + dBx, mz = dAz + dBz;
                    auto cross = [](float ax, float az, float bx, float bz) { return ax * bz - az * bx; };
                    const auto nA = inward(A.side, hA);
                    const auto nB = inward(B.side, hB);
                    const bool awayA = (cross(dAx, dAz, mx, mz) > 0) != (cross(dAx, dAz, nA.first, nA.second) > 0);
                    const bool awayB = (cross(dBx, dBz, mx, mz) > 0) != (cross(dBx, dBz, nB.first, nB.second) > 0);
                    if(awayA != awayB){
                        crossing.obtuse = true;
                        const float c = (w / 2 + f) / std::sin(alpha);
                        cuts[ra].append(Range(sA - c, sA + c));
                        cuts[rb].append(Range(sB - c, sB + c));
                        plan.crossings.append(crossing);
                        continue;
                    }
                    const int noseA = awayA ? 1 : -1;
                    const int noseB = awayA ? signB : -signB;
                    // The nose starts where its legs' heads touch; the rails
                    // from the throat stop at the knuckle, a flangeway and a
                    // head from the other leg, and go on as wing rails.
                    const float dn = w / (2 * std::sin(alpha / 2));
                    const float dk = (w + f) / std::sin(alpha);
                    cuts[ra].append(Range(std::min(sA - noseA * dk, sA + noseA * dn), std::max(sA - noseA * dk, sA + noseA * dn)));
                    cuts[rb].append(Range(std::min(sB - noseB * dk, sB + noseB * dn), std::max(sB - noseB * dk, sB + noseB * dn)));
                    auto wing = [&](const RailLine &from, float sFrom, int noseFrom, float hOther, int noseOther,
                            QPair<float, float> nOther) {
                        const Point k0 = TrackShapeAnalysis::at(from.line, sFrom - noseFrom * dk);
                        const float ox = noseOther * std::sin(hOther), oz = noseOther * std::cos(hOther);
                        constexpr float WingLength = 1.5f, Flare = 0.08f, FlareLength = 0.8f;
                        const float px = crossing.position[0] + nOther.first * (w + f);
                        const float pz = crossing.position[1] + nOther.second * (w + f);
                        const float ex = px + ox * (dn + WingLength), ez = pz + oz * (dn + WingLength);
                        addPiece(RailPiece::Kind::Wing, from.path, from.side, polyline({
                            {k0.x, k0.z}, {ex, ez},
                            {ex + ox * FlareLength + nOther.first * Flare, ez + oz * FlareLength + nOther.second * Flare}}));
                    };
                    wing(A, sA, noseA, hB, noseB, nB);
                    wing(B, sB, noseB, hA, noseA, nA);
                    checkRail(ra, sA + noseA * dn / 2);
                    checkRail(rb, sB + noseB * dn / 2);
                    plan.crossings.append(crossing);
                }
        }

    // The pieces: each drawn rail less its cuts; a blade near its toe beside
    // its stock rail, narrowed to its tip (closed) or opened.
    for(int r = firstRail; r < lastRail; r++){
        if(!drawn.contains(r))
            continue;
        RailLine &rail = plan.rails[r];
        rail.drawn = without(drawn[r], cuts.value(r), 0.05f);
        Line line = rail.line;
        QVector<float> scale;
        RailPiece::Kind kind = RailPiece::Kind::Plain;
        if(blades.contains(r)){
            kind = RailPiece::Kind::Blade;
            const Line &stock = plan.rails[blades[r].stock].line;
            float heel = line.last().distance;
            for(const Point &p : line)
                if(distanceTo(p, stock) >= w){
                    heel = p.distance;
                    break;
                }
            const float tip = 0.2f * w;
            scale.resize(line.size());
            for(int k = 0; k < line.size(); k++){
                Point &p = line[k];
                scale[k] = 1.0f;
                if(p.distance >= heel)
                    continue;
                const float sep = std::min(w, distanceTo(p, stock));
                const float width = tip + (w - tip) * sep / w;
                float shift = w / 2 + width / 2 - sep;
                if(!blades[r].closed)
                    shift += st.bladeOpening * (1 - p.distance / std::max(1e-3f, heel));
                const auto n = inward(rail.side, p.heading);
                p.x += n.first * shift;
                p.z += n.second * shift;
                scale[k] = width / w;
            }
        }
        for(const Range &range : rail.drawn){
            RailPiece piece;
            piece.kind = kind;
            piece.path = rail.path;
            piece.side = rail.side;
            piece.line = line;
            piece.scale = scale;
            piece.from = range.first;
            piece.to = range.second;
            plan.railPieces.append(piece);
        }
    }
}

}

ComplexTrackPlan::Line ComplexTrackPlan::offsetLine(const Line &line, float offset) {
    Line result;
    result.reserve(line.size());
    float distance = 0;
    for(int i = 0; i < line.size(); i++){
        Point p = line[i];
        p.x = line[i].x + offset * std::cos(line[i].heading);
        p.z = line[i].z - offset * std::sin(line[i].heading);
        if(i > 0)
            distance += std::hypot(p.x - result.last().x, p.z - result.last().z);
        p.distance = distance;
        result.append(p);
    }
    return result;
}

QVector<ComplexTrackPlan::Hit> ComplexTrackPlan::lineHits(float ox, float oz, float nx, float nz,
        const Line &line, float reach) {
    QVector<Hit> hits;
    for(int i = 0; i + 1 < line.size(); i++){
        const Point &a = line[i];
        const Point &b = line[i + 1];
        const double rx = b.x - a.x;
        const double rz = b.z - a.z;
        const double den = nx * rz - nz * rx;
        if(std::abs(den) < 1e-12)
            continue;
        const double qx = a.x - ox;
        const double qz = a.z - oz;
        const double t = (qx * rz - qz * rx) / den;
        const double u = (qx * nz - qz * nx) / den;
        // Half-open segments, the line's last point included.
        const bool lastSegment = i + 2 == line.size();
        if(u < 0 || (lastSegment ? u > 1 : u >= 1) || std::abs(t) > reach)
            continue;
        Hit hit;
        hit.t = float(t);
        hit.heading = float(a.heading + u * (b.heading - a.heading));
        hit.distance = float(a.distance + u * (b.distance - a.distance));
        hits.append(hit);
    }
    return hits;
}

bool ComplexTrackPlan::overlaps(const Tie &a, const Tie &b) {
    const std::array<Corner, 4> ra = corners(a);
    const std::array<Corner, 4> rb = corners(b);
    for(const std::array<Corner, 4> *r : {&ra, &rb})
        for(int i = 0; i < 4; i++){
            const float ax = (*r)[(i + 1) % 4].x - (*r)[i].x;
            const float az = (*r)[(i + 1) % 4].z - (*r)[i].z;
            const float nx = -az;
            const float nz = ax;
            float minA = std::numeric_limits<float>::max(), maxA = -minA;
            float minB = minA, maxB = -minA;
            for(const Corner &c : ra){
                const float d = nx * c.x + nz * c.z;
                minA = std::min(minA, d);
                maxA = std::max(maxA, d);
            }
            for(const Corner &c : rb){
                const float d = nx * c.x + nz * c.z;
                minB = std::min(minB, d);
                maxB = std::max(maxB, d);
            }
            if(maxA < minB - 1e-6f || maxB < minA - 1e-6f)
                return false;
        }
    return true;
}

ComplexTrackPlan ComplexTrackPlan::make(const QVector<Path> &paths, const Settings &settings, int flags) {
    ComplexTrackPlan plan;
    plan.settings = settings;
    plan.paths = paths;
    QVector<Line> lines;
    for(const Path &path : paths)
        lines.append(path.line);
    for(const Line &line : lines)
        if(line.size() < 2)
            return plan;
    const TrackShapeAnalysis analysis = TrackShapeAnalysis::analyseLines(lines, flags,
            settings.tieLength + settings.tieGap, settings.maxSharedAngle);
    plan.independent = analysis.independent;
    const float railCentre = settings.gauge / 2 + settings.railOffset;
    const float headWidth = settings.railOffset > 0 ? 2 * settings.railOffset : 0.07f;
    for(const TrackShapeAnalysis::Group &analysed : analysis.groups){
        Group group;
        group.paths = analysed.paths;
        group.reference = analysed.reference;
        planTies(plan, group);
        planBed(plan, group);

        // The rail lines in the paths' order.
        const int firstRail = plan.rails.size();
        for(int i : group.reference.order)
            for(int side : {-1, 1}){
                RailLine rail;
                rail.path = i;
                rail.side = side;
                rail.offset = side * railCentre;
                rail.line = offsetLine(paths[i].line, rail.offset);
                plan.rails.append(rail);
            }
        // Phase 2: switches, frogs, check rails; the pieces to sweep.
        planRails(plan, group, firstRail, headWidth);

        // Fasteners where a rail crosses a tie (one where two rails meet).
        for(int t = 0; t < group.ties.size(); t++){
            const Tie &tie = group.ties[t];
            const float ux = std::cos(tie.heading);
            const float uz = -std::sin(tie.heading);
            QVector<Fastener> onTie;
            for(int r = firstRail; r < plan.rails.size(); r++){
                const RailLine &rail = plan.rails[r];
                if(!lays(paths[rail.path], TrackProfileFilters::Fastener))
                    continue;
                for(const Hit &hit : lineHits(tie.centre[0], tie.centre[1], ux, uz, rail.line, tie.length / 2)){
                    Fastener fastener;
                    fastener.position[0] = tie.centre[0] + hit.t * ux;
                    fastener.position[1] = tie.centre[1] + hit.t * uz;
                    fastener.heading = hit.heading;
                    fastener.path = rail.path;
                    fastener.side = rail.side;
                    fastener.tie = t;
                    bool doubled = false;
                    for(const Fastener &other : onTie)
                        doubled = doubled || std::hypot(other.position[0] - fastener.position[0],
                                                        other.position[1] - fastener.position[1]) < headWidth;
                    if(!doubled)
                        onTie.append(fastener);
                }
            }
            group.fasteners += onTie;
        }
        if(group.reference.type == TrackShapeAnalysis::Reference::Type::None)
            plan.notes.append(QString("steep crossing %1 deg: path %2 ties through")
                              .arg(group.reference.crossingAngle, 0, 'f', 1).arg(group.reference.path));
        plan.groups.append(group);
    }
    return plan;
}

QVector<ComplexTrackPlan::RailLine> ComplexTrackPlan::conductorLines(float offset, unsigned part) const {
    QVector<RailLine> result;
    const float band = settings.gauge / 2 + settings.conductorClearance;
    for(const Group &group : groups){
        QVector<Line> earlier;
        for(int i : group.reference.order){
            if(!lays(paths[i], part))
                continue;
            RailLine rail;
            rail.path = i;
            rail.side = offset < 0 ? -1 : offset > 0 ? 1 : 0;
            rail.offset = offset;
            rail.line = offsetLine(paths[i].line, offset);
            QVector<bool> hidden(rail.line.size(), false);
            for(int k = 0; k < rail.line.size(); k++){
                const Point &p = rail.line[k];
                // Between another track's rails, or too close to them.
                for(int j : group.paths)
                    if(j != i && distanceTo(p, paths[j].line) < band)
                        hidden[k] = true;
                // On an earlier line (drawn once there), crossing it, or
                // running beside it closer than the clearance.
                for(const Line &other : earlier)
                    if(distanceTo(p, other) < settings.conductorClearance)
                        hidden[k] = true;
            }
            // Pieces shorter than 2 m left out (the shoe bridges gaps;
            // ramps and the least piece fine-tuned later).
            rail.drawn = runs(rail.line, hidden, 2.0f);
            earlier.append(rail.line);
            result.append(rail);
        }
    }
    return result;
}
