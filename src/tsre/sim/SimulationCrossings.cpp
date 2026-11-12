// Level crossings in the simulation core (task traffic 01): which crossings
// trains hold closed, by Open Rails' rule (LevelCrossing.cs UpdateCrossings)
// on each train's route part, and standing trains beside them.
#include <tsre/sim/SimulationCore.h>
#include <QDebug>
#include <QTime>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/sim/TrackNetwork.h>

namespace Sim {

namespace {

// Open Rails: below this a train stands (Simulator.MaxStoppedMpS); at or
// below SlowSpeed it closes a crossing only within SlowReach more than the
// minimum distance (minCrossingActivationSpeed, its "65 m"); going back
// slowly, it lets go once its front is LetGo clear.
constexpr double StandingSpeed = 0.1;
constexpr double SlowSpeed = 5.0;
constexpr double SlowReach = 65.0;
constexpr double LetGo = 9.5;

// TSRE_CROSSING_LOG=1: a line each time a crossing closes or opens.
bool crossingLog() {
    static const bool on = qEnvironmentVariableIntValue("TSRE_CROSSING_LOG") > 0;
    return on;
}

bool listHas(const std::vector<int> &list, int crossing) {
    return std::find(list.begin(), list.end(), crossing) != list.end();
}

// Open Rails' distance a standing car may be from a crossing without
// closing it (the minimum distance less 13.5 m, or less 6.5 m under 20 m).
double standingReach(float minimum) {
    return std::max(0.0, minimum >= 20.0f ? double(minimum) - 13.5 : double(minimum) - 6.5);
}

}

void SimulationCore::setCrossingParameters(const std::vector<CrossingParameters> &list) {
    if (in->network == nullptr || crossingClosed.empty())
        return;
    bool changed = false;
    for (const CrossingParameters &p : list) {
        const int c = in->network->levelCrossingOfItem(p.item);
        if (c < 0)
            continue;
        const float warning = std::max(0.0f, p.warning), minimum = std::max(0.0f, p.minimum);
        if (crossingWarning[size_t(c)] == warning && crossingMinimum[size_t(c)] == minimum)
            continue;
        crossingWarning[size_t(c)] = warning;
        crossingMinimum[size_t(c)] = minimum;
        changed = true;
    }
    if (!changed)
        return;
    crossingWarningMost = CrossingWarning;
    crossingMinimumMost = CrossingMinimum;
    for (size_t c = 0; c < crossingClosed.size(); ++c) {
        crossingWarningMost = std::max(crossingWarningMost, crossingWarning[c]);
        crossingMinimumMost = std::max(crossingMinimumMost, crossingMinimum[c]);
    }
    // Standing trains looked at again, and every train now.
    crossingStandingAt = ~0ull;
    crossingChecks.clear();
    crossingNext = -1.0;
    stepLevelCrossings();
}

double SimulationCore::allowedSpeedOf(int train) const {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    double limit = std::min(double(in->routeSpeedLimit), double(topSpeedOf(train, now)));
    if (s.signalSpeed > 0.0f)
        limit = std::min(limit, double(s.signalSpeed));
    if (s.part >= 0 && size_t(s.part) < rt.limits.size()) {
        const std::vector<SpeedLimit> &limits = rt.limits[size_t(s.part)];
        for (size_t k = std::min(rt.limit, limits.size()); k < limits.size() && limits[k].from <= s.front; ++k)
            if (limits[k].to > s.front)
                limit = std::min(limit, double(limits[k].speed));
    }
    return limit;
}

void SimulationCore::crossingsHeldBy(int train, const std::vector<int> &before, std::vector<int> &out,
                                     double &clear) const {
    out.clear();
    clear = std::numeric_limits<double>::infinity();
    const TrainState &s = states[size_t(train)];
    if (s.status == TrainStatus::Waiting || s.status == TrainStatus::Finished)
        return;
    const Runtime &rt = runtime[size_t(train)];
    if (s.part < 0 || size_t(s.part) >= rt.ways.size())
        return;
    const std::vector<ProjectedItem> &list = rt.ways[size_t(s.part)].levelCrossings;
    if (list.empty())
        return;
    const double v = s.speed;
    const double speed = std::fabs(v);
    const double length = rt.length;
    // How far around the train any crossing may be held: the longest
    // reach (at most the route's speed limit allowed), ahead of the front
    // and behind the rear. Most steps most trains have none there.
    const double most = double(crossingWarningMost) * speed
            + std::max(0.0, double(crossingWarningMost) * (double(in->routeSpeedLimit) - speed) / 2.0)
            + double(crossingMinimumMost) + 1.0 + SlowReach;
    // The longest it can be at any speed the train may run at: how far it
    // goes before the next crossing ahead comes within it.
    const double fastest = std::max({double(in->trains[size_t(train)].maxSpeed), double(in->routeSpeedLimit), speed});
    const double longest = double(crossingWarningMost) * fastest + double(crossingMinimumMost) + 1.0 + SlowReach;
    // Behind its rear: within the minimum distance going forward (or
    // standing); its whole reach going back. Ahead: as far as any it holds
    // may be (its reach was longer when it took it).
    const double behind = v < 0.0 ? most : double(crossingMinimumMost) + 1.0;
    const double ahead = before.empty() ? most : std::max(most, longest);
    auto it = std::lower_bound(list.begin(), list.end(), s.front - length - behind,
                               [](const ProjectedItem &item, double at) { return item.at < at; });
    if (it == list.end() || it->at > s.front + ahead) {
        clear = it == list.end() ? std::numeric_limits<double>::infinity()
                                 : std::max(0.0, it->at - s.front - longest);
        return;
    }
    // One within reach: looked at every step.
    clear = 0.0;
    const double allowed = allowedSpeedOf(train);
    // Open Rails' ReservedTrackLengthM: the track reserved ahead of the
    // front (none: no limit).
    const double reserved = s.authority - s.front;
    const auto reservedOk = [reserved](double distance) { return reserved <= 0.0 || distance < reserved; };
    for (; it != list.end() && it->at <= s.front + ahead; ++it) {
        const int c = it->index;
        if (c < 0 || size_t(c) >= crossingClosed.size() || listHas(out, c))
            continue;
        const double warning = crossingWarning[size_t(c)];
        const double minimum = crossingMinimum[size_t(c)];
        // Open Rails' reach: where the train would be in the warning time,
        // or (if it may still speed up) half way to its allowed speed.
        const double reach = warning * speed + std::max(0.0, warning * (allowed - speed) / 2.0) + minimum + 1.0;
        // Ahead of the front (+) or behind it (-); behind the rear (+).
        const double frontDist = it->at - s.front;
        const bool was = listHas(before, c);
        if ((frontDist > reach && !was) || frontDist < -(reach + length))
            continue;
        const double rearDist = -frontDist - length;
        bool held = false;
        if (speed <= StandingSpeed) {
            // Standing: on it or within the minimum distance (a train
            // standing further back lets it open: Open Rails' timeout).
            held = frontDist <= minimum && rearDist <= minimum && reservedOk(frontDist);
        } else if (v > 0.0 && speed <= SlowSpeed) {
            // Slowly forward: closes only close by; one held stays held.
            if ((frontDist <= reach || was) && reservedOk(frontDist) && rearDist <= minimum)
                held = was || frontDist <= minimum + SlowReach;
        } else if (v < 0.0 && speed <= SlowSpeed) {
            // Slowly back: the rear closes it close by; the front lets go
            // once clear of it.
            if (rearDist <= reach && reservedOk(rearDist) && frontDist <= minimum)
                held = frontDist <= LetGo && (was || rearDist <= minimum + SlowReach);
        } else if (v < 0.0) {
            held = rearDist <= reach && reservedOk(rearDist) && frontDist <= minimum;
        } else {
            // Running on towards it, one held stays held: Open Rails lets it
            // go when the train's allowed speed drops and its reach with it,
            // and closes it again further on (the barriers go up and down).
            held = (frontDist <= reach || (was && frontDist > 0.0)) && reservedOk(frontDist) && rearDist <= minimum;
        }
        if (held)
            out.push_back(c);
    }
}

void SimulationCore::standingCrossings() {
    std::fill(crossingStanding.begin(), crossingStanding.end(), 0);
    if (in->network == nullptr)
        return;
    const double around = standingReach(crossingMinimumMost);
    std::vector<int> near;
    for (const Standing &st : standing) {
        if (st.gone)
            continue;
        for (const RouteInterval &interval : st.track) {
            const double low = std::min(interval.from, interval.to), high = std::max(interval.from, interval.to);
            near.clear();
            in->network->levelCrossingsOn(interval.node, float(low - around), float(high + around), near);
            for (int c : near) {
                const double at = in->network->levelCrossings()[size_t(c)].distance;
                const double apart = std::max({0.0, low - at, at - high});
                if (apart <= standingReach(crossingMinimum[size_t(c)]))
                    crossingStanding[size_t(c)] = 1;
            }
        }
    }
}

void SimulationCore::stepLevelCrossings() {
    if (crossingClosed.empty() || now < crossingNext - 1e-9)
        return;
    crossingNext = now + CrossingEvery;
    crossingTouched.clear();
    bool all = false;
    if (crossingStandingAt != standingChanges) {
        standingCrossings();
        crossingStandingAt = standingChanges;
        all = true;
    }
    // The train that last took or let go of each crossing touched, for the
    // log.
    std::vector<std::pair<int, int>> by;
    if (crossingChecks.size() != states.size())
        crossingChecks.assign(states.size(), CrossingCheck());
    for (size_t i = 0; i < states.size(); ++i) {
        const TrainState &s = states[i];
        CrossingCheck &check = crossingChecks[i];
        // Nothing within its reach until it has gone so far (a driven train
        // may run any way: looked at every step).
        if (!check.holding && check.untilTravelled >= 0.0 && s.travelled < check.untilTravelled
                && s.part == check.part && s.status == check.status && int(i) != driven)
            continue;
        Runtime &rt = runtime[i];
        double clear = 0.0;
        crossingsHeldBy(int(i), rt.crossingsHeld, crossingScratch, clear);
        check.part = s.part;
        check.status = s.status;
        check.holding = !crossingScratch.empty();
        check.untilTravelled = check.holding || clear <= 0.0 ? -1.0 : s.travelled + clear;
        if (crossingScratch == rt.crossingsHeld)
            continue;
        for (int c : rt.crossingsHeld)
            if (!listHas(crossingScratch, c)) {
                --crossingCount[size_t(c)];
                crossingTouched.push_back(c);
                if (crossingLog())
                    by.push_back({c, int(i)});
            }
        for (int c : crossingScratch)
            if (!listHas(rt.crossingsHeld, c)) {
                ++crossingCount[size_t(c)];
                crossingTouched.push_back(c);
                if (crossingLog())
                    by.push_back({c, int(i)});
            }
        rt.crossingsHeld.swap(crossingScratch);
    }
    const auto settle = [&](int c) {
        const unsigned char closed = crossingCount[size_t(c)] > 0 || crossingStanding[size_t(c)] ? 1 : 0;
        if (closed == crossingClosed[size_t(c)])
            return;
        crossingClosed[size_t(c)] = closed;
        ++crossingChanges;
        if (!crossingLog())
            return;
        int train = -1;
        for (const auto &entry : by)
            if (entry.first == c)
                train = entry.second;
        const LevelCrossing &crossing = in->network->levelCrossings()[size_t(c)];
        QString who = QStringLiteral("a standing train");
        if (train >= 0) {
            const TrainState &s = states[size_t(train)];
            const Runtime &rt = runtime[size_t(train)];
            double at = 0.0;
            if (s.part >= 0 && size_t(s.part) < rt.ways.size())
                for (const ProjectedItem &item : rt.ways[size_t(s.part)].levelCrossings)
                    if (item.index == c)
                        at = item.at;
            who = QString("train %1 %2 (front %3 m from it, %4 m/s)")
                    .arg(train).arg(in->trains[size_t(train)].service)
                    .arg(at - s.front, 0, 'f', 1).arg(double(s.speed), 0, 'f', 1);
        }
        qInfo().noquote() << QString("crossing %1 %2 at %3 by %4")
                .arg(crossing.item).arg(closed ? "closed" : "opened")
                .arg(QTime(0, 0).addMSecs(int(std::fmod(now, 86400.0) * 1000.0)).toString("hh:mm:ss.z"))
                .arg(who);
    };
    if (all) {
        for (size_t c = 0; c < crossingClosed.size(); ++c)
            settle(int(c));
    } else {
        for (int c : crossingTouched)
            settle(c);
    }
}

}
