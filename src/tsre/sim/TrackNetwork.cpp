#include <tsre/sim/TrackNetwork.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <tsre/Game.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalType.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/trains/PathTrack.h>

namespace Sim {

namespace {

// Closer than this, two places on a node are one.
constexpr float Same = 0.05f;
// Heads within this of each other (on one node, one way) are one signal.
constexpr float OneSignal = 0.1f;

SignalFunction functionOf(const QString &name, const SigCfg *config) {
    QString function = name.toUpper();
    if (config != nullptr) {
        // Open Rails' functions declare the MSTS function they act as.
        auto declared = config->signalFunctions.constFind(function.toLower());
        if (declared == config->signalFunctions.constEnd())
            declared = config->signalFunctions.constFind(function);
        if (declared != config->signalFunctions.constEnd())
            function = declared.value().toUpper();
    }
    if (function == "NORMAL") return SignalFunction::Normal;
    // MSTS's own files spell it DISTANCE.
    if (function == "DISTANT" || function == "DISTANCE") return SignalFunction::Distant;
    if (function == "REPEATER") return SignalFunction::Repeater;
    if (function == "SHUNTING") return SignalFunction::Shunting;
    if (function == "INFO") return SignalFunction::Info;
    if (function == "SPEED") return SignalFunction::Speed;
    return SignalFunction::Other;
}

SignalKind kindOf(const SignalType &type, const SigCfg *config, bool openRailsConfig) {
    SignalKind kind;
    kind.name = type.name;
    kind.function = functionOf(type.function, config);
    for (const SignalType::AspectEntry &entry : type.aspects) {
        SignalKind::Aspect aspect;
        aspect.aspect = int(entry.aspect);
        const SignalType::DrawState *state = type.drawState(entry.drawState);
        aspect.drawState = state != nullptr ? state->index : 0;
        aspect.speed = entry.speedMpS;
        aspect.noSpeedReduction = entry.noSpeedReduction;
        kind.aspects.push_back(aspect);
    }
    std::stable_sort(kind.aspects.begin(), kind.aspects.end(),
                     [](const SignalKind::Aspect &a, const SignalKind::Aspect &b) { return a.aspect < b.aspect; });
    for (const SignalType::DrawState &state : type.drawStates)
        if (!kind.drawStateNames.contains(state.index))
            kind.drawStateNames.insert(state.index, state.name);
    const SignalType::DrawState *fallback = type.defaultDrawState();
    kind.defaultDrawState = fallback != nullptr ? fallback->index : 0;
    // Open Rails: one value in the route root's file counts NORMAL heads; in
    // OpenRails/sigcfg.dat, or the first of two values, it counts signals.
    if (!type.numClearAhead.isEmpty())
        kind.clearAhead = type.numClearAhead.first();
    kind.countsSignals = openRailsConfig || type.numClearAhead.size() >= 2;
    return kind;
}

double wrap(double angle) {
    while (angle > M_PI)
        angle -= 2.0 * M_PI;
    while (angle < -M_PI)
        angle += 2.0 * M_PI;
    return angle;
}

}

TrackNetwork::TrackNetwork(TDB *tdb, const TrackGeometry &geometry, const SigCfg *config, bool openRailsConfig) {
    if (tdb == nullptr)
        return;
    // Signal types.
    QHash<QString, int> kindByName;
    if (config != nullptr)
        for (auto it = config->signalType.constBegin(); it != config->signalType.constEnd(); ++it) {
            if (it.value() == nullptr)
                continue;
            kindByName.insert(it.key().toLower(), int(types.size()));
            types.push_back(kindOf(*it.value(), config, openRailsConfig));
        }

    int maxNode = 0;
    for (const auto &entry : tdb->trackNodes)
        maxNode = std::max(maxNode, entry.first);
    nodes.resize(size_t(maxNode) + 1);
    headsOnNode.resize(nodes.size());
    postsOnNode.resize(nodes.size());
    crossingsOnNode.resize(nodes.size());
    junctions.assign(nodes.size(), -1);

    // Items on vector nodes: heads, speed posts, crossings.
    std::unordered_map<int, int> crossingOfItem;
    for (const auto &entry : tdb->trackNodes) {
        const int id = entry.first;
        const TRnode *n = entry.second;
        if (n == nullptr || n->typ != 1 || id <= 0)
            continue;
        nodes[size_t(id)].length = geometry.length(id);
        for (int i = 0; i < n->iTri; ++i) {
            auto found = tdb->trackItems.find(n->trItemRef[i]);
            if (found == tdb->trackItems.end() || found->second == nullptr)
                continue;
            TRitem *item = found->second;
            const float distance = item->getTrackPosition();
            if (item->type == "signalitem") {
                SignalHead head;
                head.item = int(item->trItemId);
                head.node = id;
                head.distance = distance;
                head.forward = item->trSignalType2 == 0;
                head.flags = item->trSignalType1;
                head.kind = kindByName.value(item->trSignalType4.toLower(), -1);
                if (head.kind < 0)
                    warnings << QString("signal item %1: no signal type %2 in sigcfg.dat").arg(head.item).arg(item->trSignalType4);
                if (item->trSignalDirs > 0 && item->trSignalDir != nullptr) {
                    head.junction = item->trSignalDir[0];
                    head.junctionPin = 1 + std::clamp(item->trSignalDir[2], 0, 1);
                }
                headsOnNode[size_t(id)].push_back({distance, int(signalHeads.size())});
                headItems.push_back({head.item, int(signalHeads.size())});
                signalHeads.push_back(head);
            } else if (item->type == "speedpostitem" && item->speedpostTrItemData != nullptr) {
                const TRitem::SType type = item->getSpeedpostType();
                if (type == TRitem::MILEPOST)
                    continue;
                SpeedPost post;
                post.item = int(item->trItemId);
                post.node = id;
                post.distance = distance;
                post.kind = type == TRitem::RESUME ? SpeedPost::Resume
                          : type == TRitem::WARNING ? SpeedPost::Warning : SpeedPost::Limit;
                const int flags = int(item->speedpostTrItemData[0]);
                const bool both = (flags >> 7) & 1;
                post.passenger = both || ((flags >> 5) & 1);
                post.freight = both || ((flags >> 6) & 1);
                const float value = item->getSpeedpostSpeed();
                post.speed = item->getSpeedPostSpeedUnitId() == 1 ? value * 0.44704f : value / 3.6f;
                // Facing: its angle against the track's direction there, as
                // Open Rails takes it. A post at the track's angle less a
                // quarter turn (as TSRE places a new one) faces trains going
                // against the node's way: warning posts come before their
                // limits that way on the stock routes.
                TrackPose pose;
                if (geometry.pose(id, distance, pose))
                    post.forward = std::fabs(wrap(item->getSpeedpostRot() - (pose.yaw - M_PI / 2.0))) > M_PI / 2.0;
                postsOnNode[size_t(id)].push_back({distance, int(posts.size())});
                posts.push_back(post);
            } else if (item->type == "crossoveritem" && item->crossoverTrItemData != nullptr) {
                // One crossing for the pair of items, a point on each track.
                const int other = item->crossoverTrItemData[0];
                int crossing = -1;
                auto known = crossingOfItem.find(other);
                if (known != crossingOfItem.end()) {
                    crossing = known->second;
                } else {
                    TrackSection section;
                    section.kind = TrackSection::Crossing;
                    crossing = int(all.size());
                    all.push_back(section);
                }
                crossingOfItem[int(item->trItemId)] = crossing;
                crossingsOnNode[size_t(id)].push_back({distance, crossing});
            }
        }
    }
    auto byDistance = [](const OnNode &a, const OnNode &b) {
        return a.distance < b.distance || (a.distance == b.distance && a.index < b.index);
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
        std::sort(headsOnNode[i].begin(), headsOnNode[i].end(), byDistance);
        std::sort(postsOnNode[i].begin(), postsOnNode[i].end(), byDistance);
        std::sort(crossingsOnNode[i].begin(), crossingsOnNode[i].end(), byDistance);
    }
    std::sort(headItems.begin(), headItems.end());

    // Track sections: each vector node cut at its NORMAL heads.
    for (size_t id = 0; id < nodes.size(); ++id) {
        Node &node = nodes[id];
        if (node.length <= 0.0f)
            continue;
        std::vector<float> cuts;
        for (const OnNode &on : headsOnNode[id]) {
            const SignalHead &head = signalHeads[size_t(on.index)];
            if (head.kind >= 0 && types[size_t(head.kind)].function == SignalFunction::Normal
                    && on.distance > Same && on.distance < node.length - Same
                    && (cuts.empty() || on.distance - cuts.back() > Same))
                cuts.push_back(on.distance);
        }
        node.first = int(all.size());
        float from = 0.0f;
        for (size_t c = 0; c <= cuts.size(); ++c) {
            TrackSection section;
            section.node = int(id);
            section.from = from;
            section.to = c < cuts.size() ? cuts[c] : node.length;
            all.push_back(section);
            from = section.to;
        }
        node.count = int(all.size()) - node.first;
        for (int s = 0; s < node.count; ++s) {
            TrackSection &section = all[size_t(node.first + s)];
            section.link[0] = s > 0 ? node.first + s - 1 : -1;
            section.link[1] = s + 1 < node.count ? node.first + s + 1 : -1;
        }
    }

    // Junctions: a section each, linked to the track sections at its pins.
    for (const auto &entry : tdb->trackNodes) {
        const int id = entry.first;
        const TRnode *n = entry.second;
        if (n == nullptr || n->typ != 2 || id <= 0 || size_t(id) >= nodes.size())
            continue;
        TrackSection section;
        section.kind = TrackSection::Junction;
        section.node = id;
        section.mainPin = std::max(1, PathTrack::junctionMainPin(tdb, n));
        junctions[size_t(id)] = int(all.size());
        all.push_back(section);
    }
    for (const auto &entry : tdb->trackNodes) {
        const int id = entry.first;
        const TRnode *n = entry.second;
        if (n == nullptr || n->typ != 1 || id <= 0)
            continue;
        Node &node = nodes[size_t(id)];
        if (node.count == 0)
            continue;
        for (int end = 0; end < 2; ++end) {
            const int at = n->pins[end].link;
            auto other = tdb->trackNodes.find(at);
            if (at <= 0 || size_t(at) >= nodes.size() || other == tdb->trackNodes.end() || other->second == nullptr
                    || other->second->typ != 2)
                continue;
            const int pin = PathTrack::junctionPin(other->second, id, end);
            const int junction = junctions[size_t(at)];
            if (pin < 0 || pin > 2 || junction < 0)
                continue;
            node.end[end] = junction;
            node.pin[end] = pin;
            const int track = end == 0 ? node.first : node.first + node.count - 1;
            all[size_t(track)].link[end] = junction;
            all[size_t(junction)].link[pin] = track;
        }
    }

    // Signals: heads at one place facing one way.
    std::vector<int> order(signalHeads.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = int(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        const SignalHead &x = signalHeads[size_t(a)], &y = signalHeads[size_t(b)];
        if (x.node != y.node) return x.node < y.node;
        if (x.forward != y.forward) return x.forward < y.forward;
        if (x.distance != y.distance) return x.distance < y.distance;
        return a < b;
    });
    for (int index : order) {
        SignalHead &head = signalHeads[size_t(index)];
        SignalGroup *last = groups.empty() ? nullptr : &groups.back();
        if (last == nullptr || last->node != head.node || last->forward != head.forward
                || std::fabs(head.distance - last->distance) > OneSignal) {
            SignalGroup group;
            group.node = head.node;
            group.distance = head.distance;
            group.forward = head.forward;
            groups.push_back(group);
            last = &groups.back();
        }
        head.signal = int(groups.size()) - 1;
        last->heads.push_back(index);
        last->normal = last->normal
                || (head.kind >= 0 && types[size_t(head.kind)].function == SignalFunction::Normal);
    }
}

int TrackNetwork::headOfItem(int item) const {
    auto it = std::lower_bound(headItems.begin(), headItems.end(), std::make_pair(item, -1));
    return it != headItems.end() && it->first == item ? it->second : -1;
}

std::pair<int, int> TrackNetwork::nodeSections(int node) const {
    if (node <= 0 || size_t(node) >= nodes.size())
        return {0, 0};
    return {nodes[size_t(node)].first, nodes[size_t(node)].count};
}

int TrackNetwork::sectionAt(int node, float distance, bool forward) const {
    const auto [first, count] = nodeSections(node);
    if (count == 0)
        return -1;
    // On a boundary: the section a train going that way enters.
    if (forward) {
        for (int s = 0; s < count; ++s)
            if (distance < all[size_t(first + s)].to - Same)
                return first + s;
        return first + count - 1;
    }
    for (int s = count - 1; s >= 0; --s)
        if (distance > all[size_t(first + s)].from + Same)
            return first + s;
    return first;
}

int TrackNetwork::junctionSection(int junctionNode) const {
    if (junctionNode <= 0 || size_t(junctionNode) >= junctions.size())
        return -1;
    return junctions[size_t(junctionNode)];
}

int TrackNetwork::endSection(int node, int end) const {
    if (node <= 0 || size_t(node) >= nodes.size() || end < 0 || end > 1)
        return -1;
    return nodes[size_t(node)].end[end];
}

int TrackNetwork::endPin(int node, int end) const {
    if (node <= 0 || size_t(node) >= nodes.size() || end < 0 || end > 1)
        return -1;
    return nodes[size_t(node)].pin[end];
}

Projection TrackNetwork::project(const RouteTrack &route) const {
    Projection projection;
    std::vector<int> seenHeads, seenPosts;
    const std::vector<RouteInterval> &intervals = route.intervals();
    const RouteInterval *previous = nullptr;
    for (const RouteInterval &interval : intervals) {
        if (interval.node <= 0 || size_t(interval.node) >= nodes.size())
            continue;
        const bool forward = interval.forward();
        const float low = std::min(interval.from, interval.to), high = std::max(interval.from, interval.to);
        auto routeAt = [&](float distance) { return interval.start + std::fabs(double(distance) - interval.from); };
        std::vector<ProjectedSection> here;
        // The junction between the previous interval and this one.
        if (previous != nullptr && previous->node != interval.node) {
            const int exitEnd = previous->forward() ? 1 : 0;
            const int junction = endSection(previous->node, exitEnd);
            if (junction >= 0 && all[size_t(junction)].kind == TrackSection::Junction) {
                ProjectedSection pass;
                pass.section = junction;
                pass.enter = pass.leave = interval.start;
                pass.pinIn = endPin(previous->node, exitEnd);
                pass.pinOut = endPin(interval.node, forward ? 0 : 1);
                here.push_back(pass);
            }
        }
        previous = &interval;
        if (high - low < 1e-4f)
            continue;
        // Its track sections, in the order passed.
        const auto [first, count] = nodeSections(interval.node);
        for (int k = 0; k < count; ++k) {
            const int s = forward ? first + k : first + count - 1 - k;
            const TrackSection &section = all[size_t(s)];
            if (section.to <= low + 1e-4f || section.from >= high - 1e-4f)
                continue;
            const float in = forward ? std::max(section.from, low) : std::min(section.to, high);
            const float out = forward ? std::min(section.to, high) : std::max(section.from, low);
            ProjectedSection pass;
            pass.section = s;
            pass.enter = routeAt(in);
            pass.leave = routeAt(out);
            pass.forward = forward;
            here.push_back(pass);
        }
        // Crossings on it.
        for (const OnNode &on : crossingsOnNode[size_t(interval.node)])
            if (on.distance >= low && on.distance <= high) {
                ProjectedSection pass;
                pass.section = on.index;
                pass.enter = pass.leave = routeAt(on.distance);
                pass.forward = forward;
                here.push_back(pass);
            }
        std::stable_sort(here.begin(), here.end(),
                         [](const ProjectedSection &a, const ProjectedSection &b) { return a.enter < b.enter; });
        for (const ProjectedSection &pass : here) {
            ProjectedSection *last = projection.sections.empty() ? nullptr : &projection.sections.back();
            // The same section going on (an interval cut where nothing is).
            if (last != nullptr && last->section == pass.section && all[size_t(pass.section)].kind == TrackSection::Track)
                last->leave = pass.leave;
            else
                projection.sections.push_back(pass);
        }
        // Heads and speed posts facing the train.
        for (const OnNode &on : headsOnNode[size_t(interval.node)])
            if (signalHeads[size_t(on.index)].forward == forward && on.distance >= low - Same && on.distance <= high + Same
                    && std::find(seenHeads.begin(), seenHeads.end(), on.index) == seenHeads.end()) {
                seenHeads.push_back(on.index);
                projection.heads.push_back({on.index, routeAt(on.distance)});
            }
        for (const OnNode &on : postsOnNode[size_t(interval.node)])
            if (posts[size_t(on.index)].forward == forward && on.distance >= low - Same && on.distance <= high + Same
                    && std::find(seenPosts.begin(), seenPosts.end(), on.index) == seenPosts.end()) {
                seenPosts.push_back(on.index);
                projection.speedPosts.push_back({on.index, routeAt(on.distance)});
            }
    }
    auto byAt = [](const ProjectedItem &a, const ProjectedItem &b) { return a.at < b.at; };
    std::stable_sort(projection.heads.begin(), projection.heads.end(), byAt);
    std::stable_sort(projection.speedPosts.begin(), projection.speedPosts.end(), byAt);
    // Stop signals: the facing heads' signals with NORMAL heads, each with
    // the first section of its block.
    for (const ProjectedItem &item : projection.heads) {
        const SignalHead &head = signalHeads[size_t(item.index)];
        if (head.kind < 0 || types[size_t(head.kind)].function != SignalFunction::Normal)
            continue;
        if (!projection.stopSignals.empty() && projection.stopSignals.back().signal == head.signal) {
            ++projection.stopSignals.back().normalHeads;
            continue;
        }
        ProjectedSignal signal;
        signal.signal = head.signal;
        signal.at = item.at;
        signal.normalHeads = 1;
        signal.kind = head.kind;
        signal.section = projection.sections.size();
        for (size_t k = 0; k < projection.sections.size(); ++k)
            if (projection.sections[k].enter >= item.at - Same) {
                signal.section = k;
                break;
            }
        projection.stopSignals.push_back(signal);
    }
    return projection;
}

int TrackNetwork::nextStopSignal(int node, float distance, bool forward, const std::vector<int> &switches,
                                 double reach) const {
    double travelled = 0.0;
    float from = distance;
    for (int hops = 0; hops < 256 && travelled <= reach; ++hops) {
        if (node <= 0 || size_t(node) >= nodes.size() || nodes[size_t(node)].length < 0.0f)
            return -1;
        const std::vector<OnNode> &on = headsOnNode[size_t(node)];
        if (forward) {
            for (const OnNode &o : on)
                if (o.distance > from + Same && signalHeads[size_t(o.index)].forward && isNormal(signalHeads[size_t(o.index)]))
                    return signalHeads[size_t(o.index)].signal;
        } else {
            for (auto o = on.rbegin(); o != on.rend(); ++o)
                if (o->distance < from - Same && !signalHeads[size_t(o->index)].forward
                        && isNormal(signalHeads[size_t(o->index)]))
                    return signalHeads[size_t(o->index)].signal;
        }
        const float length = nodes[size_t(node)].length;
        travelled += forward ? length - std::max(0.0f, from) : std::min(length, from);
        // Through the junction at the node's end, as it is set (a facing
        // junction by its switch, a trailing one to its input).
        const int end = forward ? 1 : 0;
        const int junction = nodes[size_t(node)].end[end];
        if (junction < 0)
            return -1;
        const int in = nodes[size_t(node)].pin[end];
        const TrackSection &j = all[size_t(junction)];
        const int out = in == 0 ? (size_t(junction) < switches.size() && switches[size_t(junction)] > 0
                                       ? switches[size_t(junction)] : j.mainPin)
                                : 0;
        const int track = out >= 0 && out <= 2 ? j.link[out] : -1;
        if (track < 0)
            return -1;
        const TrackSection &next = all[size_t(track)];
        node = next.node;
        forward = next.link[0] == junction && track == nodes[size_t(node)].first;
        from = forward ? -1.0f : nodes[size_t(node)].length + 1.0f;
    }
    return -1;
}

size_t TrackNetwork::memory() const {
    size_t bytes = nodes.capacity() * sizeof(Node) + all.capacity() * sizeof(TrackSection)
            + junctions.capacity() * sizeof(int) + signalHeads.capacity() * sizeof(SignalHead)
            + posts.capacity() * sizeof(SpeedPost) + headItems.capacity() * sizeof(std::pair<int, int>);
    for (const SignalGroup &group : groups)
        bytes += sizeof(SignalGroup) + group.heads.capacity() * sizeof(int);
    for (const SignalKind &kind : types)
        bytes += sizeof(SignalKind) + kind.aspects.capacity() * sizeof(SignalKind::Aspect);
    for (const auto *list : {&headsOnNode, &postsOnNode, &crossingsOnNode}) {
        bytes += list->capacity() * sizeof(std::vector<OnNode>);
        for (const std::vector<OnNode> &on : *list)
            bytes += on.capacity() * sizeof(OnNode);
    }
    return bytes;
}

std::shared_ptr<const SigCfg> SimulationSigCfg(TDB *tdb, bool *openRails) {
    if (openRails != nullptr)
        *openRails = false;
    // The route's OpenRails folder first (any letter case).
    const QDir route(Game::root + "/ROUTES/" + Game::route);
    for (const QFileInfo &folder : route.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (folder.fileName().compare("openrails", Qt::CaseInsensitive) != 0)
            continue;
        for (const QFileInfo &file : QDir(folder.filePath()).entryInfoList(QDir::Files))
            if (file.fileName().compare("sigcfg.dat", Qt::CaseInsensitive) == 0) {
                auto config = std::make_shared<const SigCfg>(file.filePath());
                if (config->loaded) {
                    if (openRails != nullptr)
                        *openRails = true;
                    return config;
                }
            }
    }
    if (tdb != nullptr && tdb->sigCfg != nullptr)
        return std::shared_ptr<const SigCfg>(tdb->sigCfg, [](const SigCfg *) {});
    return nullptr;
}

}
