#include <tsre/sim/TrainRoute.h>

#include <algorithm>
#include <cmath>

namespace Sim {

double RouteInterval::length() const {
    return std::fabs(double(to) - double(from));
}

void RouteTrack::append(int node, float from, float to) {
    if (!parts.empty()) {
        RouteInterval &last = parts.back();
        const bool forward = to >= from;
        if (last.node == node && std::fabs(last.to - from) < 1e-3f
                && (last.length() == 0.0 || last.forward() == forward || from == to)) {
            if (from != to && last.length() == 0.0)
                last.from = from;
            last.to = to;
            return;
        }
    }
    RouteInterval interval;
    interval.node = node;
    interval.from = from;
    interval.to = to;
    interval.start = length();
    parts.push_back(interval);
}

double RouteTrack::length() const {
    return parts.empty() ? 0.0 : parts.back().start + parts.back().length();
}

RoutePoint RouteTrack::locate(double distance) const {
    RoutePoint point;
    if (parts.empty())
        return point;
    distance = std::clamp(distance, 0.0, length());
    auto it = std::upper_bound(parts.begin(), parts.end(), distance,
                               [](double d, const RouteInterval &i) { return d < i.start; });
    const RouteInterval &interval = it == parts.begin() ? parts.front() : *(it - 1);
    const double along = std::min(distance - interval.start, interval.length());
    point.node = interval.node;
    point.forward = interval.forward();
    point.distance = float(point.forward ? interval.from + along : interval.from - along);
    return point;
}

bool TrainRoute::place(double pathDistance, int &part, double &distance) const {
    for (size_t i = 0; i < parts.size(); ++i) {
        const double begin = partPathStart[i];
        const double offset = i == 0 ? pathStart : 0.0;
        const double end = begin + parts[i].length() - offset;
        if (pathDistance >= begin - 1e-3 && pathDistance <= end + 1e-3) {
            part = int(i);
            distance = std::clamp(pathDistance - begin + offset, 0.0, parts[i].length());
            return true;
        }
    }
    return false;
}

TrainRoute BuildRoute(TDB *tdb, const QVector<PathTrack::Node> &nodes,
                      const QVector<PathTrack::Leg> &legs, double leadIn) {
    TrainRoute route;
    if (nodes.isEmpty() || legs.isEmpty())
        return route;
    route.parts.emplace_back();
    route.partPathStart.push_back(0.0);
    // The lead-in: the track behind the start, walked back and laid in the
    // order the train runs it.
    const PathTrack::Leg &first = legs.first();
    if (leadIn > 0.0 && !first.broken) {
        bool ended = false;
        const QVector<PathTrack::Leg> behind = PathTrack::follow(tdb, first.track, first.from, !first.forward(),
                                                                 float(leadIn), &ended);
        for (int i = behind.size() - 1; i >= 0; --i)
            route.parts.back().append(behind[i].track, behind[i].to, behind[i].from);
        route.leadInEnded = ended;
        route.pathStart = route.parts.back().length();
    }
    double pathDistance = 0.0;
    for (int k = 0; k < legs.size(); ++k) {
        const PathTrack::Leg &leg = legs[k];
        if (leg.broken) {
            route.broken = true;
            break;
        }
        route.parts.back().append(leg.track, leg.from, leg.to);
        pathDistance += std::fabs(double(leg.to) - double(leg.from));
        // A reversal at the leg's end: the next part runs the other way.
        const bool reversal = k + 1 < nodes.size() && nodes[k + 1].type == PathTrack::NodeType::Reverse;
        if (reversal && k + 1 < legs.size()) {
            route.parts.emplace_back();
            route.partPathStart.push_back(pathDistance);
        }
    }
    return route;
}

}
