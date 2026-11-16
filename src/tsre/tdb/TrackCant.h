#pragma once

#include <QString>
#include <QStringList>
#include <limits>
#include <set>
#include <unordered_map>
#include <vector>

class TDB;
class Trk;

// Task tracks 16: track superelevation (cant), computed as Open Rails does
// (Orts.Simulation/Simulation/SuperElevation.cs and the standards of
// Orts.Formats.Msts/RouteFile.cs, Open Rails unstable 07ab0ce). Lengths in
// metres, speeds in m/s, angles in radians. The results live in the TDB
// (TDB::cant) and are never written to it.
namespace TrackCant {

constexpr float Infinity = std::numeric_limits<float>::infinity();

// A piecewise-linear table: x increasing, linear past its ends (Open Rails'
// Interpolator).
struct Table {
    std::vector<float> x;
    std::vector<float> y;
    bool empty() const { return x.size() < 2; }
    float at(float value) const;
};

// One ORTSSuperElevation block (Open Rails' SuperElevationStandard).
struct Standard {
    float maxFreightUnderbalance = Infinity;
    float maxPassengerUnderbalance = Infinity;
    float minCant = 0.010f;
    float maxCant = 0.180f;
    float minSpeed = 25.5f / 3.6f;
    float maxSpeed = Infinity;
    float precision = 0.005f;
    float runoffSlope = 0.003f;
    float runoffSpeed = 0.055f;
    // ORTSTrackSuperElevation's radius table gives the cant (no underbalance
    // given).
    bool legacy = true;
    // Made by Open Rails, not written in the route.
    bool isDefault = true;

    // The route has none: metric or imperial (its milepost units), high
    // speed above 45 m/s (its speed limit).
    static Standard makeDefault(bool metric, bool highSpeed);
    // The route has only ORTSTrackSuperElevation (radius -> cant).
    static Standard fromTable(const Table &radiusCant, bool metric, bool highSpeed);
    // A block as the .trk writes it ("ORTSSuperElevation ( ... )").
    static bool parse(const QString &block, Standard &out);
};

// The route's superelevation, from its .trk.
struct Route {
    // In the file's order (the first whose speeds fit is used); one made by
    // Open Rails when the file has none.
    std::vector<Standard> standards;
    // ORTSTrackSuperElevation: curve radius -> cant.
    Table radiusCant;
    // ORTSForceSuperElevation: -1 not given, 0 or 1.
    int forced = -1;
    // TrackGauge, else 1.435 m.
    float gauge = 1.435f;
    // SpeedLimit.
    float speedLimit = 44.444443f;
    bool metric = true;

    static Route fromTrk(const Trk &trk);
    // The .trk's blocks TSRE keeps as written (Trk::kept) and its values.
    static Route fromBlocks(const QStringList &blocks, float trackGauge, float speedLimit, bool metric);
};

// A vector section's superelevation (Open Rails' TrVectorSection fields).
struct Section {
    // Open Rails' NomElevM, m; negative: not computed (no curve).
    float nominal = -1.0f;
    // The speed limits in effect, the higher of both ways, at most the
    // route's (Open Rails' PassSpeedMpS, FreightSpeedMpS); -Infinity: not
    // known yet.
    float passengerSpeed = -Infinity;
    float freightSpeed = -Infinity;
    // Along the section, 0..1: the cant in metres (physics) and its angle,
    // signed by the curve's direction (visual, Open Rails' VisElevTable).
    // No points: none.
    unsigned char points = 0;
    float position[4] = {0, 0, 0, 0};
    float physical[4] = {0, 0, 0, 0};
    float visual[4] = {0, 0, 0, 0};
    // The curve run it belongs to, -1 none.
    int curve = -1;
    // The pivot of the profile its object is drawn with, from the centre
    // (OrtsProfileRoll::pivot, Open Rails' ElevOffsetM; Tables::pivots): cars
    // there rise with it (CarPlacement::liftOf). Not part of its cant.
    float pivot = 0.0f;

    float physicalAt(float fraction) const;
    float visualAt(float fraction) const;
    bool sameCant(const Section &other) const;
};

// A database's superelevation: each vector node's sections, in order.
struct Tables {
    std::unordered_map<int, std::vector<Section>> nodes;
    int curves = 0;
    const Section *section(int node, int index) const;

    // Each world object's vector sections (by its world tile and UiD as the
    // database writes them), in database order: shapes find their own.
    struct Owned {
        int node = 0;
        int index = 0;
    };
    std::unordered_map<unsigned long long, std::vector<Owned>> owners;
    const std::vector<Owned> *ownedBy(int tileX, int tileZ, unsigned int uid) const;
    void indexOwners(TDB &tdb);

    // World objects drawn without superelevation, decided as they load (a
    // DISABLED template, a profile without superelevation; Open Rails'
    // RemoveSuperElevation as its scenery loads): their sections have no
    // visual cant, kept when the database is computed again.
    std::set<unsigned long long> visualOff;
    // World objects' pivots, as they are drawn (setPivot); kept like
    // visualOff.
    std::unordered_map<unsigned long long, float> pivots;
    // Each speed post item as the last computation read it (by item id), so
    // that an edit reporting one unchanged (its object dragged) computes
    // nothing again: postsChanged.
    struct PostRead {
        int node = 0;
        float distance = 0.0f;
        // Taken for the speeds: a limit or resume post it could read.
        bool counted = false;
        bool forward = true;
        float passenger = -1.0f;
        float freight = -1.0f;
        bool operator==(const PostRead &o) const {
            return node == o.node && distance == o.distance && counted == o.counted && forward == o.forward
                    && passenger == o.passenger && freight == o.freight;
        }
    };
    std::unordered_map<int, PostRead> postsRead;
    // The route the tables were computed with, to compute them again.
    Route route;

    // The world objects whose superelevation changed, to be rebuilt (by
    // the world tile and UiD the database writes); filled by update().
    struct Owner {
        int tileX = 0;
        int tileZ = 0;
        unsigned int uid = 0;
        bool operator<(const Owner &other) const {
            return tileX != other.tileX ? tileX < other.tileX
                 : tileZ != other.tileZ ? tileZ < other.tileZ : uid < other.uid;
        }
    };
    std::set<Owner> rebuild;
    // The owners whose sections' cant differs from `before` (a whole
    // computation), added to rebuild.
    void queueChanged(TDB &tdb, const Tables &before);
};

// The cant of a world object's sections shown or not (its decision as it
// loads); the nodes computed again when that changes.
void setVisualOff(TDB &tdb, int tileX, int tileZ, unsigned int uid, bool off);
// The pivot of the profile a world object is drawn with (its sections'
// Section::pivot); nothing computed again.
void setPivot(TDB &tdb, int tileX, int tileZ, unsigned int uid, float pivot);

// The whole database, again.
void computeAll(TDB &tdb, const Route &route);
// After edits: the speeds of every node, then the cant of the nodes in
// `changed`, new nodes and the nodes whose speeds changed; deleted nodes
// dropped. The nodes computed again.
std::set<int> update(TDB &tdb, const Route &route, const std::set<int> &changed);
// Whether any of these track items, speed posts now or at the last
// computation, reads differently from then (moved, changed, added,
// deleted): only then can the speeds have changed.
bool postsChanged(TDB &tdb, const std::set<int> &items);

// The speed limit in effect at a place, as Open Rails finds it for the
// superelevation (SuperElevation.DetermineTrackSpeeds): the last speed post
// a train moving that way passed (default switch positions, no distance
// limit); warning posts not, a resume post as 999 m/s.
class SpeedScan {
public:
    // remember: filled with every speed post item as read (Tables::postsRead).
    explicit SpeedScan(TDB &tdb, std::unordered_map<int, Tables::PostRead> *remember = nullptr);
    // -Infinity: none.
    float at(int node, float distance, bool forward, bool freight);

private:
    struct Post {
        float distance = 0.0f;
        bool forward = true;
        float passenger = -1.0f;
        float freight = -1.0f;
    };
    TDB &tdb;
    std::unordered_map<int, std::vector<Post>> posts;
    std::unordered_map<long long, float> entries;
    float entry(int node, bool forward, bool freight);
    float last(int node, bool forward, bool freight) const;
    bool previous(int node, bool forward, int &previousNode, bool &previousForward) const;
};

// TSRE_CANT_DUMP=<file>: each section's speeds, nominal cant and tables
// (physical, visual), written after a whole computation (for the comparison
// with Open Rails). TSRE_CANT_RAW: nothing taken away from them (junctions,
// tunnels, objects drawn without), as Open Rails' are before its scenery
// loads.
void dumpIfAsked(TDB &tdb);

}
