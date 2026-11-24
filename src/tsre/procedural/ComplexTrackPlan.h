/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef COMPLEXTRACKPLAN_H
#define COMPLEXTRACKPLAN_H

#include <QPair>
#include <QStringList>
#include <QVector>
#include <tsre/procedural/TrackShapeAnalysis.h>

// Tracks task 17, phase 1: what a profile family's COMPLEX member lays on a
// shape's complex groups, in shape space (MSTS: +x right, +z forward,
// headings positive to the right): ties and bearers (design section 5),
// fasteners where rails cross them, the rail lines with the stretches where
// a rail lies on another's left out (section 7, decision 22), conductor and
// rack rails cut at other tracks (decision 24), the bed's cross-sections
// along the reference line (section 6). The port of
// extra/tsection-review/tieplan.py.
class ComplexTrackPlan {
public:
    using Point = TrackShapeAnalysis::Point;
    using Line = QVector<Point>;
    // Distances along a line, [from, to].
    using Range = QPair<float, float>;

    // A SwitchDrive or SwitchIndicator template as the plan places it
    // (step 7, design section 2): its Offset (x out from the track centre,
    // its sign the default side, + the diverging side; z as any
    // template's, -z forward from the toe), Spacing between drives along
    // the blades (0: one), and its mesh's extents (template space, as
    // modelled standing right of the track).
    struct SwitchPart {
        float offset[3] = {0, 0, 0};
        float spacing = 0;
        float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    };

    struct Settings {
        // The gauge line: half of it each side of a path.
        float gauge = 1.435f;
        // The rail's centre outside the gauge line (half the head; the Rail
        // part's Offset x).
        float railOffset = 0.036f;
        // The Tie mesh's x and z extents.
        float tieLength = 2.6f;
        float tieWidth = 0.26f;
        float tieSpacing = 0.6f;
        float tiePhase = 0.3f;
        float tieGap = 0.1f;
        float maxBearerLength = 4.8f;
        float maxSharedAngle = 60.0f;
        float conductorClearance = 0.2f;
        // Phase 2 (design section 7): the flangeway beside noses, wing and
        // check rails; an open blade's tip from its stock rail; a check
        // rail's length.
        float flangeway = 0.045f;
        float bladeOpening = 0.16f;
        float checkRailLength = 4.8f;
        // Where tracks part, each its own ties where they fit (decision 4).
        bool ownTies = true;
        // Steep crossings: the first track's ties lengthened under the
        // other track's rails, within maxBearerLength.
        bool steepLengthen = true;
        // Step 7 (decisions 36, 38), from the filter: the switches are
        // operated by hand (their drives: the SwitchDrive templates
        // marked HandOperated), their parts on the other side.
        bool handOperated = false;
        bool switchSideFlip = false;
        // The drives (unmarked SwitchDrive templates) and the hand-operated
        // ones (marked) of the nearest LOD having any: the ties under them
        // are lengthened to reach under them (decision 36).
        QVector<SwitchPart> drives;
        QVector<SwitchPart> handDrives;
    };

    // Which parts of a path the plan lays (TrackProfileFilters::Part bits):
    // a shape's own paths by its filter, included shapes' by
    // IncludedTrackShapeParts.
    struct Path {
        Line line;
        unsigned parts = ~0u;
        // The shape's MainRoute: its switch blades lie closed.
        bool main = false;
    };

    struct Tie {
        enum class Kind {
            // A track's own tie, square to it.
            Own,
            // One track alone on a station, square to the reference line.
            Square,
            // Shared by two or more tracks, or lengthened under another
            // track's rails (steep crossings).
            Bearer
        };
        Kind kind = Kind::Own;
        float centre[2] = {0, 0};
        // The track direction the tie lies square to: its axis runs to
        // (cos h, -sin h).
        float heading = 0;
        float length = 0;
        float width = 0;
        QVector<int> paths;
    };

    struct Fastener {
        float position[2] = {0, 0};
        // The rail's heading there.
        float heading = 0;
        int path = -1;
        // -1 left rail, 1 right.
        int side = 0;
        int tie = -1;
    };

    // A rail (or conductor, rack) line of one path: its centre line and the
    // stretches drawn.
    struct RailLine {
        int path = -1;
        int side = 0;
        // Signed distance from the path's centre line, right positive.
        float offset = 0;
        Line line;
        QVector<Range> drawn;
    };

    // A stretch of rail to sweep (phase 2, design section 7): along its own
    // line from `from` to `to`.
    struct RailPiece {
        enum class Kind {
            // A running rail, a stock rail, a nose's leg.
            Plain,
            // A switch blade: near the toe beside its stock rail (closed,
            // or open by bladeOpening), its section narrowed to the tip.
            Blade,
            // A frog's wing rail: from the knuckle beside the nose, flared.
            Wing,
            // Opposite a nose, inside the gauge by the flangeway, flared.
            CheckRail
        };
        Kind kind = Kind::Plain;
        int path = -1;
        int side = 0;
        Line line;
        // The section's width factor at each point of line (blades); empty:
        // the whole section.
        QVector<float> scale;
        float from = 0;
        float to = 0;
    };

    // Where two paths' rails cross (phase 2): a common crossing (a frog:
    // nose, wing rails, check rails) or an obtuse one (a diamond's middle:
    // a flangeway gap in each rail for now).
    struct Crossing {
        float position[2] = {0, 0};
        // Degrees between the rails.
        float angle = 0;
        bool obtuse = false;
        int railA = -1;
        int railB = -1;
    };

    // One cross-section of a bed: at a point of its line, across to the
    // right of heading; the outermost tracks' centres at left and right
    // (signed distances along the cross-section).
    struct BedSlice {
        float position[2] = {0, 0};
        float heading = 0;
        float left = 0;
        float right = 0;
        // Along the bed's line, for texture coordinates.
        float distance = 0;
        // Every track across it (design section 6): the signed distances
        // of their centres, ascending, and their paths; between two
        // neighbours each track's own cross-section reaches to the
        // midpoint. Empty: one track at 0 (a path's own bed).
        QVector<float> tracks;
        QVector<int> trackPaths;
    };

    // A switch (step 7): two paths starting together and parting, with
    // blades between them. Its parts stand from its toe (the paths'
    // start) along its main path.
    struct Switch {
        int group = -1;
        // The MainRoute's path (else the first of those starting there),
        // and the other: a three-way point is two switches.
        int main = -1;
        int other = -1;
        // -1: the other path leaves to the left of the main one, 1 right.
        int diverging = 0;
        // From the toe to the blades' heel: where a blade's gap from its
        // stock rail reaches bladeOpening (the open blade's), the farther
        // of the two blades.
        float bladeLength = 0;
    };

    // Where a switch part stands: on its switch's main path `along`
    // metres past the toe, `side` of it (-1 left, 1 right; its mesh turned
    // half round on the left).
    struct SwitchPlacement {
        float position[2] = {0, 0};
        float heading = 0;
        float along = 0;
        int side = 1;
    };

    struct Group {
        QVector<int> paths;
        TrackShapeAnalysis::Reference reference;
        QVector<Tie> ties;
        QVector<Fastener> fasteners;
        // The group's bed along its reference line; then one bed per path
        // where a path runs outside it (or every path, crossings steeper
        // than maxSharedAngle).
        QVector<BedSlice> bed;
        QVector<QVector<BedSlice>> pathBeds;
    };

    Settings settings;
    QVector<Path> paths;
    // Paths in no group (drawn as plain track).
    QVector<int> independent;
    QVector<Group> groups;
    // The running rails' lines (both of every path in a group); drawn: the
    // stretches without a crossing's cuts.
    QVector<RailLine> rails;
    // What is swept: running and stock rails, blades, wing and check rails.
    QVector<RailPiece> railPieces;
    QVector<Crossing> crossings;
    QVector<Switch> switches;
    QStringList notes;

    // The plan of paths (a shape's, with included shapes' after them).
    static ComplexTrackPlan make(const QVector<Path> &paths, const Settings &settings,
            int flags = 0);
    // A conductor or rack rail at `offset` (right positive) along every
    // path of the groups that lays `part`: cut where it would lie between
    // another track's rails or within conductorClearance of them, and where
    // it comes within conductorClearance of an earlier path's line (lying on
    // it: drawn once; crossing or beside it); pieces under 2 m left out
    // (decision 24).
    QVector<RailLine> conductorLines(float offset, unsigned part) const;

    // The side a switch's parts stand on (decision 36): the sign of the
    // part's Offset x (0 as +) on the diverging side, the other way with
    // switchSideFlip.
    int switchSide(const Switch &sw, const SwitchPart &part) const;
    // The places of a part at a switch: at `-Offset z` past the toe, then
    // every Spacing along the blades as long as at least one Spacing is
    // left before the heel (decision 36); one where Spacing is 0.
    QVector<SwitchPlacement> switchPlacements(const Switch &sw, const SwitchPart &part) const;

    // A line `offset` metres to the right of `line`, its distances
    // measured along it.
    static Line offsetLine(const Line &line, float offset);
    // Where the line o + t n crosses a polyline: (t, the polyline's heading
    // and distance there), within |t| <= reach. Segments are half-open, the
    // polyline's last point included.
    struct Hit {
        float t = 0;
        float heading = 0;
        float distance = 0;
    };
    static QVector<Hit> lineHits(float ox, float oz, float nx, float nz,
            const Line &line, float reach = 12.0f);
    // A tie's footprint overlapping another's (separating axes).
    static bool overlaps(const Tie &a, const Tie &b);
};

#endif
