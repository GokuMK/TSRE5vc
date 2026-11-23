/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TRACKSHAPEANALYSIS_H
#define TRACKSHAPEANALYSIS_H

#include <QString>
#include <QVector>
#include <unordered_map>

class TrackShape;
class TSection;

// Tracks task 17: a tsection.dat track shape's paths in shape space (MSTS's:
// +x right, +z forward, headings in radians, positive to the right), grouped
// by geometry, never by name. Paths whose centre lines come closer than a
// tie plus the gap kept between tie ends share ties: a complex group, drawn
// from a profile family's COMPLEX member around one reference line. The
// other paths are independent, drawn as plain track. The port of
// extra/tsection-review/tieplan.py's groups and reference lines.
class TrackShapeAnalysis {
public:
    struct Point {
        float x = 0;
        float z = 0;
        float heading = 0;
        // Along the path (the line) from its start.
        float distance = 0;
    };

    // The line a group's shared ties lie square to (design section 4).
    struct Reference {
        enum class Type {
            // Two straight paths crossing steeper than maxSharedAngle: no
            // shared ties; path (the first by the order below) keeps all its
            // ties, the others get theirs where they fit.
            None,
            // One of the group's paths: a straight one on the shape's axis
            // (from the origin, heading 0), else the straightest, the one
            // nearest the axis's heading, the longest.
            Path,
            // Two straight paths whose centre lines cross: their bisector,
            // over the whole group.
            Bisector,
            // No straight path (wyes, curved points): the mean of the two
            // least curved paths, by equal distance from their start.
            Mean
        };
        Type type = Type::None;
        // Path, None: the path; Bisector, Mean: the first of the two.
        int path = -1;
        // Bisector, Mean: the second.
        int otherPath = -1;
        // Bisector, None: the crossing angle, degrees (0-90).
        float crossingAngle = 0;
        // Sampled along the line; Path: the path's own samples.
        QVector<Point> line;
    };

    struct Group {
        // Ascending, two or more.
        QVector<int> paths;
        Reference reference;
    };

    // Kinds, as trackprofiles.dat filters name them (Kind ( ... )): a shape
    // has its geometry's one (Single, Parallel or Complex) and its flags'.
    enum KindFlag {
        Single = 1,
        Parallel = 2,
        Complex = 4,
        Tunnel = 8,
        Road = 16
    };

    static constexpr float DefaultTieLength = 2.6f;
    static constexpr float DefaultTieGap = 0.1f;
    static constexpr float DefaultMaxSharedAngle = 60.0f;
    static constexpr float SampleStep = 0.1f;

    // False: one of the shape's sections is missing from the file.
    bool valid = false;
    // Each path's centre line, sampled every SampleStep or less.
    QVector<QVector<Point>> paths;
    QVector<Group> groups;
    // Paths in no group, ascending.
    QVector<int> independent;
    // KindFlag values.
    int kinds = 0;

    // groupDistance: centre lines closer than this share ties (a tie's
    // length plus the gap between tie ends).
    static TrackShapeAnalysis analyse(const TrackShape &shape,
            const std::unordered_map<int, TSection*> &sections,
            float groupDistance = DefaultTieLength + DefaultTieGap,
            float maxSharedAngle = DefaultMaxSharedAngle);
    // One path's centre line; empty when a section is missing.
    static QVector<Point> samplePath(const TrackShape &shape, int path,
            const std::unordered_map<int, TSection*> &sections,
            float step = SampleStep);
    // A sampled line at a distance along it (clamped to its ends).
    static Point at(const QVector<Point> &line, float distance);

    // Kind names for messages ("Complex, Tunnel").
    static QString kindNames(int kinds);
    // A Kind value's flag, any case; 0 for an unknown name.
    static int kindFromName(const QString &name);
};

#endif
