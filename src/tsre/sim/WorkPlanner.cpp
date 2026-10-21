#include <tsre/sim/WorkPlanner.h>

#include <QSet>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
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

std::vector<Vehicle> turned(const std::vector<Vehicle> &vehicles) {
    std::vector<Vehicle> out(vehicles.rbegin(), vehicles.rend());
    for (Vehicle &vehicle : out)
        vehicle.flip = !vehicle.flip;
    return out;
}

bool engine(const Vehicle &vehicle) {
    return vehicle.type >= 4;
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
        length += vehicles[k].length;
    return length;
}

QString carName(unsigned int car) {
    return QString("%1-%2").arg(car / 65536u).arg(car % 65536u);
}

// Where to come to: a cut's end to couple to (from outside it, the way
// given), a siding's stretch to stand in (the leading end in it, as deep in
// as the track allows), a place (the path's end, come to the way given).
// Which end must lead (-1 either); the cut coupled to (not in the way).
struct Target {
    enum Kind { Couple, Siding, Point };
    Kind kind = Point;
    int node = -1;
    double x = 0.0;
    bool forward = true;
    double from = 0.0;
    double to = 0.0;
    int lead = -1;
    int skip = -1;
};

struct Move {
    bool found = false;
    std::vector<WorkLeg> legs;
    double metres = 0.0;
};

class Planner {
public:
    Planner(const SimulationInput &input) : in(input), net(*input.network) {}

    WorkPlan plan();

private:
    const SimulationInput &in;
    const TrackNetwork &net;
    YardTrain train;
    std::vector<YardTrain> cuts;
    bool lastLeadB = true;

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
    // The train after a move, a coupling (to the cut `cut` at its start or
    // end, by its leading end) and a cut (`count` cars at end B or A left
    // standing).
    void moveBy(const Move &move);
    void couple(int cut, bool atStart, bool leadB);
    int cutOff(size_t count, bool atB);

    bool inSiding(const YardTrain &cut, const EventPlan &event) const;
    // The yard as it is, to try a way and go back.
    struct Snapshot {
        YardTrain train;
        std::vector<YardTrain> cuts;
        bool lastLeadB;
    };
    Snapshot save() const { return Snapshot{train, cuts, lastLeadB}; }
    void restore(const Snapshot &snapshot) {
        train = snapshot.train;
        cuts = snapshot.cuts;
        lastLeadB = snapshot.lastLeadB;
    }
    // A move kept in a goal (its metres and reversals counted) and made; a
    // move found to a target and kept.
    void keep(WorkGoal &goal, const Move &move, WorkStep::Action action, const QString &what, WorkStep &made);
    bool step(WorkGoal &goal, const Target &target, WorkStep::Action action, QString what, WorkStep &made);
    Target sidingOf(const EventPlan &event, int lead) const;
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
    bool setOutEnd(WorkGoal &goal, const EventPlan &event, size_t count, bool atB, bool runRound);
    bool assemble(WorkGoal &goal, const EventPlan &event, const Ahead &ahead);
    // Places to leave part of the train on its way to a target (with either
    // end leading, or the one given): short of the node ends on the way's
    // last legs, at its reversals, and where it stands (an empty move).
    std::vector<Move> stagesTowards(const Target &target, int lead) const;
    // The cars of a cut put into the train where the listed order wants
    // them, between two of its parts: the train cut there, the part with an
    // engine to the cars and back.
    bool insert(WorkGoal &goal, const std::vector<unsigned int> &cars);
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
    std::map<std::tuple<int, bool, int, int>, double> best;
    const double length = train.length();
    const auto push = [&](State state) {
        if (state.kind != 0) {
            const auto key = std::make_tuple(state.node, state.forward, state.lead, state.kind);
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
    if (train.track.empty())
        return {};
    const RouteInterval &b = train.track.back();
    const RouteInterval &a = train.track.front();
    push(State{b.node, b.forward(), 1, b.to, 0, lastLeadB ? 0.0 : ReversalCost, 0.0, -1, {}, false});
    push(State{a.node, !a.forward(), 0, a.from, 0, lastLeadB ? ReversalCost : 0.0, 0.0, -1, {}, false});
    int found = -1;
    double foundCost = 1e18;
    std::vector<RouteInterval> lastPiece;
    double lastMetres = 0.0;
    int expanded = 0;
    while (!queue.empty() && expanded < 200000) {
        const auto [cost, index] = queue.top();
        queue.pop();
        if (cost >= foundCost)
            break;
        ++expanded;
        const State s = states[size_t(index)];
        int hit = -1;
        const double limit = reach(s.node, s.forward, s.start, target.skip, &hit);
        const bool leadOk = target.lead < 0 || target.lead == s.lead;
        // The target on this node, the way it goes.
        if (leadOk && s.node == target.node) {
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
    return move;
}

void Planner::moveBy(const Move &move) {
    const double length = train.length();
    for (const WorkLeg &leg : move.legs) {
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
    made.legs = move.legs;
    made.action = action;
    made.what = what;
    goal.metres += move.metres;
    bool lead = lastLeadB;
    for (const WorkLeg &leg : move.legs) {
        if (leg.leadB != lead)
            ++goal.reversals;
        lead = leg.leadB;
    }
    moveBy(move);
}

bool Planner::step(WorkGoal &goal, const Target &target, WorkStep::Action action, QString what, WorkStep &made) {
    const Move move = route(target);
    if (!move.found)
        return false;
    keep(goal, move, action, what, made);
    return true;
}

Target Planner::sidingOf(const EventPlan &event, int lead) const {
    Target target;
    target.kind = Target::Siding;
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
    if (made.action == WorkStep::Couple)
        ++goal.couplings;
    if (made.action == WorkStep::Cut)
        ++goal.cuts;
    goal.steps.push_back(std::move(made));
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
            goal.why = QString("car %1 is in no train or cut").arg(carName(missing));
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
            goal.why = QString("no way to %1").arg(name);
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
    goal.why = "too many cuts";
    return false;
}

bool Planner::setOut(WorkGoal &goal, const EventPlan &event, const Ahead &ahead) {
    if (!event.siding || event.sidingNode < 0) {
        goal.why = "the event has no siding";
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
    Ahead mine = ahead;
    mine.drops.insert(mine.drops.begin(), &event);
    if (!pickUp(goal, event.cars, mine))
        return false;
    return setOutBlock(goal, event, event.cars);
}

bool Planner::setOutBlock(WorkGoal &goal, const EventPlan &event, const std::vector<unsigned int> &cars) {
    const size_t n = train.vehicles.size();
    size_t first = 0, last = 0, count = 0;
    const bool together = blockOf(train.vehicles, cars, first, last, count);
    if (count == 0) {
        goal.why = "the cars are not in the train";
        return false;
    }
    if (count == n) {
        // The whole train: into the siding as it is (the listed cars, engines
        // and all: decision 3).
        WorkStep made;
        if (!step(goal, sidingOf(event, -1), WorkStep::None, "the train into the siding", made)) {
            goal.why = "no way into the siding for the whole train";
            return false;
        }
        finish(goal, std::move(made));
        return true;
    }
    if (!together) {
        goal.why = "the cars are not together in the train";
        return false;
    }
    const bool atA = first == 0, atB = last + 1 == n;
    // A block at an end, an engine in the rest: that end first into the
    // siding, cut off there.
    if (atA || atB) {
        const bool b = atB;
        if (!(b ? anyEngine(train.vehicles, 0, first) : anyEngine(train.vehicles, last + 1, n))) {
            goal.why = "no engine would be left in the train";
            return false;
        }
        if (!setOutEnd(goal, event, count, b, true)) {
            goal.why = "no way into the siding with these cars leading, nor round them";
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
    goal.why = "no way to set out the cars from the middle of the train";
    return false;
}

bool Planner::setOutEnd(WorkGoal &goal, const EventPlan &event, size_t count, bool atB, bool runRound) {
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
        if (!step(goal, sidingOf(event, b ? 1 : 0), WorkStep::Cut, QString("set out %1 cars").arg(count), made))
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
            goal.why = "no way into the siding";
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
        goal.why = "the cars asked for are not together in the train";
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
        goal.why = QString("the cars are in another order: car %1 asked for at place %2, car %3 there")
                       .arg(carName(cars[at])).arg(at + 1).arg(carName(there));
        return false;
    }
    if (count == n)
        return intoSiding();
    const bool atA = first == 0, atB = last + 1 == n;
    if (!atA && !atB) {
        if (atLocation)
            return setOutBlock(goal, event, cars);
        goal.why = "the cars asked for are in the middle of the train";
        return false;
    }
    // At an end: with an engine among them, the others cut off where it
    // stands; without, they set out (into the siding, or where it stands).
    if (anyEngine(train.vehicles, first, last + 1)) {
        WorkStep made;
        made.action = WorkStep::Cut;
        made.cars = int(n - count);
        made.atB = atA;
        made.what = QString("leave %1 cars not asked for").arg(n - count);
        cutOff(n - count, atA);
        finish(goal, std::move(made));
        return intoSiding();
    }
    if (atLocation)
        return setOutBlock(goal, event, cars);
    WorkStep made;
    made.action = WorkStep::Cut;
    made.cars = int(count);
    made.atB = atB;
    made.what = QString("leave the %1 cars asked for where it stands").arg(count);
    cutOff(count, atB);
    finish(goal, std::move(made));
    return true;
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
    if (goal.planned && (goal.metres - direct > ImpracticalExtra || goal.reversals > ImpracticalReversals)) {
        goal.planned = false;
        goal.impractical = true;
        goal.why = QString("impractical: %1 km of moves (%2 km more than the way there), %3 reversals")
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
        goal.why = "not a wagon event";
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
    const auto fire = [&](int e) {
        const EventPlan &event = in.events[size_t(e)];
        fired[size_t(e)] = true;
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
    for (int guard = 0; guard < 1000 && !ended; ++guard) {
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
        const auto rank = [&](int e) {
            const EventPlan &event = in.events[size_t(e)];
            int kills = 0;
            for (int k : event.decrease)
                kills += std::find(active.begin(), active.end(), k) != active.end() ? 1 : 0;
            return (event.success ? 2 : 0) + (kills > 0 ? 1 : 0);
        };
        std::stable_sort(active.begin(), active.end(), [&](int a, int b) { return rank(a) < rank(b); });
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
        out.goals.push_back(chosenGoal);
        fire(chosen);
    }
    for (size_t e = 0; e < in.events.size(); ++e)
        if (!in.events[e].cars.empty() && !fired[e])
            out.notes << QString("%1: never active").arg(in.events[e].name);
    // Back to the path's end.
    const RouteTrack &lastPart = player->route.parts.back();
    const RoutePoint end = lastPart.locate(lastPart.length());
    Target back;
    back.kind = Target::Point;
    back.node = end.node;
    back.x = end.distance;
    back.forward = end.forward;
    const bool wasB = lastLeadB;
    const Move move = route(back);
    if (move.found) {
        out.back = move.metres;
        bool lead = wasB;
        for (const WorkLeg &leg : move.legs) {
            if (leg.leadB != lead)
                ++out.backReversals;
            lead = leg.leadB;
        }
    }
    out.train = train;
    out.cuts = cuts;
    return out;
}

}

double YardTrain::length() const {
    return lengthOf(track);
}

double WorkLeg::length() const {
    return lengthOf(way);
}

WorkPlan PlanWork(const SimulationInput &input) {
    Planner planner(input);
    return planner.plan();
}

}
