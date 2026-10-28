#include <tsre/sim/WorkPlanner.h>

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <set>
#include <unordered_map>
#include <tsre/sim/TrackNetwork.h>

namespace Sim {

namespace {

// Clearance beyond a junction the train reverses past; how far short of
// cars not coupled to (or a buffer) it stops; what a reversal weighs
// against metres driven in choosing a way.
constexpr double Margin = 15.0;
constexpr double StandOff = 5.0;
constexpr double ReversalCost = 300.0;
// A goal's plan is impractical (skipped) beyond so many metres more than
// the way to its place, or so many reversals (the user, 2026-10-21: work
// too complicated would be no fun for a player either).
constexpr double ImpracticalExtra = 10000.0;
constexpr int ImpracticalReversals = 12;
// Sorting may reverse more (the user, 2026-10-21: each siding visited off a
// ladder takes about two).
constexpr int ImpracticalSortReversals = 20;
// What skipping a stop, a waiting point or an event's place on the path
// weighs against metres driven in choosing where to come back to it.
constexpr double SkipFeature = 5000.0;
// Where cars are left off the paths of trains: among the nearest so many
// tracks within so many metres, for at most so many metres more than
// leaving them where they are.
constexpr size_t ParkingChoices = 6;
constexpr double ParkingReach = 3000.0;
constexpr double ParkingExtra = 3000.0;
// Sorting: at most so many moves; given up beyond so many metres or
// reversals (well past what is impractical); setting cars aside that are
// wanted in a siding weighs so many metres more than taking them there.
constexpr int SortMoves = 200;
constexpr double SortGiveUp = 2.0 * ImpracticalExtra;
constexpr int SortGiveUpReversals = 2 * ImpracticalSortReversals;
constexpr double AsideWanted = 2000.0;
// A drop-off's plan without sorting so long that sorting is tried too.
constexpr double SortTried = 5000.0;
constexpr double Eps = 1e-3;

double lengthOf(const std::vector<RouteInterval> &track) {
    double length = 0.0;
    for (const RouteInterval &interval : track)
        length += std::fabs(double(interval.to - interval.from));
    return length;
}

// Adds a piece of track at the end (joined to the last one where it goes on
// along the same node the same way).
void appendTo(std::vector<RouteInterval> &track, int node, double from, double to) {
    if (std::fabs(to - from) < Eps)
        return;
    if (!track.empty()) {
        RouteInterval &last = track.back();
        if (last.node == node && std::fabs(double(last.to) - from) < 0.01 && last.forward() == (to >= from)) {
            last.to = float(to);
            return;
        }
    }
    track.push_back(RouteInterval{node, float(from), float(to), 0.0});
}

void appendAll(std::vector<RouteInterval> &track, const std::vector<RouteInterval> &more) {
    for (const RouteInterval &interval : more)
        appendTo(track, interval.node, interval.from, interval.to);
}

std::vector<RouteInterval> backwardsOf(const std::vector<RouteInterval> &track) {
    std::vector<RouteInterval> out;
    for (auto it = track.rbegin(); it != track.rend(); ++it)
        out.push_back(RouteInterval{it->node, it->to, it->from, 0.0});
    return out;
}

// The last `length` metres of a track, and the first.
std::vector<RouteInterval> tailOf(const std::vector<RouteInterval> &track, double length) {
    std::vector<RouteInterval> out;
    double left = length;
    for (auto it = track.rbegin(); it != track.rend() && left > Eps; ++it) {
        const double here = std::fabs(double(it->to - it->from));
        if (here <= left + Eps) {
            out.push_back(*it);
            left -= here;
        } else {
            const float sign = it->forward() ? 1.0f : -1.0f;
            out.push_back(RouteInterval{it->node, it->to - sign * float(left), it->to, 0.0});
            left = 0.0;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<RouteInterval> headOf(const std::vector<RouteInterval> &track, double length) {
    return backwardsOf(tailOf(backwardsOf(track), length));
}

// For how many metres two tracks run together from their starts.
double together(const std::vector<RouteInterval> &a, const std::vector<RouteInterval> &b) {
    size_t i = 0, j = 0;
    double inA = 0.0, inB = 0.0, run = 0.0;
    while (i < a.size() && j < b.size()) {
        const RouteInterval &x = a[i], &y = b[j];
        if (x.node != y.node || x.forward() != y.forward())
            break;
        const double atX = x.from + (x.forward() ? inA : -inA), atY = y.from + (y.forward() ? inB : -inB);
        if (std::fabs(atX - atY) > 0.05)
            break;
        const double lengthX = std::fabs(double(x.to - x.from)), lengthY = std::fabs(double(y.to - y.from));
        const double step = std::min(lengthX - inA, lengthY - inB);
        run += step;
        inA += step;
        inB += step;
        if (inA >= lengthX - 1e-6) {
            ++i;
            inA = 0.0;
        }
        if (inB >= lengthY - 1e-6) {
            ++j;
            inB = 0.0;
        }
    }
    return run;
}

std::vector<RouteInterval> intervalsOf(const RouteTrack &track) {
    std::vector<RouteInterval> out;
    for (const RouteInterval &interval : track.intervals())
        appendTo(out, interval.node, interval.from, interval.to);
    return out;
}

std::vector<Vehicle> turned(const std::vector<Vehicle> &vehicles) {
    std::vector<Vehicle> out(vehicles.rbegin(), vehicles.rend());
    for (Vehicle &vehicle : out)
        vehicle.flip = !vehicle.flip;
    return out;
}

bool engine(const Vehicle &vehicle) {
    return vehicle.type() >= 4;
}

bool anyEngine(const std::vector<Vehicle> &vehicles, size_t from, size_t to) {
    for (size_t k = from; k < to && k < vehicles.size(); ++k)
        if (engine(vehicles[k]))
            return true;
    return false;
}

double carsLength(const std::vector<Vehicle> &vehicles, size_t from, size_t to) {
    double length = 0.0;
    for (size_t k = from; k < to && k < vehicles.size(); ++k)
        length += vehicles[k].length();
    return length;
}

QString carName(unsigned int car) {
    return QString("%1-%2").arg(car / 65536u).arg(car % 65536u);
}

// Where to come to: a cut's end to couple to (from outside it, the way
// given), a siding's stretch to stand in (the leading end in it, as deep in
// as the track allows), a place (the path's end, come to the way given),
// the path (anywhere on the rest of it, the way it goes). Which end must
// lead (-1 either); the cut coupled to (not in the way). A siding: only so
// deep in from its near end when `depth` is above 0.
struct Target {
    enum Kind { Couple, Siding, Point, Path };
    Kind kind = Point;
    int node = -1;
    double x = 0.0;
    bool forward = true;
    double from = 0.0;
    double to = 0.0;
    int lead = -1;
    int skip = -1;
    double depth = 0.0;
    // A move to it no longer than so many metres (0 any).
    double within = 0.0;
};

struct Move {
    bool found = false;
    std::vector<WorkLeg> legs;
    double metres = 0.0;
    // To the path: the part and route distance come to.
    int part = -1;
    double at = 0.0;
};

// The rest of the path to come back to: each a stretch of a part (node
// distances `from` to `to`, the part's route distance `start` at `from`).
struct PathPiece {
    int part;
    int node;
    bool forward;
    double from;
    double to;
    double start;
};

class Planner {
public:
    Planner(const SimulationInput &input, const WorkProgress *progress = nullptr)
        : in(input), net(*input.network), progress(progress) {}

    WorkPlan plan();

private:
    const SimulationInput &in;
    const TrackNetwork &net;
    const WorkProgress *progress;
    bool cancelled() const {
        return progress != nullptr && progress->cancel != nullptr && progress->cancel->load(std::memory_order_relaxed);
    }
    YardTrain train;
    std::vector<YardTrain> cuts;
    bool lastLeadB = true;
    // The track of the cars last coupled, from where they were met on.
    std::vector<RouteInterval> lastCoupled;
    // While sorting, moves no longer than so many metres (0 any).
    double searchWithin = 0.0;
    // The rest of the path, and its stops, waits and event places (part,
    // route distance) on it.
    std::vector<PathPiece> pathPieces;
    std::vector<std::pair<int, double>> features;
    double skipCost(int part, double at) const {
        int skipped = 0;
        for (const auto &[p, d] : features)
            skipped += p < part || (p == part && d < at - 0.5) ? 1 : 0;
        return SkipFeature * skipped;
    }
    // The path from route distance `departure` on its first part, clear of
    // cuts on it from where it comes to them on.
    void pathFrom(const TrainPlan &player, double departure);
    // The nodes the paths of the activity's trains take (the player's and
    // its AI trains'), and the wagon events' sidings: cars are not left
    // standing there. Those of its AI trains' paths alone.
    std::set<int> busy;
    std::set<int> aiBusy;
    static bool on(const std::set<int> &nodes, const std::vector<RouteInterval> &track) {
        for (const RouteInterval &interval : track)
            if (nodes.count(interval.node) > 0)
                return true;
        return false;
    }
    bool onBusy(const std::vector<RouteInterval> &track) const { return on(busy, track); }
    // Nodes off those paths with room for `need` metres of cars, the
    // nearest to the train first.
    std::vector<int> parkings(double need) const;
    // The `count` cars at end B (or A) left standing off the paths: where
    // they are when clear of them, else set out into the nearest track off
    // them they fit in (where they are when there is none, or when only the
    // player's path or an event's siding and that is much shorter: cars on
    // an AI train's path would stop it); `then` the move after it, which
    // the place must leave possible (false: it failed).
    bool leaveClear(WorkGoal &goal, size_t count, bool atB, const QString &what, const std::function<bool()> &then);

    double nodeLength(int node) const {
        const auto [first, count] = net.nodeSections(node);
        if (first < 0 || count <= 0 || size_t(first + count - 1) >= net.sections().size())
            return 0.0;
        return net.sections()[size_t(first + count - 1)].to;
    }

    struct Exit {
        int node;
        bool forward;
    };
    // The nodes a train goes on to past a node's end (its far end going
    // `forward`): either output of a facing junction, the input of a
    // trailing one; none at a track end.
    std::vector<Exit> exits(int node, bool forward) const {
        std::vector<Exit> out;
        const int end = forward ? 1 : 0;
        const int junction = net.endSection(node, end);
        if (junction < 0 || size_t(junction) >= net.sections().size()
                || net.sections()[size_t(junction)].kind != TrackSection::Junction)
            return out;
        const TrackSection &j = net.sections()[size_t(junction)];
        const int in = net.endPin(node, end);
        const int outs[2] = {in == 0 ? 1 : 0, in == 0 ? 2 : -1};
        for (int pin : outs) {
            if (pin < 0 || pin > 2)
                continue;
            const int track = j.link[pin];
            if (track < 0 || size_t(track) >= net.sections().size())
                continue;
            const TrackSection &next = net.sections()[size_t(track)];
            const int nextNode = next.node;
            const auto [first, count] = net.nodeSections(nextNode);
            out.push_back(Exit{nextNode, next.link[0] == junction && track == first});
        }
        return out;
    }

    // Going along `node` from `from` the `forward` way: how far (the place on
    // the node) before the first cut in the way (`skip` not counted), its
    // index in `hit` (-1: the node's end reached).
    double reach(int node, bool forward, double from, int skip, int *hit) const {
        double limit = forward ? nodeLength(node) : 0.0;
        if (hit != nullptr)
            *hit = -1;
        // (The cut coupled to too: the move stops at its near end, never
        // through it to its far one.)
        (void)skip;
        for (size_t c = 0; c < cuts.size(); ++c) {
            for (const RouteInterval &interval : cuts[c].track) {
                if (interval.node != node)
                    continue;
                const double lo = std::min(interval.from, interval.to), hi = std::max(interval.from, interval.to);
                if (forward && hi > from + Eps && lo < limit) {
                    limit = std::max(from, lo);
                    if (hit != nullptr)
                        *hit = int(c);
                } else if (!forward && lo < from - Eps && hi > limit) {
                    limit = std::min(from, hi);
                    if (hit != nullptr)
                        *hit = int(c);
                }
            }
        }
        return limit;
    }

    // A run of `need` metres ahead from a place, along any way through the
    // junctions, clear of cuts: the track of it.
    bool freeRun(int node, bool forward, double from, double need, std::vector<RouteInterval> &run, int depth) const {
        if (depth > 16)
            return false;
        int hit = -1;
        const double limit = reach(node, forward, from, -1, &hit);
        const double room = std::fabs(limit - from);
        if (room >= need - Eps) {
            appendTo(run, node, from, from + (forward ? need : -need));
            return true;
        }
        if (hit >= 0)
            return false;
        const std::vector<RouteInterval> before = run;
        appendTo(run, node, from, limit);
        for (const Exit &exit : exits(node, forward)) {
            if (freeRun(exit.node, exit.forward, exit.forward ? 0.0 : nodeLength(exit.node), need - room, run, depth + 1))
                return true;
            run = before;
            appendTo(run, node, from, limit);
        }
        run = before;
        return false;
    }

    Move route(const Target &target) const;
    // A place to couple at can be come to: the track behind it (the way it
    // is come to) leads in from a junction clear of cars, or the train
    // stands on it (not so a cut's end against a buffer: a search would
    // cover all the track within its reach before failing).
    bool approachable(const Target &target) const {
        if (target.kind != Target::Couple)
            return true;
        for (const RouteInterval &interval : train.track)
            if (interval.node == target.node)
                return true;
        const double end = target.forward ? 0.0 : nodeLength(target.node);
        int hit = -1;
        const double limit = reach(target.node, target.forward, end, -1, &hit);
        const bool clear = target.forward ? limit >= target.x - Eps : limit <= target.x + Eps;
        return clear && !exits(target.node, !target.forward).empty();
    }
    // The train after a move, a coupling (to the cut `cut` at its start or
    // end, by its leading end) and a cut (`count` cars at end B or A left
    // standing).
    void moveBy(Move &move);
    void couple(int cut, bool atStart, bool leadB);
    int cutOff(size_t count, bool atB);

    bool inSiding(const YardTrain &cut, const EventPlan &event) const;
    // The yard as it is, to try a way and go back.
    struct Snapshot {
        YardTrain train;
        std::vector<YardTrain> cuts;
        bool lastLeadB;
        std::vector<int> satisfied;
    };
    Snapshot save() const { return Snapshot{train, cuts, lastLeadB, satisfied}; }
    void restore(const Snapshot &snapshot) {
        train = snapshot.train;
        cuts = snapshot.cuts;
        lastLeadB = snapshot.lastLeadB;
        satisfied = snapshot.satisfied;
    }
    // Pickups active while a goal is planned, and those whose cars were all
    // in the train at once on its moves (they fire then: a drop-off's
    // sorting fetches them).
    std::vector<int> watching;
    std::vector<int> satisfied;
    // A move kept in a goal (its metres and reversals counted) and made; a
    // move found to a target and kept.
    void keep(WorkGoal &goal, const Move &move, WorkStep::Action action, const QString &what, WorkStep &made);
    bool step(WorkGoal &goal, const Target &target, WorkStep::Action action, QString what, WorkStep &made);
    Target sidingOf(const EventPlan &event, int lead, double depth = 0.0) const;
    Target endOf(int cut, bool atStart, int lead) const;
    // What goals to come want of a pickup: an assemble's order, the
    // drop-offs' cars at an end of the train that can lead into their
    // sidings. How far the yard as it is falls short of that.
    struct Ahead {
        const std::vector<unsigned int> *order = nullptr;
        std::vector<const EventPlan *> drops;
    };
    double judge(const Ahead &ahead) const;
    bool pickUp(WorkGoal &goal, const std::vector<unsigned int> &cars, const Ahead &ahead);
    bool setOut(WorkGoal &goal, const EventPlan &event, const Ahead &ahead);
    // The listed cars, together in the train, as a cut into the event's
    // siding: from an end of the train, or from its middle (the rest left
    // short of the siding and coupled again).
    bool setOutBlock(WorkGoal &goal, const EventPlan &event, const std::vector<unsigned int> &cars);
    // The `count` cars at end B (or A) into the siding and cut off there:
    // that end first, or (`runRound`) after running the rest round them
    // (the cars left standing, the rest taken to their other end and
    // coupled, so that they lead the other way); the shorter.
    bool setOutEnd(WorkGoal &goal, const EventPlan &event, size_t count, bool atB, bool runRound, double depth = 0.0);
    bool assemble(WorkGoal &goal, const EventPlan &event, const Ahead &ahead);
    // Places to leave part of the train on its way to a target (with either
    // end leading, or the one given): short of the node ends on the way's
    // last legs, at its reversals, and where it stands (an empty move).
    std::vector<Move> stagesTowards(const Target &target, int lead) const;
    // The cars of a cut put into the train where the listed order wants
    // them, between two of its parts: the train cut there, the part with an
    // engine to the cars and back.
    bool insert(WorkGoal &goal, const std::vector<unsigned int> &cars);
    // Sorting (the user, 2026-10-21): a drop-off's cars gathered into one
    // cut in its siding, in any order, from wherever they stand. Each time
    // the cheapest of: a block at an end of the train (cars going to the
    // same place, up to an engine) taken where its cars are wanted (this
    // siding or a drop-off's to come, coupled to the cars gathered there
    // and cut off with them) or set aside on a track no path takes; with
    // none of the cars in the train, a cut with some fetched (the cars
    // beyond the last of them left where they stand).
    struct Block {
        size_t count = 0;
        const EventPlan *to = nullptr;
    };
    Block endBlock(bool atB, const std::vector<const EventPlan *> &drops) const;
    // The cut in an event's siding of its cars only (the largest), -1; the
    // length of the listed cars, wherever they are.
    int gathered(const EventPlan &event) const;
    double carsLengthOf(const std::vector<unsigned int> &cars) const;
    bool deliver(WorkGoal &goal, const EventPlan &to, size_t count, bool atB);
    bool putAside(WorkGoal &goal, size_t count, bool atB, const EventPlan &event);
    bool fetch(WorkGoal &goal, int cut, bool atStart, int lead, const EventPlan &event);
    // The event's siding (or the cars gathered there), every other cut
    // with its cars, the sidings of the wagon events to come and where the
    // train started reached from where the train stands.
    bool reachesRest(const EventPlan &event) const;
    std::vector<const EventPlan *> pendingSidings;
    Target started;
    bool sort(WorkGoal &goal, const EventPlan &event, const Ahead &ahead);
    void finish(WorkGoal &goal, WorkStep &&made);
    int trainIndex(unsigned int car) const;
    int cutOf(unsigned int car) const;
    WorkGoal goalFor(int event, const Ahead &ahead);
    WorkGoal planGoal(int event, const Ahead &ahead);
    // The way from where the train stands to an event's place (its cars'
    // cut nearest, or its siding), metres; 0 none.
    double wayTo(const EventPlan &event) const;
};

Move Planner::route(const Target &target) const {
    // Dijkstra over (node, way, leading end, entered or just reversed):
    // a state's front at `start` on its node going its way, the piece of
    // track the front took to get there from the state before, whether the
    // train reversed after it.
    struct State {
        int node;
        bool forward;
        int lead;
        double start;
        int kind; // 0 where it stands, 1 entered the node, 2 reversed on it
        double cost;
        double metres;
        int prev;
        std::vector<RouteInterval> piece;
        bool reversal;
    };
    std::vector<State> states;
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    // The least cost found for each state (node, way, leading end, kind).
    std::unordered_map<std::uint64_t, double> best;
    const auto keyOf = [](int node, bool forward, int lead, int kind) {
        return (std::uint64_t(std::uint32_t(node)) << 3) | (forward ? 4u : 0u) | (lead == 1 ? 2u : 0u) | (kind == 2 ? 1u : 0u);
    };
    const double length = train.length();
    // No move longer than so many metres (the planner's bound while it
    // sorts: past it the sorting gives up anyway).
    const double within = target.within > 0.0 ? target.within : searchWithin;
    const auto push = [&](State state) {
        if (within > 0.0 && state.cost > within)
            return;
        if (state.kind != 0) {
            const auto key = keyOf(state.node, state.forward, state.lead, state.kind);
            auto found = best.find(key);
            if (found != best.end() && found->second <= state.cost + Eps)
                return;
            best[key] = state.cost;
        }
        states.push_back(std::move(state));
        queue.push({states.back().cost, int(states.size()) - 1});
    };
    // Where it stands: end B leading on (its track's way there), or end A
    // (a reversal unless end A led last).
    if (train.track.empty() || !approachable(target))
        return {};
    const RouteInterval &b = train.track.back();
    const RouteInterval &a = train.track.front();
    push(State{b.node, b.forward(), 1, b.to, 0, lastLeadB ? 0.0 : ReversalCost, 0.0, -1, {}, false});
    push(State{a.node, !a.forward(), 0, a.from, 0, lastLeadB ? ReversalCost : 0.0, 0.0, -1, {}, false});
    int found = -1;
    double foundCost = 1e18;
    int foundPart = -1;
    double foundAt = 0.0;
    std::vector<RouteInterval> lastPiece;
    double lastMetres = 0.0;
    int expanded = 0;
    while (!queue.empty() && expanded < 200000) {
        const auto [cost, index] = queue.top();
        queue.pop();
        if (cost >= foundCost || cancelled())
            break;
        // (A state reached more cheaply since it was queued: done then.)
        const State &queued = states[size_t(index)];
        if (queued.kind != 0) {
            const auto known = best.find(keyOf(queued.node, queued.forward, queued.lead, queued.kind));
            if (known != best.end() && known->second < cost - Eps)
                continue;
        }
        ++expanded;
        // (Its fields, not its piece: pushes below may move the states.)
        struct Head {
            int node;
            bool forward;
            int lead;
            double start;
            int kind;
            double metres;
        };
        const Head s{queued.node, queued.forward, queued.lead, queued.start, queued.kind, queued.metres};
        int hit = -1;
        const double limit = reach(s.node, s.forward, s.start, target.skip, &hit);
        const bool leadOk = target.lead < 0 || target.lead == s.lead;
        // The path: where this run comes to it first, the way it goes.
        if (leadOk && target.kind == Target::Path) {
            for (const PathPiece &piece : pathPieces) {
                if (piece.node != s.node || piece.forward != s.forward)
                    continue;
                const double x = s.forward ? std::max(s.start, piece.from) : std::min(s.start, piece.from);
                if (s.forward ? x > std::min(limit, piece.to) + Eps : x < std::max(limit, piece.to) - Eps)
                    continue;
                const double at = piece.start + std::fabs(x - piece.from);
                const double total = cost + std::fabs(x - s.start) + skipCost(piece.part, at);
                if (total < foundCost) {
                    foundCost = total;
                    found = index;
                    lastPiece.clear();
                    appendTo(lastPiece, s.node, s.start, x);
                    lastMetres = s.metres + std::fabs(x - s.start);
                    foundPart = piece.part;
                    foundAt = at;
                }
            }
        }
        // The target on this node, the way it goes.
        if (leadOk && target.kind != Target::Path && s.node == target.node) {
            double x = -1.0;
            bool ok = false;
            if (target.kind != Target::Siding && s.forward == target.forward) {
                x = target.x;
                ok = s.forward ? x >= s.start - Eps && x <= limit + Eps : x <= s.start + Eps && x >= limit - Eps;
            } else if (target.kind == Target::Siding) {
                // As deep in as it goes: short of cars there, or a buffer.
                const bool buffer = hit < 0 && exits(s.node, s.forward).empty();
                const double stop = hit >= 0 || buffer ? limit + (s.forward ? -StandOff : StandOff) : limit;
                x = s.forward ? std::min(target.to, stop) : std::max(target.from, stop);
                if (target.depth > 0.0)
                    x = s.forward ? std::min(x, target.from + target.depth) : std::max(x, target.to - target.depth);
                ok = s.forward ? x >= target.from + 1.0 && x >= s.start - Eps : x <= target.to - 1.0 && x <= s.start + Eps;
            }
            if (ok && cost + std::fabs(x - s.start) < foundCost) {
                foundCost = cost + std::fabs(x - s.start);
                found = index;
                lastPiece.clear();
                appendTo(lastPiece, s.node, s.start, x);
                lastMetres = s.metres + std::fabs(x - s.start);
            }
        }
        // On past the node's end.
        if (hit < 0) {
            const double end = s.forward ? nodeLength(s.node) : 0.0;
            const double run = std::fabs(end - s.start);
            for (const Exit &exit : exits(s.node, s.forward)) {
                State next{exit.node, exit.forward, s.lead, exit.forward ? 0.0 : nodeLength(exit.node), 1,
                           cost + run, s.metres + run, index, {}, false};
                appendTo(next.piece, s.node, s.start, end);
                push(std::move(next));
            }
        }
        // Reversing where it has come in: on until the whole train is past
        // the junction behind it, back from there.
        if (s.kind == 1 && nodeLength(s.node) > Margin) {
            std::vector<RouteInterval> run;
            if (freeRun(s.node, s.forward, s.start, length + Margin, run, 0)) {
                State next{s.node, !s.forward, 1 - s.lead, s.start + (s.forward ? Margin : -Margin), 2,
                           cost + length + Margin + ReversalCost, s.metres + length + Margin, index, run, true};
                push(std::move(next));
            }
        }
    }
    Move move;
    if (found < 0)
        return move;
    // The chain back to where it stood: its pieces, a new leg after each
    // reversal.
    std::vector<int> chain;
    for (int k = found; k >= 0; k = states[size_t(k)].prev)
        chain.push_back(k);
    std::reverse(chain.begin(), chain.end());
    WorkLeg leg;
    leg.leadB = states[size_t(chain.front())].lead == 1;
    for (size_t c = 1; c < chain.size(); ++c) {
        const State &state = states[size_t(chain[c])];
        appendAll(leg.way, state.piece);
        if (state.reversal) {
            move.legs.push_back(leg);
            leg = WorkLeg();
            leg.leadB = state.lead == 1;
        }
    }
    appendAll(leg.way, lastPiece);
    move.legs.push_back(leg);
    move.metres = lastMetres;
    move.found = true;
    move.part = foundPart;
    move.at = foundAt;
    return move;
}

void Planner::pathFrom(const TrainPlan &player, double departure) {
    pathPieces.clear();
    features.clear();
    for (size_t p = 0; p < player.route.parts.size(); ++p) {
        for (const RouteInterval &interval : player.route.parts[p].intervals()) {
            const double length = interval.length();
            const double skip = p == 0 ? departure - interval.start : 0.0;
            if (skip >= length - Eps)
                continue;
            const double sign = interval.forward() ? 1.0 : -1.0;
            const double cut = std::max(0.0, skip);
            pathPieces.push_back(PathPiece{int(p), interval.node, interval.forward(), interval.from + sign * cut,
                                           interval.to, interval.start + cut});
        }
    }
    // Not where the rest of the path would run into cars standing.
    size_t clearFrom = 0;
    double clearAt = -1.0;
    for (size_t k = 0; k < pathPieces.size(); ++k) {
        const PathPiece &piece = pathPieces[k];
        const double lo = std::min(piece.from, piece.to), hi = std::max(piece.from, piece.to);
        for (const YardTrain &cut : cuts)
            for (const RouteInterval &interval : cut.track) {
                const double a = std::min(interval.from, interval.to), b = std::max(interval.from, interval.to);
                if (interval.node != piece.node || std::min(hi, b) - std::max(lo, a) <= 0.5)
                    continue;
                const double past = piece.forward ? std::min(hi, b) : std::max(lo, a);
                if (k > clearFrom || clearAt < 0.0 || std::fabs(past - piece.from) > std::fabs(clearAt - piece.from)) {
                    clearFrom = k;
                    clearAt = past;
                }
            }
    }
    if (clearAt >= 0.0) {
        PathPiece &piece = pathPieces[clearFrom];
        piece.start += std::fabs(clearAt - piece.from);
        piece.from = clearAt;
        pathPieces.erase(pathPieces.begin(), pathPieces.begin() + long(clearFrom));
    }
    const double from = pathPieces.empty() ? 0.0 : pathPieces.front().start;
    const int fromPart = pathPieces.empty() ? int(player.route.parts.size()) : pathPieces.front().part;
    const auto add = [&](int part, double at) {
        if (part > fromPart || (part == fromPart && at >= from - 0.5))
            features.push_back({part, at});
    };
    for (const StopPlan &stop : player.stops)
        add(stop.part, stop.front);
    for (const WaitPlan &wait : player.waits)
        add(wait.part, wait.front);
    for (const EventPlan &event : in.events) {
        if (event.kind != EventPlan::Location || event.trigger >= 0)
            continue;
        for (const TrackPlace &place : event.places)
            for (size_t p = 0; p < player.route.parts.size(); ++p) {
                double at = -1.0;
                if (player.route.parts[p].find(place.node, place.distance, at))
                    add(int(p), at);
            }
    }
}

void Planner::moveBy(Move &move) {
    const double length = train.length();
    for (WorkLeg &leg : move.legs) {
        leg.body = leg.leadB ? train.track : backwardsOf(train.track);
        if (leg.leadB) {
            std::vector<RouteInterval> track = train.track;
            appendAll(track, leg.way);
            train.track = tailOf(track, length);
        } else {
            std::vector<RouteInterval> track = backwardsOf(train.track);
            appendAll(track, leg.way);
            train.track = backwardsOf(tailOf(track, length));
        }
        lastLeadB = leg.leadB;
    }
}

void Planner::couple(int cut, bool atStart, bool leadB) {
    YardTrain &other = cuts[size_t(cut)];
    lastCoupled = atStart ? other.track : backwardsOf(other.track);
    if (leadB) {
        const std::vector<Vehicle> cars = atStart ? other.vehicles : turned(other.vehicles);
        train.vehicles.insert(train.vehicles.end(), cars.begin(), cars.end());
        appendAll(train.track, atStart ? other.track : backwardsOf(other.track));
    } else {
        std::vector<Vehicle> cars = atStart ? turned(other.vehicles) : other.vehicles;
        cars.insert(cars.end(), train.vehicles.begin(), train.vehicles.end());
        train.vehicles = cars;
        std::vector<RouteInterval> track = atStart ? backwardsOf(other.track) : other.track;
        appendAll(track, train.track);
        train.track = track;
    }
    cuts.erase(cuts.begin() + cut);
}

int Planner::cutOff(size_t count, bool atB) {
    const size_t n = train.vehicles.size();
    count = std::min(count, n);
    YardTrain made;
    made.name = train.name;
    const double total = train.length();
    if (atB) {
        const double block = carsLength(train.vehicles, n - count, n);
        made.vehicles.assign(train.vehicles.begin() + long(n - count), train.vehicles.end());
        made.track = tailOf(train.track, block);
        train.vehicles.resize(n - count);
        train.track = headOf(train.track, total - block);
    } else {
        const double block = carsLength(train.vehicles, 0, count);
        made.vehicles.assign(train.vehicles.begin(), train.vehicles.begin() + long(count));
        made.track = headOf(train.track, block);
        train.vehicles.erase(train.vehicles.begin(), train.vehicles.begin() + long(count));
        train.track = tailOf(train.track, total - block);
    }
    cuts.push_back(made);
    return int(cuts.size()) - 1;
}

bool Planner::inSiding(const YardTrain &cut, const EventPlan &event) const {
    if (!event.siding || cut.track.empty())
        return true;
    const auto in = [&](int node, float at) {
        return node == event.sidingNode && at >= event.sidingFrom - 1.0f && at <= event.sidingTo + 1.0f;
    };
    return in(cut.track.front().node, cut.track.front().from) || in(cut.track.back().node, cut.track.back().to);
}

int Planner::trainIndex(unsigned int car) const {
    for (size_t k = 0; k < train.vehicles.size(); ++k)
        if (train.vehicles[k].id == car)
            return int(k);
    return -1;
}

int Planner::cutOf(unsigned int car) const {
    for (size_t c = 0; c < cuts.size(); ++c)
        for (const Vehicle &vehicle : cuts[c].vehicles)
            if (vehicle.id == car)
                return int(c);
    return -1;
}

void Planner::keep(WorkGoal &goal, const Move &move, WorkStep::Action action, const QString &what, WorkStep &made) {
    made.action = action;
    made.what = what;
    goal.metres += move.metres;
    bool lead = lastLeadB;
    for (const WorkLeg &leg : move.legs) {
        if (leg.leadB != lead)
            ++goal.reversals;
        lead = leg.leadB;
    }
    Move kept = move;
    moveBy(kept);
    made.legs = kept.legs;
}

bool Planner::step(WorkGoal &goal, const Target &target, WorkStep::Action action, QString what, WorkStep &made) {
    const Move move = route(target);
    if (!move.found)
        return false;
    keep(goal, move, action, what, made);
    return true;
}

Target Planner::sidingOf(const EventPlan &event, int lead, double depth) const {
    Target target;
    target.kind = Target::Siding;
    target.depth = depth;
    target.node = event.sidingNode;
    target.from = std::min(event.sidingFrom, event.sidingTo);
    target.to = std::max(event.sidingFrom, event.sidingTo);
    target.lead = lead;
    return target;
}

Target Planner::endOf(int cut, bool atStart, int lead) const {
    const YardTrain &other = cuts[size_t(cut)];
    Target target;
    target.kind = Target::Couple;
    target.skip = cut;
    target.lead = lead;
    if (atStart) {
        target.node = other.track.front().node;
        target.x = other.track.front().from;
        target.forward = other.track.front().forward();
    } else {
        target.node = other.track.back().node;
        target.x = other.track.back().to;
        target.forward = !other.track.back().forward();
    }
    return target;
}

namespace {

bool listed(const std::vector<unsigned int> &cars, unsigned int car) {
    return std::find(cars.begin(), cars.end(), car) != cars.end();
}

// The listed cars among vehicles: their first and last place, how many, and
// whether they are together.
bool blockOf(const std::vector<Vehicle> &vehicles, const std::vector<unsigned int> &cars, size_t &first, size_t &last,
             size_t &count) {
    first = vehicles.size();
    last = 0;
    count = 0;
    for (size_t k = 0; k < vehicles.size(); ++k)
        if (listed(cars, vehicles[k].id)) {
            first = std::min(first, k);
            last = k;
            ++count;
        }
    return count > 0 && last - first + 1 == count;
}

// Exactly the listed cars, in their order or the other way.
bool exactly(const std::vector<Vehicle> &vehicles, const std::vector<unsigned int> &cars) {
    if (vehicles.size() != cars.size())
        return false;
    bool forward = true, backward = true;
    for (size_t k = 0; k < cars.size(); ++k) {
        forward = forward && vehicles[k].id == cars[k];
        backward = backward && vehicles[vehicles.size() - 1 - k].id == cars[k];
    }
    return forward || backward;
}

}

double Planner::judge(const Ahead &ahead) const {
    double penalty = 0.0;
    const size_t n = train.vehicles.size();
    if (ahead.order != nullptr) {
        // The listed cars in the train so far: together, a run of the list in
        // its order or the other way.
        size_t first = 0, last = 0, count = 0;
        if (blockOf(train.vehicles, *ahead.order, first, last, count)) {
            std::vector<unsigned int> got;
            for (size_t k = first; k <= last; ++k)
                got.push_back(train.vehicles[k].id);
            const std::vector<unsigned int> &order = *ahead.order;
            std::vector<unsigned int> back(order.rbegin(), order.rend());
            const auto runOf = [&got](const std::vector<unsigned int> &list) {
                return std::search(list.begin(), list.end(), got.begin(), got.end()) != list.end();
            };
            if (!runOf(order) && !runOf(back))
                penalty += 1e6;
        } else if (count > 0) {
            penalty += 1e6;
        }
    }
    for (const EventPlan *drop : ahead.drops) {
        // Its cars in the train: together at an end that leads into its
        // siding (the whole train any way).
        size_t first = 0, last = 0, count = 0;
        std::vector<unsigned int> mine;
        for (unsigned int car : drop->cars)
            if (trainIndex(car) >= 0)
                mine.push_back(car);
        if (mine.empty())
            continue;
        if (!blockOf(train.vehicles, mine, first, last, count)) {
            penalty += 1e5;
            continue;
        }
        const bool atA = first == 0, atB = last + 1 == n;
        bool ok = false;
        if (atA && atB)
            ok = route(sidingOf(*drop, -1)).found;
        if (!ok && atB && !(atA && atB))
            ok = route(sidingOf(*drop, 1)).found;
        if (!ok && atA && !(atA && atB))
            ok = route(sidingOf(*drop, 0)).found;
        if (!ok)
            penalty += 1e5;
    }
    return penalty;
}

void Planner::finish(WorkGoal &goal, WorkStep &&made) {
    if (made.action == WorkStep::Couple) {
        ++goal.couplings;
        made.over = lastCoupled;
    }
    if (made.action == WorkStep::Cut)
        ++goal.cuts;
    goal.steps.push_back(std::move(made));
    for (int e : watching) {
        if (e == goal.event || std::find(satisfied.begin(), satisfied.end(), e) != satisfied.end())
            continue;
        bool all = true;
        for (unsigned int car : in.events[size_t(e)].cars)
            all = all && trainIndex(car) >= 0;
        if (all)
            satisfied.push_back(e);
    }
}

bool Planner::pickUp(WorkGoal &goal, const std::vector<unsigned int> &cars, const Ahead &ahead) {
    for (int guard = 0; guard < 32; ++guard) {
        unsigned int missing = NoCar;
        for (unsigned int car : cars)
            if (trainIndex(car) < 0) {
                missing = car;
                break;
            }
        if (missing == NoCar)
            return true;
        const int c = cutOf(missing);
        if (c < 0) {
            goal.why = //% "car %1 is in no train or cut"
                    qtTrId("work.why.no.car").arg(carName(missing));
            return false;
        }
        // The listed cars of the cut: a block at its end coupled to, the
        // others cut off again; else the whole cut taken along (PickUp asks
        // only that they are in the train).
        const QString name = cuts[size_t(c)].name;
        const size_t n = cuts[size_t(c)].vehicles.size();
        size_t first = 0, last = 0, count = 0;
        const bool together = blockOf(cuts[size_t(c)].vehicles, cars, first, last, count);
        const bool all = together && count == n;
        struct Option {
            bool atStart;
            int lead;
            bool trim;
        };
        Option best{true, 1, false};
        Move bestMove;
        double bestScore = 1e18;
        const Snapshot before = save();
        for (bool atStart : {true, false})
            for (int lead : {1, 0}) {
                const bool trim = together && !all && (atStart ? first == 0 : last + 1 == n);
                const Move move = route(endOf(c, atStart, lead));
                if (!move.found)
                    continue;
                // As the yard would be: moved, coupled, trimmed.
                WorkGoal trial = goal;
                WorkStep made;
                keep(trial, move, WorkStep::Couple, QString(), made);
                couple(c, atStart, lastLeadB);
                if (trim)
                    cutOff(n - count, lastLeadB);
                const double score = trial.metres - goal.metres + ReversalCost * (trial.reversals - goal.reversals)
                        + judge(ahead) + (trim || all ? 0.0 : 1000.0);
                restore(before);
                if (score < bestScore) {
                    bestScore = score;
                    best = Option{atStart, lead, trim};
                    bestMove = move;
                }
            }
        if (!bestMove.found) {
            goal.why = //% "no way to %1"
                    qtTrId("work.why.no.way.to").arg(name);
            return false;
        }
        WorkStep made;
        made.cars = int(n);
        keep(goal, bestMove, WorkStep::Couple, QString("couple to %1 (%2 cars)").arg(name).arg(n), made);
        couple(c, best.atStart, lastLeadB);
        finish(goal, std::move(made));
        if (best.trim) {
            // The cars not asked for, now at the leading end: left standing.
            WorkStep cut;
            cut.action = WorkStep::Cut;
            cut.cars = int(n - count);
            cut.atB = lastLeadB;
            cut.what = QString("leave %1 cars not asked for").arg(n - count);
            cutOff(n - count, lastLeadB);
            finish(goal, std::move(cut));
        }
    }
    //% "too many cuts to take"
    goal.why = qtTrId("work.why.too.many");
    return false;
}

bool Planner::setOut(WorkGoal &goal, const EventPlan &event, const Ahead &ahead) {
    if (!event.siding || event.sidingNode < 0) {
        //% "the event has no siding"
        goal.why = qtTrId("work.why.no.siding");
        return false;
    }
    // Already standing so in the siding.
    for (const YardTrain &cut : cuts) {
        if (cut.vehicles.size() != event.cars.size())
            continue;
        bool same = true;
        for (const Vehicle &vehicle : cut.vehicles)
            same = same && listed(event.cars, vehicle.id);
        if (same && inSiding(cut, event))
            return true;
    }
    // Cars not in the train picked up first, so that they can lead into
    // the siding.
    const Snapshot before = save();
    const WorkGoal savedGoal = goal;
    Ahead mine = ahead;
    mine.drops.insert(mine.drops.begin(), &event);
    const bool simple = pickUp(goal, event.cars, mine) && setOutBlock(goal, event, event.cars);
    if (simple && goal.metres - savedGoal.metres <= SortTried)
        return true;
    // Else sorted, from the yard as it was (the shorter kept).
    const QString why = goal.why;
    const WorkGoal simpleGoal = goal;
    const Snapshot simpleYard = save();
    restore(before);
    goal = savedGoal;
    if (sort(goal, event, ahead)) {
        if (!simple || goal.metres + ReversalCost * goal.reversals
                        <= simpleGoal.metres + ReversalCost * simpleGoal.reversals) {
            goal.sorted = true;
            return true;
        }
    } else if (!simple) {
        if (goal.why.isEmpty())
            goal.why = why;
        return false;
    }
    goal = simpleGoal;
    restore(simpleYard);
    return true;
}

Planner::Block Planner::endBlock(bool atB, const std::vector<const EventPlan *> &drops) const {
    Block block;
    const std::vector<Vehicle> &vehicles = train.vehicles;
    const size_t n = vehicles.size();
    const auto at = [&](size_t k) -> const Vehicle & { return atB ? vehicles[n - 1 - k] : vehicles[k]; };
    const auto destination = [&](unsigned int car) -> const EventPlan * {
        for (const EventPlan *drop : drops)
            if (listed(drop->cars, car))
                return drop;
        return nullptr;
    };
    if (n == 0 || engine(at(0)))
        return block;
    block.to = destination(at(0).id);
    while (block.count < n && !engine(at(block.count)) && destination(at(block.count).id) == block.to)
        ++block.count;
    return block;
}

double Planner::carsLengthOf(const std::vector<unsigned int> &cars) const {
    double length = 0.0;
    const auto add = [&](const std::vector<Vehicle> &vehicles) {
        for (const Vehicle &vehicle : vehicles)
            length += listed(cars, vehicle.id) ? vehicle.length() : 0.0;
    };
    add(train.vehicles);
    for (const YardTrain &cut : cuts)
        add(cut.vehicles);
    return length;
}

int Planner::gathered(const EventPlan &event) const {
    int best = -1;
    for (size_t c = 0; c < cuts.size(); ++c) {
        bool only = !cuts[c].vehicles.empty();
        for (const Vehicle &vehicle : cuts[c].vehicles)
            only = only && listed(event.cars, vehicle.id);
        if (only && inSiding(cuts[c], event) && (best < 0 || cuts[c].vehicles.size() > cuts[size_t(best)].vehicles.size()))
            best = int(c);
    }
    return best;
}

bool Planner::deliver(WorkGoal &goal, const EventPlan &to, size_t count, bool atB) {
    const int there = gathered(to);
    // In only as far as all its cars will need (a long yard track).
    if (there < 0)
        return setOutEnd(goal, to, count, atB, false, carsLengthOf(to.cars) + Margin);
    // Coupled to the cars gathered there at their open end, cut off with
    // them as one.
    Move best;
    bool bestStart = true;
    for (bool atStart : {true, false}) {
        const Move move = route(endOf(there, atStart, atB ? 1 : 0));
        if (move.found && (!best.found || move.metres + ReversalCost * double(move.legs.size())
                                                  < best.metres + ReversalCost * double(best.legs.size()))) {
            best = move;
            bestStart = atStart;
        }
    }
    if (!best.found)
        return false;
    const size_t gatheredCars = cuts[size_t(there)].vehicles.size();
    WorkStep made;
    made.cars = int(gatheredCars);
    keep(goal, best, WorkStep::Couple, QString("%1 cars to the %2 gathered").arg(count).arg(gatheredCars), made);
    couple(there, bestStart, lastLeadB);
    finish(goal, std::move(made));
    WorkStep cut;
    cut.action = WorkStep::Cut;
    cut.cars = int(count + gatheredCars);
    cut.atB = lastLeadB;
    cut.what = QString("leave the %1 cars").arg(count + gatheredCars);
    cutOff(count + gatheredCars, lastLeadB);
    finish(goal, std::move(cut));
    return true;
}

bool Planner::reachesRest(const EventPlan &event) const {
    const auto reaches = [&](int cut) {
        return route(endOf(cut, true, -1)).found || route(endOf(cut, false, -1)).found;
    };
    const int there = gathered(event);
    if (there >= 0 ? !reaches(there) : !route(sidingOf(event, -1)).found)
        return false;
    for (size_t c = 0; c < cuts.size(); ++c) {
        if (int(c) == there)
            continue;
        bool has = false;
        for (const Vehicle &vehicle : cuts[c].vehicles)
            has = has || listed(event.cars, vehicle.id);
        if (has && !reaches(int(c)))
            return false;
    }
    for (const EventPlan *other : pendingSidings)
        if (other != &event && gathered(*other) < 0 && !route(sidingOf(*other, -1)).found)
            return false;
    if (started.node >= 0) {
        Target back = started;
        if (!route(back).found) {
            back.forward = !back.forward;
            if (!route(back).found)
                return false;
        }
    }
    return true;
}

bool Planner::putAside(WorkGoal &goal, size_t count, bool atB, const EventPlan &event) {
    const size_t n = train.vehicles.size();
    const double need = atB ? carsLength(train.vehicles, n - count, n) : carsLength(train.vehicles, 0, count);
    const Snapshot start = save();
    const WorkGoal savedGoal = goal;
    WorkGoal bestGoal;
    Snapshot bestYard = start;
    bool found = false;
    for (int node : parkings(need)) {
        EventPlan siding;
        siding.siding = true;
        siding.sidingNode = node;
        siding.sidingFrom = 0.0f;
        siding.sidingTo = float(nodeLength(node));
        if (setOutEnd(goal, siding, count, atB, false, need + Margin) && !onBusy(cuts.back().track) && reachesRest(event)) {
            goal.steps.back().what = QString("set %1 cars aside").arg(count);
            const double cost = goal.metres + ReversalCost * goal.reversals;
            if (!found || cost < bestGoal.metres + ReversalCost * bestGoal.reversals) {
                found = true;
                bestGoal = goal;
                bestYard = save();
            }
        }
        restore(start);
        goal = savedGoal;
    }
    if (!found)
        return false;
    goal = bestGoal;
    restore(bestYard);
    return true;
}

bool Planner::fetch(WorkGoal &goal, int cut, bool atStart, int lead, const EventPlan &event) {
    const std::vector<Vehicle> &vehicles = cuts[size_t(cut)].vehicles;
    const size_t n = vehicles.size();
    // The cars beyond the last asked for, seen from the end coupled to.
    size_t beyond = 0;
    for (size_t k = 0; k < n; ++k) {
        const size_t index = atStart ? n - 1 - k : k;
        if (listed(event.cars, vehicles[index].id))
            break;
        ++beyond;
    }
    const QString name = cuts[size_t(cut)].name;
    WorkStep made;
    if (!step(goal, endOf(cut, atStart, lead), WorkStep::Couple, QString("couple to %1 (%2 cars)").arg(name).arg(n), made))
        return false;
    made.cars = int(n);
    couple(cut, atStart, lastLeadB);
    finish(goal, std::move(made));
    if (beyond > 0) {
        WorkStep left;
        left.action = WorkStep::Cut;
        left.cars = int(beyond);
        left.atB = lastLeadB;
        left.what = QString("leave %1 cars not asked for").arg(beyond);
        cutOff(beyond, lastLeadB);
        finish(goal, std::move(left));
    }
    return true;
}

bool Planner::sort(WorkGoal &goal, const EventPlan &event, const Ahead &ahead) {
    // Its searches go no further than it may drive in all.
    const double outerWithin = searchWithin;
    searchWithin = SortGiveUp;
    struct Restore {
        double &value;
        double was;
        ~Restore() { value = was; }
    } restoreWithin{searchWithin, outerWithin};
    std::vector<const EventPlan *> drops{&event};
    for (const EventPlan *drop : ahead.drops)
        if (drop != &event && drop->siding && drop->sidingNode >= 0)
            drops.push_back(drop);
    struct Option {
        enum Kind { Deliver, Aside, Fetch };
        Kind kind;
        bool atB;
        size_t count;
        const EventPlan *to;
        int cut;
        bool atStart;
        int lead;
    };
    for (int moves = 0; moves < SortMoves && !cancelled(); ++moves) {
        // Done: the cars in one cut in the siding.
        const int there = gathered(event);
        if (there >= 0 && cuts[size_t(there)].vehicles.size() == event.cars.size())
            return true;
        if (goal.metres > SortGiveUp || goal.reversals > SortGiveUpReversals) {
            goal.impractical = true;
            //% "sorting given up after %1 km of moves and %2 reversals"
            goal.why = qtTrId("work.why.sort.long").arg(goal.metres / 1000.0, 0, 'f', 1).arg(goal.reversals);
            return false;
        }
        size_t inTrain = 0;
        for (unsigned int car : event.cars)
            inTrain += trainIndex(car) >= 0 ? 1 : 0;
        std::vector<Option> options;
        if (inTrain > 0) {
            for (bool atB : {true, false}) {
                const Block block = endBlock(atB, drops);
                if (block.count == 0)
                    continue;
                if (block.to != nullptr)
                    options.push_back(Option{Option::Deliver, atB, block.count, block.to, -1, true, -1});
                if (block.to != &event)
                    options.push_back(Option{Option::Aside, atB, block.count, block.to, -1, true, -1});
            }
        } else {
            for (size_t c = 0; c < cuts.size(); ++c) {
                bool has = false;
                for (const Vehicle &vehicle : cuts[c].vehicles)
                    has = has || listed(event.cars, vehicle.id);
                if (!has || int(c) == there)
                    continue;
                for (bool atStart : {true, false})
                    for (int lead : {1, 0})
                        options.push_back(Option{Option::Fetch, true, 0, nullptr, int(c), atStart, lead});
            }
        }
        const Snapshot before = save();
        const WorkGoal savedGoal = goal;
        const double was = goal.metres + ReversalCost * goal.reversals;
        bool found = false;
        double bestCost = 0.0;
        WorkGoal bestGoal;
        Snapshot bestYard = before;
        for (const Option &option : options) {
            bool done = false;
            double extra = 0.0;
            switch (option.kind) {
            case Option::Deliver:
                done = deliver(goal, *option.to, option.count, option.atB);
                break;
            case Option::Aside:
                done = putAside(goal, option.count, option.atB, event);
                extra = option.to != nullptr ? AsideWanted : 0.0;
                break;
            case Option::Fetch:
                done = fetch(goal, option.cut, option.atStart, option.lead, event);
                break;
            }
            const double cost = goal.metres + ReversalCost * goal.reversals - was + extra;
            if (done && (!found || cost < bestCost)) {
                found = true;
                bestCost = cost;
                bestGoal = goal;
                bestYard = save();
            }
            restore(before);
            goal = savedGoal;
        }
        if (!found) {
            //% "sorting: no way on with %1 of the cars in the train"
            goal.why = qtTrId("work.why.sort.stuck").arg(inTrain);
            return false;
        }
        goal = bestGoal;
        restore(bestYard);
    }
    //% "sorting: more than %1 moves"
    goal.why = qtTrId("work.why.sort.moves").arg(SortMoves);
    return false;
}

bool Planner::setOutBlock(WorkGoal &goal, const EventPlan &event, const std::vector<unsigned int> &cars) {
    const size_t n = train.vehicles.size();
    size_t first = 0, last = 0, count = 0;
    const bool together = blockOf(train.vehicles, cars, first, last, count);
    if (count == 0) {
        //% "the cars are not in the train"
        goal.why = qtTrId("work.why.not.in.train");
        return false;
    }
    if (count == n) {
        // The whole train: into the siding as it is (the listed cars, engines
        // and all: decision 3).
        WorkStep made;
        if (!step(goal, sidingOf(event, -1), WorkStep::None, "the train into the siding", made)) {
            //% "no way into the siding for the whole train"
            goal.why = qtTrId("work.why.whole.train");
            return false;
        }
        finish(goal, std::move(made));
        return true;
    }
    if (!together) {
        //% "the cars are not together in the train: sorting"
        goal.why = qtTrId("work.why.not.together");
        return false;
    }
    const bool atA = first == 0, atB = last + 1 == n;
    // A block at an end, an engine in the rest: that end first into the
    // siding, cut off there.
    if (atA || atB) {
        const bool b = atB;
        if (!(b ? anyEngine(train.vehicles, 0, first) : anyEngine(train.vehicles, last + 1, n))) {
            //% "no engine would be left in the train"
            goal.why = qtTrId("work.why.no.engine");
            return false;
        }
        if (!setOutEnd(goal, event, count, b, true)) {
            //% "no way into the siding with these cars leading, nor round them"
            goal.why = qtTrId("work.why.leading");
            return false;
        }
        return true;
    }
    // In the middle: the whole train on its way to the siding as far as a
    // place where the part beyond the block, left there, is not in the
    // way; the block set out, the part coupled again. Tried at the places
    // short of the node ends on the way's last legs, at its reversals and
    // where the train stands; the shortest kept.
    const Snapshot before = save();
    const WorkGoal savedGoal = goal;
    WorkGoal bestGoal;
    Snapshot bestYard = before;
    bool found = false;
    for (bool keepA : {true, false}) {
        // (From the yard as it was: the other way's trials changed it.)
        restore(before);
        goal = savedGoal;
        if (!(keepA ? anyEngine(train.vehicles, 0, first) : anyEngine(train.vehicles, last + 1, n)))
            continue;
        const int lead = keepA ? 1 : 0;
        std::vector<Move> stages = stagesTowards(sidingOf(event, lead), lead);
        if (stages.size() > 8)
            stages.erase(stages.begin() + 7, stages.end() - 1);
        for (const Move &stage : stages) {
            restore(before);
            goal = savedGoal;
            if (stage.found) {
                WorkStep made;
                keep(goal, stage, WorkStep::None, "the whole train towards the siding", made);
                finish(goal, std::move(made));
            }
            const size_t beyond = keepA ? n - 1 - last : first;
            WorkStep leave;
            leave.action = WorkStep::Cut;
            leave.cars = int(beyond);
            leave.atB = keepA;
            leave.what = QString("leave %1 cars there").arg(beyond);
            const int put = cutOff(beyond, keepA);
            const unsigned int marker = cuts[size_t(put)].vehicles.front().id;
            finish(goal, std::move(leave));
            if (!setOutEnd(goal, event, count, keepA, true))
                continue;
            // Back to the cars left, at the end that was coupled to the block.
            int index = -1;
            for (size_t c = 0; c < cuts.size(); ++c)
                if (!cuts[c].vehicles.empty() && cuts[c].vehicles.front().id == marker)
                    index = int(c);
            WorkStep again;
            if (index < 0 || !step(goal, endOf(index, keepA, -1), WorkStep::Couple,
                                   QString("couple to the %1 cars left").arg(beyond), again))
                continue;
            again.cars = int(beyond);
            couple(index, keepA, lastLeadB);
            finish(goal, std::move(again));
            const double cost = goal.metres + ReversalCost * goal.reversals;
            if (!found || cost < bestGoal.metres + ReversalCost * bestGoal.reversals) {
                found = true;
                bestGoal = goal;
                bestYard = save();
            }
        }
    }
    if (found) {
        goal = bestGoal;
        restore(bestYard);
        return true;
    }
    restore(before);
    goal = savedGoal;
    //% "no way to set out the cars from the middle of the train"
    goal.why = qtTrId("work.why.middle");
    return false;
}

bool Planner::setOutEnd(WorkGoal &goal, const EventPlan &event, size_t count, bool atB, bool runRound, double depth) {
    const Snapshot start = save();
    const WorkGoal savedGoal = goal;
    WorkGoal bestGoal;
    Snapshot bestYard = start;
    bool found = false;
    const auto keepBest = [&]() {
        const double cost = goal.metres + ReversalCost * goal.reversals;
        if (!found || cost < bestGoal.metres + ReversalCost * bestGoal.reversals) {
            found = true;
            bestGoal = goal;
            bestYard = save();
        }
    };
    const auto setOut = [&](bool b) {
        WorkStep made;
        if (!step(goal, sidingOf(event, b ? 1 : 0, depth), WorkStep::Cut, QString("set out %1 cars").arg(count), made))
            return false;
        made.cars = int(count);
        made.atB = b;
        cutOff(count, b);
        finish(goal, std::move(made));
        return true;
    };
    if (setOut(atB))
        keepBest();
    // A run-around where the train stands or on its way to the siding.
    std::vector<Move> stages;
    if (runRound) {
        stages = stagesTowards(sidingOf(event, -1), -1);
        if (stages.size() > 6)
            stages.erase(stages.begin() + 5, stages.end() - 1);
    }
    for (const Move &stage : stages) {
        restore(start);
        goal = savedGoal;
        if (stage.found) {
            WorkStep made;
            keep(goal, stage, WorkStep::None, "the train towards the siding", made);
            finish(goal, std::move(made));
        }
        WorkStep leave;
        leave.action = WorkStep::Cut;
        leave.cars = int(count);
        leave.atB = atB;
        leave.what = QString("leave the %1 cars to run round them").arg(count);
        const int put = cutOff(count, atB);
        finish(goal, std::move(leave));
        // Their far end: the end of their track away from the rest.
        const bool farStart = !atB;
        WorkStep round;
        if (!step(goal, endOf(put, farStart, -1), WorkStep::Couple, "round the cars to their other end", round))
            continue;
        round.cars = int(count);
        couple(put, farStart, lastLeadB);
        finish(goal, std::move(round));
        if (setOut(lastLeadB))
            keepBest();
    }
    if (found) {
        goal = bestGoal;
        restore(bestYard);
        return true;
    }
    restore(start);
    goal = savedGoal;
    return false;
}

std::vector<int> Planner::parkings(double need) const {
    std::vector<int> out;
    if (train.track.empty())
        return out;
    // Through the junctions from either end of the train, by distance
    // (a reversal counted as in a move).
    using Entry = std::pair<double, std::pair<int, bool>>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    std::set<std::pair<int, bool>> seen;
    const RouteInterval &b = train.track.back(), &a = train.track.front();
    queue.push({std::fabs((b.forward() ? nodeLength(b.node) : 0.0) - b.to), {b.node, b.forward()}});
    queue.push({std::fabs((a.forward() ? 0.0 : nodeLength(a.node)) - a.from), {a.node, !a.forward()}});
    while (!queue.empty() && out.size() < ParkingChoices) {
        const auto [distance, at] = queue.top();
        queue.pop();
        if (distance > ParkingReach || !seen.insert(at).second)
            continue;
        for (const Exit &exit : exits(at.first, at.second)) {
            const double length = nodeLength(exit.node);
            const double entry = exit.forward ? 0.0 : length;
            int hit = -1;
            const double room = std::fabs(reach(exit.node, exit.forward, entry, -1, &hit) - entry);
            if (busy.count(exit.node) == 0 && room >= need + Margin
                    && std::find(out.begin(), out.end(), exit.node) == out.end())
                out.push_back(exit.node);
            queue.push({distance + length, {exit.node, exit.forward}});
        }
        // Or reversing there.
        queue.push({distance + nodeLength(at.first) + ReversalCost, {at.first, !at.second}});
    }
    return out;
}

bool Planner::leaveClear(WorkGoal &goal, size_t count, bool atB, const QString &what,
                         const std::function<bool()> &then) {
    const size_t n = train.vehicles.size();
    const double need = atB ? carsLength(train.vehicles, n - count, n) : carsLength(train.vehicles, 0, count);
    const auto here = [&]() {
        WorkStep made;
        made.action = WorkStep::Cut;
        made.cars = int(count);
        made.atB = atB;
        made.what = what;
        cutOff(count, atB);
        finish(goal, std::move(made));
        return then();
    };
    if (!onBusy(atB ? tailOf(train.track, need) : headOf(train.track, need)))
        return here();
    const Snapshot start = save();
    const WorkGoal savedGoal = goal;
    const bool blocksAi = on(aiBusy, atB ? tailOf(train.track, need) : headOf(train.track, need));
    // Where they are: what it costs.
    const bool hereOk = here();
    const double hereCost = goal.metres + ReversalCost * goal.reversals;
    restore(start);
    goal = savedGoal;
    WorkGoal bestGoal;
    Snapshot bestYard = start;
    bool found = false;
    for (int node : parkings(need)) {
        EventPlan siding;
        siding.siding = true;
        siding.sidingNode = node;
        siding.sidingFrom = 0.0f;
        siding.sidingTo = float(nodeLength(node));
        if (setOutEnd(goal, siding, count, atB, false) && !onBusy(cuts.back().track)) {
            goal.steps.back().what = what + " off the running lines";
            const double cost = goal.metres + ReversalCost * goal.reversals;
            if (then() && (!found || cost < bestGoal.metres + ReversalCost * bestGoal.reversals)) {
                found = true;
                bestGoal = goal;
                bestYard = save();
            }
        }
        restore(start);
        goal = savedGoal;
    }
    if (!found || (hereOk && !blocksAi && bestGoal.metres + ReversalCost * bestGoal.reversals > hereCost + ParkingExtra))
        return here();
    goal = bestGoal;
    restore(bestYard);
    return true;
}

std::vector<Move> Planner::stagesTowards(const Target &target, int lead) const {
    std::vector<Move> stages;
    for (int wholeLead : {lead, -1}) {
        Target to = target;
        to.lead = wholeLead;
        const Move whole = route(to);
        if (!whole.found)
            continue;
        // The legs before leg k whole, leg k up to `upTo` metres.
        const auto stageAt = [&](size_t k, double upTo) {
            Move stage;
            for (size_t j = 0; j < k; ++j) {
                stage.legs.push_back(whole.legs[j]);
                stage.metres += whole.legs[j].length();
            }
            if (upTo > 1.0) {
                WorkLeg part = whole.legs[k];
                part.way = headOf(whole.legs[k].way, upTo);
                stage.legs.push_back(part);
                stage.metres += part.length();
            }
            stage.found = !stage.legs.empty();
            return stage;
        };
        for (size_t k = whole.legs.size(); k-- > 0 && whole.legs.size() - k <= 3;) {
            const WorkLeg &leg = whole.legs[k];
            std::vector<double> ends;
            double along = 0.0;
            for (const RouteInterval &interval : leg.way) {
                if (along > 0.0)
                    ends.push_back(along);
                along += std::fabs(double(interval.to - interval.from));
            }
            if (k + 1 < whole.legs.size())
                ends.push_back(along + Margin);
            std::reverse(ends.begin(), ends.end());
            for (size_t i = 0; i < ends.size() && i < 4; ++i)
                if (ends[i] - Margin > 1.0)
                    stages.push_back(stageAt(k, ends[i] - Margin));
        }
    }
    stages.push_back(Move());
    return stages;
}

bool Planner::insert(WorkGoal &goal, const std::vector<unsigned int> &cars) {
    // The listed cars standing in one cut, all of it, between two runs of
    // listed cars that stand together in the train.
    size_t lo = cars.size(), hi = 0;
    for (size_t k = 0; k < cars.size(); ++k)
        if (trainIndex(cars[k]) < 0) {
            lo = std::min(lo, k);
            hi = k;
        }
    if (lo >= cars.size() || lo == 0 || hi + 1 >= cars.size())
        return false;
    const int c = cutOf(cars[lo]);
    if (c < 0 || cuts[size_t(c)].vehicles.size() != hi - lo + 1)
        return false;
    for (size_t k = lo; k <= hi; ++k)
        if (cutOf(cars[k]) != c)
            return false;
    const int before = trainIndex(cars[lo - 1]), after = trainIndex(cars[hi + 1]);
    if (before < 0 || after < 0 || std::abs(before - after) != 1)
        return false;
    // The gap between train places `gap - 1` and `gap`; the car before the
    // cut's cars in the list on side A or B of it.
    const size_t gap = size_t(std::max(before, after));
    const size_t n = train.vehicles.size();
    const bool firstOnA = before < after;
    const unsigned int touching = cars[lo];
    const Snapshot start = save();
    const WorkGoal savedGoal = goal;
    WorkGoal bestGoal;
    Snapshot bestYard = start;
    bool found = false;
    for (bool keepA : {true, false}) {
        restore(start);
        goal = savedGoal;
        if (!(keepA ? anyEngine(train.vehicles, 0, gap) : anyEngine(train.vehicles, gap, n)))
            continue;
        // The kept part's end at the gap leads to the cut, the end of the
        // cut with the car next to it in the list.
        const int lead = keepA ? 1 : 0;
        const bool kept = keepA == firstOnA;
        const YardTrain &standing = cuts[size_t(c)];
        const unsigned int near = kept ? touching : cars[hi];
        const bool atStart = standing.vehicles.front().id == near;
        for (const Move &stage : stagesTowards(endOf(c, atStart, lead), lead)) {
            restore(start);
            goal = savedGoal;
            if (stage.found) {
                WorkStep made;
                keep(goal, stage, WorkStep::None, "the whole train towards the cars", made);
                finish(goal, std::move(made));
            }
            const size_t left = keepA ? n - gap : gap;
            WorkStep leave;
            leave.action = WorkStep::Cut;
            leave.cars = int(left);
            leave.atB = keepA;
            leave.what = QString("leave %1 cars there").arg(left);
            const int put = cutOff(left, keepA);
            const unsigned int marker = cuts[size_t(put)].vehicles.front().id;
            finish(goal, std::move(leave));
            int index = cutOf(near);
            WorkStep take;
            if (index < 0 || !step(goal, endOf(index, cuts[size_t(index)].vehicles.front().id == near, lead),
                                   WorkStep::Couple, QString("couple to %1").arg(cuts[size_t(index)].name), take))
                continue;
            take.cars = int(hi - lo + 1);
            couple(index, cuts[size_t(index)].vehicles.front().id == near, lastLeadB);
            finish(goal, std::move(take));
            index = -1;
            for (size_t k = 0; k < cuts.size(); ++k)
                if (!cuts[k].vehicles.empty() && cuts[k].vehicles.front().id == marker)
                    index = int(k);
            WorkStep again;
            if (index < 0 || !step(goal, endOf(index, keepA, lead), WorkStep::Couple,
                                   QString("couple to the %1 cars left").arg(left), again))
                continue;
            again.cars = int(left);
            couple(index, keepA, lastLeadB);
            finish(goal, std::move(again));
            const double cost = goal.metres + ReversalCost * goal.reversals;
            if (!found || cost < bestGoal.metres + ReversalCost * bestGoal.reversals) {
                found = true;
                bestGoal = goal;
                bestYard = save();
            }
        }
    }
    if (found) {
        goal = bestGoal;
        restore(bestYard);
        return true;
    }
    restore(start);
    goal = savedGoal;
    return false;
}

bool Planner::assemble(WorkGoal &goal, const EventPlan &event, const Ahead &ahead) {
    const std::vector<unsigned int> &cars = event.cars;
    const bool atLocation = event.kind == EventPlan::AssembleTrainAtLocation && event.siding && event.sidingNode >= 0;
    // The player's train into the siding where the event asks for it.
    const auto intoSiding = [&]() {
        if (!atLocation || inSiding(train, event))
            return true;
        WorkStep made;
        if (!step(goal, sidingOf(event, -1), WorkStep::None, "the train into the siding", made)) {
            //% "no way into the siding"
            goal.why = qtTrId("work.why.siding");
            return false;
        }
        finish(goal, std::move(made));
        return true;
    };
    // Done where a train or a cut is exactly the cars (as the core checks).
    for (const YardTrain &cut : cuts)
        if (exactly(cut.vehicles, cars))
            return intoSiding();
    Ahead mine = ahead;
    mine.order = &cars;
    // Cars of a cut wanted between two parts of the train: put in there.
    insert(goal, cars);
    if (!pickUp(goal, cars, mine))
        return false;
    const size_t n = train.vehicles.size();
    size_t first = 0, last = 0, count = 0;
    if (!blockOf(train.vehicles, cars, first, last, count)) {
        //% "the cars asked for are not together in the train: sorting"
        goal.why = qtTrId("work.why.assemble.together");
        return false;
    }
    const std::vector<Vehicle> block(train.vehicles.begin() + long(first), train.vehicles.begin() + long(last) + 1);
    if (!exactly(block, cars)) {
        // Where the order first breaks (read the way more of it matches).
        size_t forward = 0, backward = 0;
        while (forward < cars.size() && block[forward].id == cars[forward])
            ++forward;
        while (backward < cars.size() && block[block.size() - 1 - backward].id == cars[backward])
            ++backward;
        const size_t at = std::max(forward, backward);
        const unsigned int there = forward >= backward ? block[at].id : block[block.size() - 1 - at].id;
        //% "the cars are in another order: car %1 asked for at place %2, car %3 there"
        goal.why = qtTrId("work.why.order")
                       .arg(carName(cars[at])).arg(at + 1).arg(carName(there));
        return false;
    }
    if (count == n)
        return intoSiding();
    const bool atA = first == 0, atB = last + 1 == n;
    if (!atA && !atB) {
        if (atLocation)
            return setOutBlock(goal, event, cars);
        //% "the cars asked for are in the middle of the train"
        goal.why = qtTrId("work.why.assemble.middle");
        return false;
    }
    // At an end: with an engine among them, the others cut off where it
    // stands; without, they set out (into the siding, or where it stands).
    if (anyEngine(train.vehicles, first, last + 1))
        return leaveClear(goal, n - count, atA, QString("leave %1 cars not asked for").arg(n - count), intoSiding);
    if (atLocation)
        return setOutBlock(goal, event, cars);
    return leaveClear(goal, count, atB, QString("leave the %1 cars asked for").arg(count), [] { return true; });
}

double Planner::wayTo(const EventPlan &event) const {
    double best = -1.0;
    const auto consider = [&](const Target &target) {
        const Move move = route(target);
        if (move.found && (best < 0.0 || move.metres < best))
            best = move.metres;
    };
    for (unsigned int car : event.cars) {
        const int c = cutOf(car);
        if (c >= 0) {
            consider(endOf(c, true, -1));
            consider(endOf(c, false, -1));
            break;
        }
    }
    if (event.siding && event.sidingNode >= 0)
        consider(sidingOf(event, -1));
    return std::max(0.0, best);
}

WorkGoal Planner::goalFor(int e, const Ahead &ahead) {
    const EventPlan &event = in.events[size_t(e)];
    const double direct = wayTo(event);
    WorkGoal goal = planGoal(e, ahead);
    const int maxReversals = goal.sorted ? ImpracticalSortReversals : ImpracticalReversals;
    if (goal.planned && (goal.metres - direct > ImpracticalExtra || goal.reversals > maxReversals)) {
        goal.planned = false;
        goal.impractical = true;
        //% "impractical: %1 km of moves (%2 km more than the way there), %3 reversals"
        goal.why = qtTrId("work.why.impractical")
                       .arg(goal.metres / 1000.0, 0, 'f', 1).arg((goal.metres - direct) / 1000.0, 0, 'f', 1)
                       .arg(goal.reversals);
    }
    return goal;
}

WorkGoal Planner::planGoal(int e, const Ahead &ahead) {
    const EventPlan &event = in.events[size_t(e)];
    WorkGoal goal;
    goal.event = e;
    goal.name = event.name;
    goal.kind = event.kind;
    goal.cars = int(event.cars.size());
    switch (event.kind) {
    case EventPlan::PickUpWagons:
        goal.planned = pickUp(goal, event.cars, ahead);
        break;
    case EventPlan::DropOffWagonsAtLocation:
        goal.planned = setOut(goal, event, ahead);
        break;
    case EventPlan::AssembleTrain:
    case EventPlan::AssembleTrainAtLocation:
        goal.planned = assemble(goal, event, ahead);
        break;
    default:
        //% "not a wagon event"
        goal.why = qtTrId("work.why.not.wagons");
        break;
    }
    return goal;
}

WorkPlan Planner::plan() {
    WorkPlan out;
    const TrainPlan *player = nullptr;
    for (const TrainPlan &plan : in.trains)
        if (plan.player)
            player = &plan;
    if (player == nullptr || player->route.parts.empty() || in.network == nullptr) {
        out.notes << "no player's train on the track";
        return out;
    }
    // The player's train where it starts: end B its front, its vehicles from
    // its rear (end A).
    const RouteTrack &part = player->route.parts.front();
    std::vector<RouteInterval> whole;
    for (const RouteInterval &interval : part.intervals())
        appendTo(whole, interval.node, interval.from, interval.to);
    train.name = player->service;
    train.track = tailOf(headOf(whole, player->startFront), player->length);
    train.vehicles = turned(player->vehicles);
    if (!train.track.empty()) {
        started.kind = Target::Point;
        started.node = train.track.back().node;
        started.x = train.track.back().to;
        started.forward = train.track.back().forward();
    }
    for (const StaticConsist &consist : in.staticConsists) {
        YardTrain cut;
        cut.name = consist.name;
        cut.vehicles = consist.vehicles;
        cut.track = consist.track;
        cut.loose = true;
        cuts.push_back(cut);
    }
    out.path = -player->startFront;
    for (const RouteTrack &routePart : player->route.parts)
        out.path += routePart.length();
    for (const TrainPlan &plan : in.trains)
        for (const RouteTrack &routePart : plan.route.parts)
            for (const RouteInterval &interval : routePart.intervals()) {
                busy.insert(interval.node);
                if (!plan.player)
                    aiBusy.insert(interval.node);
            }
    for (const EventPlan &event : in.events)
        if (event.siding && event.sidingNode >= 0)
            busy.insert(event.sidingNode);
    // The event chain: events not about cars fire when active (one ending
    // the activity in failure never); of the wagon events active together
    // the first (in the list's order) that can be planned is done, each
    // from where the ones before left the cars. One none can be planned:
    // reported and taken as done, the chain going on.
    std::vector<int> level(in.events.size(), 0);
    std::vector<bool> fired(in.events.size(), false);
    for (size_t e = 0; e < in.events.size(); ++e)
        level[e] = in.events[e].activation;
    bool ended = false;
    // The event ending the activity in success at a place the player's
    // train comes to.
    int endsAt = -1;
    const auto fire = [&](int e) {
        const EventPlan &event = in.events[size_t(e)];
        fired[size_t(e)] = true;
        if (event.success && event.kind == EventPlan::Location && event.trigger < 0 && !event.places.empty())
            endsAt = e;
        for (int k : event.activate)
            if (k >= 0 && size_t(k) < level.size())
                level[size_t(k)] = 1;
        for (int k : event.restore)
            if (k >= 0 && size_t(k) < level.size())
                level[size_t(k)] = in.events[size_t(k)].activation;
        for (int k : event.decrease)
            if (k >= 0 && size_t(k) < level.size())
                --level[size_t(k)];
        for (int k : event.increase)
            if (k >= 0 && size_t(k) < level.size())
                ++level[size_t(k)];
        ended = ended || event.success;
    };
    int wagonEvents = 0;
    for (const EventPlan &event : in.events)
        wagonEvents += event.cars.empty() ? 0 : 1;
    const auto report = [&]() {
        if (progress != nullptr && progress->report)
            progress->report(std::min(int(out.goals.size()), wagonEvents), wagonEvents);
    };
    report();
    for (int guard = 0; guard < 1000 && !ended && !cancelled(); ++guard) {
        // (One ending the activity in success only once no wagon goal is
        // active: it is where the activity ends.)
        std::vector<int> active;
        for (bool again = true; again;) {
            again = false;
            active.clear();
            for (size_t e = 0; e < in.events.size(); ++e)
                if (!fired[e] && level[e] > 0 && !in.events[e].cars.empty())
                    active.push_back(int(e));
            for (size_t e = 0; e < in.events.size() && !again; ++e)
                if (!fired[e] && level[e] > 0 && in.events[e].cars.empty() && !in.events[e].failure
                        && (!in.events[e].success || active.empty())) {
                    fire(int(e));
                    again = !ended;
                }
        }
        if (ended || active.empty())
            break;
        Ahead ahead;
        for (size_t e = 0; e < in.events.size(); ++e)
            if (!fired[e] && in.events[e].kind == EventPlan::DropOffWagonsAtLocation && in.events[e].siding)
                ahead.drops.push_back(&in.events[e]);
        // Tried first: goals whose outcomes take no other active goal's level
        // down (one that does goes after it); one ending the activity in
        // success last.
        // A pickup whose cars an active drop-off wants too goes after it:
        // that one's moves bring them into the train.
        const auto rank = [&](int e) {
            const EventPlan &event = in.events[size_t(e)];
            int kills = 0;
            for (int k : event.decrease)
                kills += std::find(active.begin(), active.end(), k) != active.end() ? 1 : 0;
            bool covered = false;
            if (event.kind == EventPlan::PickUpWagons)
                for (int other : active) {
                    const EventPlan &drop = in.events[size_t(other)];
                    bool all = drop.kind == EventPlan::DropOffWagonsAtLocation;
                    for (unsigned int car : event.cars)
                        all = all && listed(drop.cars, car);
                    covered = covered || all;
                }
            return (covered ? 4 : 0) + (event.success ? 2 : 0) + (kills > 0 ? 1 : 0);
        };
        std::stable_sort(active.begin(), active.end(), [&](int a, int b) { return rank(a) < rank(b); });
        watching.clear();
        for (int e : active)
            if (in.events[size_t(e)].kind == EventPlan::PickUpWagons)
                watching.push_back(e);
        pendingSidings.clear();
        for (size_t e = 0; e < in.events.size(); ++e)
            if (!fired[e] && !in.events[e].cars.empty() && in.events[e].siding && in.events[e].sidingNode >= 0)
                pendingSidings.push_back(&in.events[e]);
        satisfied.clear();
        const Snapshot before = save();
        int chosen = -1, failedFirst = -1;
        WorkGoal chosenGoal, failedGoal;
        std::vector<WorkGoal> failures;
        for (int e : active) {
            restore(before);
            Ahead others = ahead;
            others.drops.erase(std::remove(others.drops.begin(), others.drops.end(), &in.events[size_t(e)]), others.drops.end());
            WorkGoal goal = goalFor(e, others);
            if (goal.planned) {
                chosen = e;
                chosenGoal = goal;
                break;
            }
            if (failedFirst < 0) {
                failedFirst = e;
                failedGoal = goal;
            }
            failures.push_back(goal);
        }
        // The activity ending in success with goals undone: those reported
        // before it (with why).
        if (chosen >= 0 && in.events[size_t(chosen)].success)
            for (const WorkGoal &failure : failures) {
                out.goals.push_back(failure);
                fired[size_t(failure.event)] = true;
            }
        if (chosen < 0) {
            restore(before);
            chosen = failedFirst;
            chosenGoal = failedGoal;
        }
        // Pickups its moves did on the way: done then, before it.
        if (chosenGoal.planned)
            for (int e : satisfied)
                if (e != chosen && !fired[size_t(e)]) {
                    WorkGoal done;
                    done.event = e;
                    done.name = in.events[size_t(e)].name;
                    done.kind = in.events[size_t(e)].kind;
                    done.cars = int(in.events[size_t(e)].cars.size());
                    done.planned = true;
                    done.why = QString("done on the moves of %1").arg(chosenGoal.name);
                    out.goals.push_back(done);
                    fire(e);
                }
        watching.clear();
        satisfied.clear();
        out.goals.push_back(chosenGoal);
        fire(chosen);
        report();
    }
    if (cancelled()) {
        out.cancelled = true;
        return out;
    }
    for (size_t e = 0; e < in.events.size(); ++e)
        if (!in.events[e].cars.empty() && !fired[e])
            out.notes << QString("%1: never active").arg(in.events[e].name);
    // Where the work leaves the path: as far along it as its first move
    // runs together with it.
    out.departure = player->startFront;
    const WorkLeg *first = nullptr;
    for (const WorkGoal &goal : out.goals)
        for (const WorkStep &step : goal.steps)
            for (const WorkLeg &leg : step.legs)
                if (goal.planned && first == nullptr)
                    first = &leg;
    if (first != nullptr && first->leadB) {
        const std::vector<RouteInterval> path = intervalsOf(player->route.parts.front());
        out.departure += together(tailOf(path, lengthOf(path) - player->startFront), first->way);
    }
    // Back to the path, the nearest way not skipping its stops, waits and
    // event places; else to its end.
    out.train = train;
    out.cuts = cuts;
    const bool wasB = lastLeadB;
    pathFrom(*player, out.departure);
    // The first move ran the whole path: the run ends with the work.
    if (player->route.parts.size() == 1 && out.departure >= player->route.parts.front().length() - 0.5) {
        out.back = 0.0;
        return out;
    }
    Target back;
    back.kind = Target::Path;
    Move move = route(back);
    // The activity ending at a place the rest of the path does not pass:
    // there instead.
    if (endsAt >= 0) {
        const EventPlan &event = in.events[size_t(endsAt)];
        const std::vector<RouteTrack> &parts = player->route.parts;
        bool passed = false;
        for (const TrackPlace &place : event.places)
            for (size_t p = move.found ? size_t(move.part) : parts.size(); p < parts.size(); ++p) {
                double at = -1.0;
                passed = passed || (parts[p].find(place.node, place.distance, at)
                                    && (p > size_t(move.part) || at >= move.at - 0.5));
            }
        Move there;
        double cost = 0.0;
        for (const TrackPlace &place : event.places)
            for (bool forward : {true, false}) {
                Target target;
                target.kind = Target::Point;
                target.node = place.node;
                target.x = place.distance;
                target.forward = forward;
                const Move found = route(target);
                const double weighed = found.metres + ReversalCost * double(found.legs.size());
                if (!passed && found.found && (!there.found || weighed < cost)) {
                    there = found;
                    cost = weighed;
                }
            }
        if (there.found) {
            move = there;
            out.endEvent = endsAt;
        }
    }
    if (move.found && out.endEvent < 0) {
        out.rejoinPart = move.part;
        out.rejoinAt = move.at;
        out.skipped = int(std::lround(skipCost(move.part, move.at) / SkipFeature));
    } else if (!move.found) {
        const RouteTrack &lastPart = player->route.parts.back();
        const RoutePoint end = lastPart.locate(lastPart.length());
        back.kind = Target::Point;
        back.node = end.node;
        back.x = end.distance;
        back.forward = end.forward;
        move = route(back);
    }
    if (move.found) {
        out.back = move.metres;
        bool lead = wasB;
        for (const WorkLeg &leg : move.legs) {
            if (leg.leadB != lead)
                ++out.backReversals;
            lead = leg.leadB;
        }
        moveBy(move);
        out.backLegs = move.legs;
    }
    return out;
}

}

double YardTrain::length() const {
    return lengthOf(track);
}

double WorkLeg::length() const {
    return lengthOf(way);
}

WorkPlan PlanWork(const SimulationInput &input, const WorkProgress *progress) {
    Planner planner(input, progress);
    return planner.plan();
}

bool ApplyWork(SimulationInput &input, const WorkProgress *progress) {
    TrainPlan *player = nullptr;
    for (TrainPlan &plan : input.trains)
        if (plan.player)
            player = &plan;
    bool wagons = false;
    for (const EventPlan &event : input.events)
        wagons = wagons || !event.cars.empty();
    if (player == nullptr || !wagons || player->route.parts.empty() || input.network == nullptr)
        return true;
    const WorkPlan work = PlanWork(input, progress);
    if (work.cancelled)
        return false;
    bool any = false;
    for (const WorkGoal &goal : work.goals) {
        input.workOrders.push_back(SimulationInput::WorkOrder{goal.event, goal.planned, goal.impractical, true, goal.why});
        any = any || (goal.planned && !goal.steps.empty());
    }
    for (size_t e = 0; e < input.events.size(); ++e) {
        bool listed = false;
        for (const WorkGoal &goal : work.goals)
            listed = listed || goal.event == int(e);
        if (!input.events[e].cars.empty() && !listed)
            input.workOrders.push_back(SimulationInput::WorkOrder{int(e), false, false, false,
                                                                  //% "never active in the event chain"
                                                                  qtTrId("work.why.never.active")});
    }
    if (!any)
        return true;
    // The parts: the path up to where the work leaves it, the work's moves
    // (a new part at each reversal: from where the train stands back over
    // its track, then on), the way back merged into the path's last part.
    const std::vector<RouteInterval> pathFirst = intervalsOf(player->route.parts.front());
    const std::vector<RouteInterval> pathLast = intervalsOf(player->route.parts.back());
    const double start = player->startFront;
    std::vector<RouteTrack> parts;
    std::vector<double> workFrom;
    std::vector<double> workTo;
    std::vector<WaitPlan> waits;
    std::vector<RouteInterval> current = headOf(pathFirst, start);
    double currentWork = -1.0;
    double currentWorkTo = -1.0;
    bool lead = true;
    bool started = false;
    const double departure = work.departure;
    int cars = int(player->vehicles.size());
    const auto flush = [&]() {
        RouteTrack track;
        for (const RouteInterval &interval : current)
            track.append(interval.node, interval.from, interval.to);
        parts.push_back(track);
        workFrom.push_back(currentWork);
        workTo.push_back(currentWorkTo);
        current.clear();
        currentWork = 0.0;
        currentWorkTo = -1.0;
    };
    const auto addLeg = [&](const WorkLeg &leg) {
        if (!started) {
            started = true;
            if (leg.leadB) {
                // The way the work starts along the path: the path run as
                // far (its stops and waits kept), the work from there.
                current = headOf(pathFirst, departure);
                appendAll(current, tailOf(leg.way, std::max(0.0, leg.length() - (departure - start))));
                currentWork = departure;
                return;
            }
            currentWork = start;
        }
        if (leg.leadB == lead) {
            appendAll(current, leg.way);
            return;
        }
        flush();
        current = leg.body;
        appendAll(current, leg.way);
        lead = leg.leadB;
    };
    for (const WorkGoal &goal : work.goals) {
        if (!goal.planned)
            continue;
        for (const WorkStep &step : goal.steps) {
            for (const WorkLeg &leg : step.legs)
                addLeg(leg);
            if (step.action == WorkStep::Cut) {
                // The cars left at the leading end: the rear ones kept.
                WaitPlan wait;
                wait.part = int(parts.size());
                wait.front = lengthOf(current);
                wait.keep = cars - step.cars;
                wait.keepFront = step.atB != lead;
                waits.push_back(wait);
                cars -= step.cars;
            } else if (step.action == WorkStep::Couple) {
                cars += step.cars;
                // The part goes on over them (a coupling at the leading end).
                appendAll(current, step.over);
            }
        }
    }
    for (const WorkLeg &leg : work.backLegs)
        addLeg(leg);
    std::vector<StopPlan> stops;
    std::vector<PassingPlan> passing;
    // The path up to where the work leaves it: its stops, waits and passing
    // paths kept.
    for (const StopPlan &stop : player->stops)
        if (stop.part == 0 && stop.front <= departure + 0.5)
            stops.push_back(stop);
    for (const WaitPlan &wait : player->waits)
        if (wait.part == 0 && wait.front <= departure + 0.5)
            waits.push_back(wait);
    for (const PassingPlan &side : player->passing)
        if (side.part == 0 && side.to <= departure)
            passing.push_back(side);
    if (work.rejoinPart >= 0) {
        // Back on the path: the rest of the part it comes to run on from
        // there, its parts after it as they are, with their stops, waits
        // and passing paths.
        const int rejoin = work.rejoinPart;
        const double at = work.rejoinAt;
        const std::vector<RouteInterval> rest = intervalsOf(player->route.parts[size_t(rejoin)]);
        const int joined = int(parts.size());
        const double shift = lengthOf(current) - at;
        currentWorkTo = lengthOf(current);
        appendAll(current, tailOf(rest, std::max(0.0, lengthOf(rest) - at)));
        const auto place = [&](int &part, double from) {
            if (part < rejoin || (part == rejoin && from < at - 0.5))
                return false;
            if (part == 0 && from <= departure + 0.5)
                return false;
            part = part == rejoin ? joined : joined + part - rejoin;
            return true;
        };
        for (StopPlan stop : player->stops) {
            const bool same = stop.part == rejoin;
            if (place(stop.part, stop.front)) {
                if (same) {
                    stop.front += shift;
                    stop.platformEnd += shift;
                    stop.platformStart += shift;
                }
                stops.push_back(stop);
            }
        }
        for (WaitPlan wait : player->waits) {
            const bool same = wait.part == rejoin;
            if (place(wait.part, wait.front)) {
                wait.front += same ? shift : 0.0;
                waits.push_back(wait);
            }
        }
        for (PassingPlan side : player->passing) {
            const bool same = side.part == rejoin;
            if (place(side.part, side.from)) {
                if (same) {
                    side.from += shift;
                    side.to += shift;
                }
                passing.push_back(side);
            }
        }
        flush();
        for (size_t q = size_t(rejoin) + 1; q < player->route.parts.size(); ++q) {
            parts.push_back(player->route.parts[q]);
            workFrom.push_back(-1.0);
            workTo.push_back(-1.0);
        }
    } else {
        // Where the way back runs together with the path's last part to
        // its end: that stretch's stops and waits kept on the last part.
        const double merged = together(backwardsOf(current), backwardsOf(pathLast));
        const double lastLength = lengthOf(pathLast);
        const int lastPath = int(player->route.parts.size()) - 1;
        const int finalPart = int(parts.size());
        const double finalLength = lengthOf(current);
        for (StopPlan stop : player->stops) {
            if (stop.part == lastPath && lastLength - stop.platformStart <= merged + 0.5
                    && !(stop.part == 0 && stop.front <= departure + 0.5)) {
                const double shift = finalLength - lastLength;
                stop.part = finalPart;
                stop.front += shift;
                stop.platformEnd += shift;
                stop.platformStart += shift;
                stops.push_back(stop);
            }
        }
        for (WaitPlan wait : player->waits) {
            if (wait.part == lastPath && lastLength - wait.front <= merged + 0.5
                    && !(wait.part == 0 && wait.front <= departure + 0.5)) {
                wait.part = finalPart;
                wait.front += finalLength - lastLength;
                waits.push_back(wait);
            }
        }
        flush();
    }
    std::stable_sort(waits.begin(), waits.end(), [](const WaitPlan &a, const WaitPlan &b) {
        return a.part != b.part ? a.part < b.part : a.front < b.front;
    });
    std::stable_sort(stops.begin(), stops.end(), [](const StopPlan &a, const StopPlan &b) {
        return a.part != b.part ? a.part < b.part : a.front < b.front;
    });
    std::stable_sort(passing.begin(), passing.end(), [](const PassingPlan &a, const PassingPlan &b) {
        return a.part != b.part ? a.part < b.part : a.from < b.from;
    });
    player->route.parts = parts;
    player->stops = stops;
    player->waits = waits;
    player->passing = passing;
    player->workFrom = workFrom;
    player->workTo = workTo;
    player->projection.assign(parts.size(), Projection());
    player->limits.assign(parts.size(), {});
    for (size_t p = 0; p < parts.size(); ++p)
        FinishPart(input, *player, parts[p], player->projection[p], player->limits[p]);
    return true;
}

}
