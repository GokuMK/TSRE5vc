#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class SigCfg;
class TDB;

namespace Sim {

class TrackGeometry;
class RouteTrack;

// What a signal type does (sigcfg.dat's SignalFnType; Open Rails' own
// functions by the MSTS function they declare).
enum class SignalFunction { Normal, Distant, Repeater, Shunting, Info, Speed, Other };

// A signal type as the simulation needs it.
struct SignalKind {
    QString name;
    SignalFunction function = SignalFunction::Normal;
    // Its aspects from the most restrictive: the aspect (SignalType::Aspect),
    // its draw state's index, its speed (m/s; negative none).
    struct Aspect {
        int aspect = 0;
        int drawState = 0;
        float speed = -1.0f;
        bool noSpeedReduction = false;
    };
    std::vector<Aspect> aspects;
    int defaultDrawState = 0;
    // How far ahead a train clears from a signal of this type: so many NORMAL
    // heads (Open Rails' legacy mode), or so many signals (Open Rails mode:
    // OpenRails/sigcfg.dat, or the first of two values), where 0 clears
    // nothing beyond it and -1 does not count it.
    int clearAhead = 1;
    bool countsSignals = false;
};

// A signal head: a TDB signal item.
struct SignalHead {
    int item = -1;
    int node = -1;
    float distance = 0.0f;
    // It faces trains moving the node's way (increasing distance).
    bool forward = true;
    // In the network's kinds.
    int kind = -1;
    // TrSignalType's flags: bit 0 a junction link; bits 4 to 9 number plate,
    // gradient plate, USER1 to USER4 (sig_feature).
    unsigned int flags = 0;
    // The junction node it is linked to and the output pin its route takes
    // there (route_set); -1 none.
    int junction = -1;
    int junctionPin = -1;
    // The signal it belongs to.
    int signal = -1;
};

// Heads at one place facing one way, as one signal: same node, within
// 0.1 m, the same direction (MSTS's own grouping tolerance).
struct SignalGroup {
    std::vector<int> heads;
    int node = -1;
    float distance = 0.0f;
    bool forward = true;
    // It has a NORMAL head: a block boundary for trains it faces.
    bool normal = false;
};

// A speed post item (mileposts left out).
struct SpeedPost {
    enum Kind { Limit, Warning, Resume };
    int item = -1;
    int node = -1;
    float distance = 0.0f;
    // It faces trains moving the node's way, from its angle against the
    // track's direction there (Open Rails' rule).
    bool forward = true;
    Kind kind = Limit;
    bool passenger = true;
    bool freight = true;
    // m/s.
    float speed = 0.0f;
};

// A piece of track trains occupy and reserve whole: a stretch of a vector
// node between NORMAL signals, crossings and the node's ends; a junction (a
// point: a train holds it while it spans it); a diamond crossing (a point on
// each of its two tracks).
struct TrackSection {
    enum Kind : unsigned char { Track, Junction, Crossing };
    Kind kind = Track;
    // Track: its vector node; Junction: its junction node.
    int node = -1;
    // Track: distances along the node.
    float from = 0.0f;
    float to = 0.0f;
    // Track: the sections at its from and to ends (-1 a track end);
    // Junction: the track sections at its pins (0 the input, 1 and 2 the
    // outputs).
    int link[3] = {-1, -1, -1};
    // Junction: the output pin of its main route.
    int mainPin = 1;
    float length() const { return to - from; }
};

// A route's way through the network: the sections it passes, the heads and
// speed posts facing it, with their route distances.
struct ProjectedSection {
    int section = -1;
    // Where it enters and leaves it (equal for a junction or crossing).
    double enter = 0.0;
    double leave = 0.0;
    // Track: it runs the node's way. Junction: the pins it comes in and goes
    // out by.
    bool forward = true;
    int pinIn = -1;
    int pinOut = -1;
};
struct ProjectedItem {
    int index = -1;
    double at = 0.0;
};
// A stop signal facing the route: a signal with NORMAL heads facing it.
struct ProjectedSignal {
    int signal = -1;
    double at = 0.0;
    // Its NORMAL heads facing the train (legacy clearing ahead counts them).
    int normalHeads = 0;
    // The kind of its first NORMAL head (how far to clear ahead).
    int kind = -1;
    // The first projected section of its block (past it).
    size_t section = 0;
};
struct Projection {
    std::vector<ProjectedSection> sections;
    std::vector<ProjectedItem> heads;
    std::vector<ProjectedItem> speedPosts;
    // In the order passed.
    std::vector<ProjectedSignal> stopSignals;
};

// The track's topology and its signal data, made from the TDB and
// sigcfg.dat when a simulation starts; immutable after (task simulation 02).
class TrackNetwork {
public:
    // `geometry` gives the vector nodes' lengths and directions (speed posts'
    // facing). `openRailsConfig`: `config` is an OpenRails/sigcfg.dat.
    TrackNetwork(TDB *tdb, const TrackGeometry &geometry, const SigCfg *config, bool openRailsConfig);

    const std::vector<TrackSection> &sections() const { return all; }
    const std::vector<SignalKind> &kinds() const { return types; }
    const std::vector<SignalHead> &heads() const { return signalHeads; }
    const std::vector<SignalGroup> &signalGroups() const { return groups; }
    const std::vector<SpeedPost> &speedPosts() const { return posts; }
    // The head of a TDB item, -1 none.
    int headOfItem(int item) const;
    // The track sections of a vector node, in distance order: first, count.
    std::pair<int, int> nodeSections(int node) const;
    // The track section of a node at a distance, the one after it for a
    // train going `forward` when it is on a boundary; -1 none.
    int sectionAt(int node, float distance, bool forward) const;
    // The junction section of a junction node, -1 none.
    int junctionSection(int junctionNode) const;
    // The section at a vector node's end (0 at distance 0, 1 the far one): a
    // junction section, or -1 a track end; and the junction pin there.
    int endSection(int node, int end) const;
    int endPin(int node, int end) const;
    // A route part's way through the network.
    Projection project(const RouteTrack &route) const;
    QStringList warnings;
    size_t memory() const;

private:
    struct Node {
        int first = 0;
        int count = 0;
        float length = -1.0f;
        int end[2] = {-1, -1};
        int pin[2] = {-1, -1};
    };
    std::vector<Node> nodes;
    std::vector<TrackSection> all;
    std::vector<int> junctions;
    std::vector<SignalKind> types;
    std::vector<SignalHead> signalHeads;
    std::vector<SignalGroup> groups;
    std::vector<SpeedPost> posts;
    // Per vector node: heads, speed posts and crossings on it, by distance.
    struct OnNode {
        float distance;
        int index;
    };
    std::vector<std::vector<OnNode>> headsOnNode;
    std::vector<std::vector<OnNode>> postsOnNode;
    std::vector<std::vector<OnNode>> crossingsOnNode;
    std::vector<std::pair<int, int>> headItems;
};

// The sigcfg.dat the simulation reads for the current route: the route's
// OpenRails/sigcfg.dat when there is one (as Open Rails does), else the
// TDB's. `openRails` tells which.
std::shared_ptr<const SigCfg> SimulationSigCfg(TDB *tdb, bool *openRails);

}
