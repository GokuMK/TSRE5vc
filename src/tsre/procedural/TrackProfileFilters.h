/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TRACKPROFILEFILTERS_H
#define TRACKPROFILEFILTERS_H

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <unordered_map>

class TrackShape;
class TSection;

// Tracks task 17: the route's track profile filters, <route>/trackprofiles.dat
// (edited by hand): each filter gives the track objects whose shape file name
// it matches a profile family, ahead of Open Rails' guess. The first filter
// that matches takes the object (design section 1, decision 25). The name
// only filters; the geometry always comes from tsection.dat.
//
//   TrackProfileFilters (
//       Filter (
//           Shapes ( "DB/DB30_*" "A*Concrete*" )   any case, * and ?
//           Kind ( Complex )                       optional, any of
//           Profile ( TrProfile_DB30 )             a family, or DISABLED
//           ExcludedParts ( Tie Fastener Bed )     optional; or IncludedParts
//           IncludedTrackShape ( "A1tPnt7_5dLftSlip.s" 0 0 -10 0 )
//           IncludedTrackShapeParts ( Tie Fastener Bed Other )
//           HandOperated ( 1 )                     optional (step 7)
//           SwitchSideFlip ( 1 )                   optional (step 7)
//       )
//   )
class TrackProfileFilters {
public:
    // The parts of a COMPLEX member (design sections 1 and 2).
    enum Part : unsigned {
        Rail = 1,
        Tie = 2,
        Fastener = 4,
        Bed = 8,
        ConductorRail = 16,
        RackRail = 32,
        Other = 64,
        // Phase 2: opposite every crossing nose.
        CheckRail = 128,
        // Step 7: a switch's drives and its indicator (decision 35).
        SwitchDrive = 256,
        SwitchIndicator = 512
    };
    static constexpr unsigned AllParts = Rail | Tie | Fastener | Bed
            | ConductorRail | RackRail | Other | CheckRail
            | SwitchDrive | SwitchIndicator;
    // What an included track shape gives by default: no rails of any kind
    // nor switch parts, which stay with the junction object that owns them
    // (decision 12).
    static constexpr unsigned DefaultIncludedTrackShapeParts = Tie | Fastener | Bed | Other;

    struct IncludedTrackShape {
        // The tsection FileName, or empty when given by index.
        QString shape;
        int index = -1;
        // Metres in the including shape's space.
        float position[3] = {0, 0, 0};
        // Degrees, positive to the right, as SectionIdx.
        float rotationDeg = 0;
    };

    struct Filter {
        // Its line in the file (1 the first).
        int line = 0;
        QStringList shapes;
        QVector<QRegularExpression> patterns;
        // TrackShapeAnalysis::KindFlag values, any of; 0: every shape.
        int kinds = 0;
        // A profile family's ID; empty when disabled.
        QString profile;
        // Profile ( DISABLED ): the static shape.
        bool disabled = false;
        // The parts the matched shapes draw (IncludedParts, ExcludedParts).
        unsigned parts = AllParts;
        QVector<IncludedTrackShape> includedTrackShapes;
        unsigned includedTrackShapeParts = DefaultIncludedTrackShapeParts;
        // Step 7: the matched switches are operated by hand (they draw the
        // SwitchDrive templates marked HandOperated, decision 38); their
        // switch parts stand on the other side (decision 36).
        bool handOperated = false;
        bool switchSideFlip = false;

        bool matchesName(const QString &shapeFileName) const;
        // "line 12: Shapes ( "A*Tun*" ) Profile ( PL_60E1_Tunnel )".
        QString description() const;
    };

    static QString fileName();
    static QVector<Filter> parse(const QString &text,
            QStringList *diagnostics = nullptr);
    // Reads <routePath>/trackprofiles.dat (through the route's files: a
    // server client's are its server's); no file, no filters. Warns about
    // what it leaves out. The shapes' kinds are found again.
    static void load(const QString &routePath);
    static const QVector<Filter> &filters();
    // For tests: these filters in place of a file's.
    static void set(const QVector<Filter> &filters);

    // The first filter matching a track object's shape file name (as its W
    // file has it, '\' read as '/') and, where the filter names kinds, its
    // shape's kinds (asked for only then); none.
    static const Filter *match(const QString &shapeFileName,
            const std::function<int()> &kinds);
    // A tsection shape's kinds (TrackShapeAnalysis with the default tie),
    // kept per shape until the next load.
    static int kindsOf(const TrackShape &shape,
            const std::unordered_map<int, TSection*> &sections);

    // A part's flag by name, any case; 0 unknown.
    static unsigned partFromName(const QString &name);
    static QString partNames(unsigned parts);

private:
    static QVector<Filter> loaded;
    static QHash<int, int> kinds;
};

#endif
