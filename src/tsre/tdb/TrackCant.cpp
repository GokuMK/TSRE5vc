#include <tsre/tdb/TrackCant.h>

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TrackSpeedPosts.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/units/UnitConversion.h>
#include <tsre/world/Trk.h>

namespace TrackCant {

namespace {

constexpr float Inch = 0.0254f;
constexpr float MilePerHour = 0.44704f;
// Open Rails' resume post: no limit.
constexpr float ResumeSpeed = 999.0f;
// Open Rails keeps cant this far from a vector node's ends.
constexpr float NodeEndClear = 20.0f;

// Open Rails' Interpolator: the segment holding `value`, the first or the
// last past the ends (linear there too).
float sample(int points, const float *x, const float *y, float value) {
    if (points < 2)
        return points == 1 ? y[0] : 0.0f;
    int i = 0;
    if (value < x[1]) {
        i = 0;
    } else if (value > x[points - 2]) {
        i = points - 2;
    } else {
        int j = points - 1;
        while (j - i > 1) {
            const int k = (i + j) / 2;
            if (x[k] > value)
                j = k;
            else
                i = k;
        }
    }
    const float d = x[i + 1] - x[i];
    if (d == 0.0f)
        return y[i + 1];
    const float a = (x[i + 1] - value) / d;
    const float b = (value - x[i]) / d;
    return a * y[i] + b * y[i + 1];
}

int sign(float value) {
    return value > 0.0f ? 1 : value < 0.0f ? -1 : 0;
}

// The first word of a block ("ORTSSuperElevation ( ... )"), lower case.
QString blockName(const QString &block) {
    int end = 0;
    while (end < block.size() && !block[end].isSpace() && block[end] != '(')
        ++end;
    return block.left(end).toLower();
}

// The values inside a block, as written ("( 100 0.1 500 0.05 )").
QStringList blockValues(const QString &block) {
    std::unique_ptr<FileBuffer> data(FileBuffer::fromText(block));
    QStringList values;
    if (ParserX::NextTokenInside(data.get()).isEmpty())
        return values;
    for (;;) {
        const QString token = ParserX::GetToken(data.get());
        if (token.isEmpty())
            break;
        values << token;
    }
    return values;
}

const TSection *sectionData(TDB &tdb, const TRnode *node, int index) {
    if (tdb.tsection == nullptr)
        return nullptr;
    const auto found = tdb.tsection->sekcja.find(int(node->trVectorSection[index].sectionIndex));
    return found == tdb.tsection->sekcja.end() ? nullptr : found->second;
}

bool curved(const TSection *section) {
    return section != nullptr && section->type == 1;
}

float lengthOf(const TSection *section) {
    return section->type == 1 ? std::fabs(section->radius * section->angle) : section->size;
}

// Open Rails' DetermineTrackSpeeds at a place: the higher of both ways,
// the passenger's for a missing freight one and the other way round, none
// as infinite; at most the route's limit.
void speedsAt(SpeedScan &scan, int node, float offset, float routeLimit, float &passenger, float &freight) {
    float f = std::max(scan.at(node, offset, true, true), scan.at(node, offset, false, true));
    float p = std::max(scan.at(node, offset, true, false), scan.at(node, offset, false, false));
    if (f <= 0.0f && p > 0.0f)
        f = p;
    if (p <= 0.0f && f > 0.0f)
        p = f;
    if (f <= 0.0f)
        f = Infinity;
    if (p <= 0.0f)
        p = Infinity;
    freight = std::min(f, routeLimit);
    passenger = std::min(p, routeLimit);
}

struct Speeds {
    float passenger = -Infinity;
    float freight = -Infinity;
};

// Each section's speeds, at its middle, as Open Rails walks the node (a
// section missing from tsection.dat skipped and not counted).
std::vector<Speeds> nodeSpeeds(TDB &tdb, const Route &route, SpeedScan &scan, int id, const TRnode *node) {
    std::vector<Speeds> speeds(size_t(std::max(0, node->iTrv)));
    float cumulative = 0.0f;
    for (int i = 0; i < node->iTrv; ++i) {
        const TSection *section = sectionData(tdb, node, i);
        if (section == nullptr)
            continue;
        const float length = lengthOf(section);
        cumulative += length;
        speedsAt(scan, id, cumulative - length / 2.0f, route.speedLimit, speeds[size_t(i)].passenger,
                 speeds[size_t(i)].freight);
    }
    return speeds;
}

// Open Rails' MarkSections: a curve run's standard, nominal cant and the
// tables of its sections.
void markRun(TDB &tdb, const Route &route, const TRnode *node, std::vector<Section> &out,
             const std::vector<int> &run, float totalLength, const std::vector<float> &lengths, int direction,
             int &curves) {
    if (run.empty())
        return;
    float maxSpeed = -Infinity;
    for (int k : run)
        maxSpeed = std::max(maxSpeed, std::max(out[size_t(k)].passengerSpeed, out[size_t(k)].freightSpeed));
    const Standard *standard = nullptr;
    float slope = 0.0f;
    float maxCant = 0.0f;
    for (const Standard &check : route.standards) {
        if (maxSpeed < check.maxSpeed + 0.05f && maxSpeed > check.minSpeed - 0.05f) {
            standard = &check;
            slope = std::min(check.runoffSlope, check.runoffSpeed / maxSpeed);
            maxCant = std::clamp(check.maxCant, 0.0f, route.gauge);
            maxCant = std::min(totalLength / (2.0f * 1.3f) * slope, maxCant);
            break;
        }
    }
    bool preset = false;
    for (int k : run)
        preset = preset || out[size_t(k)].nominal > 0.0f;
    if ((standard == nullptr || maxCant < standard->minCant) && !preset) {
        // As Open Rails: the first section only, then nothing (its tables
        // stay flat).
        out[size_t(run.front())].nominal = 0.0f;
        return;
    }
    if (standard == nullptr || slope <= 0.0f) {
        // A preset cant where no standard fits: Open Rails fails here; none.
        return;
    }
    for (int k : run) {
        Section &s = out[size_t(k)];
        if (s.nominal >= 0.0f)
            continue;
        const TSection *section = sectionData(tdb, node, k);
        if (!curved(section)) {
            s.nominal = 0.0f;
            continue;
        }
        float cant = 0.0f;
        if (standard->legacy && !route.radiusCant.empty()) {
            cant = route.radiusCant.at(section->radius);
        } else {
            // E = G v^2 / (g R) less the underbalance, the higher of the
            // passenger's and the freight's.
            const float factor = route.gauge / (9.81f * section->radius);
            const float passenger = factor * (s.passengerSpeed * s.passengerSpeed) - standard->maxPassengerUnderbalance;
            const float freight = factor * (s.freightSpeed * s.freightSpeed) - standard->maxFreightUnderbalance;
            cant = std::max(passenger, freight);
        }
        cant = float(std::round(double(cant / standard->precision))) * standard->precision;
        cant = std::clamp(cant, standard->minCant, maxCant);
        s.nominal = cant;
    }

    const int curve = curves++;
    for (int k : run)
        out[size_t(k)].curve = curve;
    const int n = int(run.size());
    std::vector<float> startCant(size_t(n), 0.0f), endCant(size_t(n), 0.0f);
    float accumulated = 0.0f;
    for (int c = 0; c < n; ++c) {
        Section &s = out[size_t(run[size_t(c)])];
        const float length = lengths[size_t(c)];
        startCant[size_t(c)] = c == 0 ? 0.0f : endCant[size_t(c - 1)];
        // The change along the whole run at most the runoff slope.
        const float maxEnd = std::min(accumulated + length, totalLength - (accumulated + length)) * slope;
        if (c >= n - 1) {
            endCant[size_t(c)] = 0.0f;
        } else {
            Section &next = out[size_t(run[size_t(c + 1)])];
            // A short straight inside the run keeps the cant around it.
            if (c < n - 2 && next.nominal == 0.0f && lengths[size_t(c + 1)] < (s.nominal / slope) * 1.5f)
                next.nominal = (s.nominal + out[size_t(run[size_t(c + 2)])].nominal) / 2.0f;
            endCant[size_t(c)] = std::min((s.nominal + next.nominal) / 2.0f, maxEnd);
        }
        const float start = startCant[size_t(c)];
        const float end = endCant[size_t(c)];
        // A linear change, unless one of the cases below.
        float cants[4] = {start, end, 0.0f, 0.0f};
        float positions[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        int points = 2;
        const float startRunoff = std::fabs(s.nominal - start) / slope;
        const float endRunoff = std::fabs(end - s.nominal) / slope;
        const float totalRunoff = std::fabs(end - start) / slope;
        if (startRunoff + endRunoff >= length) {
            // Too short for the ideal profile; a cusp unless it only rises
            // or falls.
            if (totalRunoff < length * 0.75f) {
                float midLength = 0.0f;
                float midCant = 0.0f;
                if (s.nominal > start && s.nominal > end) {
                    const float maxEndCant = start + slope * length;
                    midLength = length - ((maxEndCant - end) / slope) / 2.0f;
                    midCant = start + slope * midLength;
                } else if (s.nominal == 0.0f) {
                    midLength = length / 2.0f;
                    midCant = (start + end) / 2.0f;
                } else {
                    const float minEndCant = start - slope * length;
                    midLength = length - ((end - minEndCant) / slope) / 2.0f;
                    midCant = start + slope * midLength;
                }
                midCant = std::max(0.0f, midCant);
                cants[0] = start;
                cants[1] = midCant;
                cants[2] = end;
                positions[0] = 0.0f;
                positions[1] = midLength / length;
                positions[2] = 1.0f;
                points = 3;
            }
        } else if (!(start == s.nominal && s.nominal == end)) {
            // The ideal profile: up to the nominal cant, held, down.
            if (start != s.nominal && end != s.nominal) {
                cants[0] = start;
                cants[1] = s.nominal;
                cants[2] = s.nominal;
                cants[3] = end;
                positions[0] = 0.0f;
                positions[1] = startRunoff / length;
                positions[2] = (length - endRunoff) / length;
                positions[3] = 1.0f;
                points = 4;
            } else {
                const float midLength = start == s.nominal ? length - endRunoff : startRunoff;
                cants[0] = start;
                cants[1] = s.nominal;
                cants[2] = end;
                positions[0] = 0.0f;
                positions[1] = midLength / length;
                positions[2] = 1.0f;
                points = 3;
            }
        }
        s.points = (unsigned char)points;
        for (int p = 0; p < 4; ++p) {
            s.position[p] = p < points ? positions[p] : 0.0f;
            s.physical[p] = p < points ? cants[p] : 0.0f;
            // The angle, its sign by the curve's direction (Open Rails'
            // VisElevTable.ScaleY(-direction)).
            s.visual[p] = p < points ? float(std::asin(double(cants[p] / route.gauge))) * float(-direction) : 0.0f;
        }
        accumulated += length;
    }
}

unsigned long long ownerKeyOf(int tileX, int tileZ, unsigned int uid);

// Open Rails' RemoveSuperElevation: no visual (and with `physical` no)
// cant on a section of a curve, the joining ends of its neighbours in the
// curve flat too; it leaves the curve.
void removeCant(std::vector<Section> &out, int k, bool physical) {
    Section &s = out[size_t(k)];
    if (s.curve < 0)
        return;
    int previous = -1, next = -1;
    for (int j = k - 1; j >= 0 && previous < 0; --j)
        if (out[size_t(j)].curve == s.curve)
            previous = j;
    for (int j = k + 1; j < int(out.size()) && next < 0; ++j)
        if (out[size_t(j)].curve == s.curve)
            next = j;
    if (previous >= 0 && out[size_t(previous)].points > 0) {
        Section &p = out[size_t(previous)];
        p.visual[p.points - 1] = 0.0f;
        if (physical)
            p.physical[p.points - 1] = 0.0f;
    }
    if (next >= 0 && out[size_t(next)].points > 0) {
        out[size_t(next)].visual[0] = 0.0f;
        if (physical)
            out[size_t(next)].physical[0] = 0.0f;
    }
    for (int p = 0; p < 4; ++p) {
        s.visual[p] = 0.0f;
        if (physical)
            s.physical[p] = 0.0f;
    }
    if (physical)
        s.nominal = 0.0f;
    s.curve = -1;
}

// Cant taken away from the database alone: a junction's or a crossover's
// sections have none (Open Rails' ClearJunctionSuperElevation and its
// crossovers), a tunnel's no visual cant; and the objects drawn without it
// (Tables::visualOff).
void removeWhereNotDrawn(TDB &tdb, const TRnode *node, std::vector<Section> &out) {
    for (int i = 0; i < node->iTrv && size_t(i) < out.size(); ++i) {
        if (out[size_t(i)].curve < 0)
            continue;
        const TrackVectorSection &v = node->trVectorSection[i];
        const TrackShape *shape = nullptr;
        if (tdb.tsection != nullptr) {
            const auto found = tdb.tsection->shape.find(int(v.shapeIndex));
            if (found != tdb.tsection->shape.end())
                shape = found->second;
        }
        if (shape != nullptr && shape->numpaths > 1 && (shape->clearancedist > 0 || shape->mainroute >= 0)) {
            removeCant(out, i, true);
            continue;
        }
        if ((shape != nullptr && shape->tunnelshape)
                || tdb.cant.visualOff.count(ownerKeyOf(v.worldTileX, v.worldTileZ, v.worldObjectId)) > 0)
            removeCant(out, i, false);
    }
}

// The pivots their objects registered (Tables::pivots).
void applyPivots(TDB &tdb, const TRnode *node, std::vector<Section> &out) {
    if (tdb.cant.pivots.empty())
        return;
    for (int i = 0; i < node->iTrv && size_t(i) < out.size(); ++i) {
        const TrackVectorSection &v = node->trVectorSection[i];
        const auto found = tdb.cant.pivots.find(ownerKeyOf(v.worldTileX, v.worldTileZ, v.worldObjectId));
        if (found != tdb.cant.pivots.end())
            out[size_t(i)].pivot = found->second;
    }
}

// Open Rails' SuperElevation constructor for one vector node: its curve
// runs (curves at least 20 m from its ends, a straight before and after
// them, broken where the curve turns the other way) and their tables.
void computeNode(TDB &tdb, const Route &route, int id, const TRnode *node, const std::vector<Speeds> &speeds,
                 std::vector<Section> &out, int &curves) {
    const int count = std::max(0, node->iTrv);
    out.assign(size_t(count), Section());
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        const TSection *section = sectionData(tdb, node, i);
        if (section != nullptr)
            total += lengthOf(section);
    }
    bool inCurve = false;
    int direction = 0;
    float curveLength = 0.0f;
    std::vector<int> run;
    std::vector<float> lengths;
    float cumulative = 0.0f;
    const auto mark = [&]() { markRun(tdb, route, node, out, run, curveLength, lengths, direction, curves); };
    for (int i = 0; i < count; ++i) {
        const TSection *section = sectionData(tdb, node, i);
        if (section == nullptr)
            continue;
        const TSection *next = i < count - 1 ? sectionData(tdb, node, i + 1) : nullptr;
        const bool nextCurved = curved(next);
        const float length = lengthOf(section);
        const bool clear = !(cumulative + length < NodeEndClear || total - cumulative < NodeEndClear);
        if (curved(section)) {
            if (clear) {
                if (!inCurve) {
                    inCurve = true;
                    direction = sign(section->angle);
                    curveLength = 0.0f;
                } else if (direction != sign(section->angle)) {
                    mark();
                    direction = sign(section->angle);
                    run.clear();
                    lengths.clear();
                    curveLength = 0.0f;
                }
                curveLength += length;
                lengths.push_back(length);
                run.push_back(i);
            }
        } else if (clear) {
            if (inCurve) {
                // The straight joins the run unless the next section curves
                // the other way; the run ends unless the curve goes on.
                if (next != nullptr && !(nextCurved && direction != sign(next->angle))) {
                    curveLength += length;
                    lengths.push_back(length);
                    run.push_back(i);
                }
                if (!(nextCurved && direction == sign(next->angle))) {
                    mark();
                    curveLength = 0.0f;
                    run.clear();
                    lengths.clear();
                    inCurve = false;
                }
            } else if (next != nullptr && nextCurved) {
                // The straight before a curve starts it.
                inCurve = true;
                direction = sign(next->angle);
                curveLength = length;
                lengths.push_back(length);
                run.push_back(i);
            }
        }
        cumulative += length;
        // Its speeds become known only now (Open Rails' order: a run marked
        // above did not see them).
        out[size_t(i)].passengerSpeed = speeds[size_t(i)].passenger;
        out[size_t(i)].freightSpeed = speeds[size_t(i)].freight;
    }
    if (inCurve)
        mark();
    // TSRE_CANT_RAW: the cant as computed, nothing taken away (as Open
    // Rails' tables are before its scenery loads; the comparison).
    static const bool raw = qEnvironmentVariableIsSet("TSRE_CANT_RAW");
    if (!raw)
        removeWhereNotDrawn(tdb, node, out);
    applyPivots(tdb, node, out);
    (void)id;
}

}

float Table::at(float value) const {
    return sample(int(std::min(x.size(), y.size())), x.data(), y.data(), value);
}

Standard Standard::makeDefault(bool metric, bool highSpeed) {
    Standard s;
    if (metric) {
        if (highSpeed) {
            s.runoffSpeed = 0.080f;
            s.maxFreightUnderbalance = 0.110f;
            s.maxPassengerUnderbalance = 0.150f;
        } else {
            s.maxFreightUnderbalance = 0.100f;
            s.maxPassengerUnderbalance = 0.130f;
        }
    } else {
        s.minCant = 0.5f * Inch;
        s.maxCant = 6.0f * Inch;
        s.minSpeed = 15.5f * MilePerHour;
        s.precision = 0.25f * Inch;
        if (highSpeed) {
            s.runoffSpeed = 0.1136f * MilePerHour;
            s.maxFreightUnderbalance = 3.0f * Inch;
            s.maxPassengerUnderbalance = 5.0f * Inch;
        } else {
            s.runoffSpeed = 0.0852f * MilePerHour;
            s.maxFreightUnderbalance = 2.0f * Inch;
            s.maxPassengerUnderbalance = 3.0f * Inch;
        }
    }
    return s;
}

Standard Standard::fromTable(const Table &radiusCant, bool metric, bool highSpeed) {
    Standard s;
    if (!radiusCant.y.empty()) {
        s.minCant = *std::min_element(radiusCant.y.begin(), radiusCant.y.end());
        s.maxCant = *std::max_element(radiusCant.y.begin(), radiusCant.y.end());
    }
    s.minSpeed = 0.0f;
    if (metric) {
        if (highSpeed)
            s.runoffSpeed = 0.080f;
    } else {
        s.precision = 0.25f * Inch;
        s.runoffSpeed = (highSpeed ? 0.1136f : 0.0852f) * MilePerHour;
    }
    return s;
}

bool Standard::parse(const QString &block, Standard &out) {
    std::unique_ptr<FileBuffer> data(FileBuffer::fromText(block));
    if (ParserX::NextTokenInside(data.get()).toLower() != "ortssuperelevation")
        return false;
    Standard s;
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (sh == "maxfreightunderbalance")
            s.maxFreightUnderbalance = ParserX::GetNumber(data.get(), Units::Quantity::Length);
        else if (sh == "maxpassengerunderbalance")
            s.maxPassengerUnderbalance = ParserX::GetNumber(data.get(), Units::Quantity::Length);
        else if (sh == "minimumcant")
            s.minCant = ParserX::GetNumber(data.get(), Units::Quantity::Length);
        else if (sh == "maximumcant")
            s.maxCant = ParserX::GetNumber(data.get(), Units::Quantity::Length);
        else if (sh == "minimumspeed")
            s.minSpeed = ParserX::GetNumber(data.get(), Units::Quantity::Speed);
        else if (sh == "maximumspeed")
            s.maxSpeed = ParserX::GetNumber(data.get(), Units::Quantity::Speed);
        else if (sh == "precision")
            s.precision = ParserX::GetNumber(data.get(), Units::Quantity::Length);
        else if (sh == "maxrunoffslope")
            s.runoffSlope = ParserX::GetNumber(data.get());
        else if (sh == "maxrunoffspeed")
            s.runoffSpeed = ParserX::GetNumber(data.get(), Units::Quantity::Speed);
        ParserX::SkipToken(data.get());
    }
    s.isDefault = false;
    // Open Rails: the radius table only where no underbalance is given; one
    // underbalance stands for the other, none: 5 and 7.5 cm.
    if (s.maxFreightUnderbalance <= 10.0f || s.maxPassengerUnderbalance <= 10.0f)
        s.legacy = false;
    if (s.maxFreightUnderbalance > 10.0f) {
        if (s.maxPassengerUnderbalance > 10.0f) {
            s.maxFreightUnderbalance = 0.05f;
            s.maxPassengerUnderbalance = 0.075f;
        } else {
            s.maxFreightUnderbalance = s.maxPassengerUnderbalance;
        }
    } else if (s.maxPassengerUnderbalance > 10.0f) {
        s.maxPassengerUnderbalance = s.maxFreightUnderbalance;
    }
    out = s;
    return true;
}

Route Route::fromBlocks(const QStringList &blocks, float trackGauge, float speedLimit, bool metric) {
    Route route;
    route.gauge = trackGauge > 0.0f ? trackGauge : 1.435f;
    route.speedLimit = speedLimit;
    route.metric = metric;
    for (const QString &block : blocks) {
        const QString name = blockName(block);
        if (name == "ortssuperelevation") {
            Standard standard;
            if (Standard::parse(block, standard))
                route.standards.push_back(standard);
        } else if (name == "ortstracksuperelevation") {
            const QStringList values = blockValues(block);
            Table table;
            for (int i = 0; i + 1 < values.size(); i += 2) {
                double radius = 0.0, cant = 0.0;
                Units::fromFileToken(values[i], Units::Quantity::Any, radius);
                Units::fromFileToken(values[i + 1], Units::Quantity::Any, cant);
                table.x.push_back(float(radius));
                table.y.push_back(float(cant));
            }
            if (values.size() % 2 == 1 || table.x.size() < 2)
                qWarning() << "ORTSTrackSuperElevation: pairs of radius and cant expected:" << block;
            route.radiusCant = table;
        } else if (name == "ortsforcesuperelevation") {
            const QStringList values = blockValues(block);
            const QString value = values.isEmpty() ? QString() : values.first().toLower();
            route.forced = value == "true" || value.toDouble() != 0.0 ? 1 : 0;
        }
    }
    if (route.standards.empty()) {
        const bool highSpeed = route.speedLimit > 45.0f;
        route.standards.push_back(route.radiusCant.empty() ? Standard::makeDefault(metric, highSpeed)
                                                           : Standard::fromTable(route.radiusCant, metric, highSpeed));
    }
    return route;
}

Route Route::fromTrk(const Trk &trk) {
    return fromBlocks(trk.kept, trk.trackGauge, trk.speedLimit, trk.milepostUnitsKilometers);
}

float Section::physicalAt(float fraction) const {
    return sample(points, position, physical, fraction);
}

float Section::visualAt(float fraction) const {
    return sample(points, position, visual, fraction);
}

bool Section::sameCant(const Section &other) const {
    if (points != other.points || nominal != other.nominal)
        return false;
    for (int p = 0; p < points; ++p)
        if (position[p] != other.position[p] || physical[p] != other.physical[p] || visual[p] != other.visual[p])
            return false;
    return true;
}

const Section *Tables::section(int node, int index) const {
    const auto found = nodes.find(node);
    if (found == nodes.end() || index < 0 || size_t(index) >= found->second.size())
        return nullptr;
    return &found->second[size_t(index)];
}

namespace {
unsigned long long ownerKeyOf(int tileX, int tileZ, unsigned int uid) {
    return (static_cast<unsigned long long>(static_cast<unsigned int>(tileX) & 0xFFFFu) << 48)
         ^ (static_cast<unsigned long long>(static_cast<unsigned int>(tileZ) & 0xFFFFu) << 32)
         ^ static_cast<unsigned long long>(uid);
}
}

const std::vector<Tables::Owned> *Tables::ownedBy(int tileX, int tileZ, unsigned int uid) const {
    const auto found = owners.find(ownerKeyOf(tileX, tileZ, uid));
    return found == owners.end() ? nullptr : &found->second;
}

void setVisualOff(TDB &tdb, int tileX, int tileZ, unsigned int uid, bool off) {
    const unsigned long long key = ownerKeyOf(tileX, tileZ, uid);
    const bool was = tdb.cant.visualOff.count(key) > 0;
    if (was == off)
        return;
    if (off)
        tdb.cant.visualOff.insert(key);
    else
        tdb.cant.visualOff.erase(key);
    std::set<int> nodes;
    if (const auto *owned = tdb.cant.ownedBy(tileX, tileZ, uid))
        for (const Tables::Owned &o : *owned)
            nodes.insert(o.node);
    if (nodes.empty())
        return;
    const Route route = tdb.cant.route;
    update(tdb, route, nodes);
    // Its neighbours are rebuilt; it is deciding its own shape now.
    tdb.cant.rebuild.erase({tileX, tileZ, uid});
}

void setPivot(TDB &tdb, int tileX, int tileZ, unsigned int uid, float pivot) {
    tdb.cant.pivots[ownerKeyOf(tileX, tileZ, uid)] = pivot;
    if (const auto *owned = tdb.cant.ownedBy(tileX, tileZ, uid))
        for (const Tables::Owned &o : *owned) {
            const auto node = tdb.cant.nodes.find(o.node);
            if (node != tdb.cant.nodes.end() && size_t(o.index) < node->second.size())
                node->second[size_t(o.index)].pivot = pivot;
        }
}

void Tables::indexOwners(TDB &tdb) {
    owners.clear();
    std::vector<int> ids;
    for (const auto &entry : tdb.trackNodes)
        if (entry.second != nullptr && entry.second->typ == 1 && entry.first > 0)
            ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const TRnode *node = tdb.trackNodes[id];
        for (int j = 0; j < node->iTrv; ++j) {
            const TrackVectorSection &s = node->trVectorSection[j];
            owners[ownerKeyOf(s.worldTileX, s.worldTileZ, s.worldObjectId)].push_back({id, j});
        }
    }
}

SpeedScan::SpeedScan(TDB &database) : tdb(database) {
    for (const auto &entry : tdb.trackNodes) {
        const TRnode *node = entry.second;
        if (node == nullptr || node->typ != 1 || entry.first <= 0)
            continue;
        std::vector<Post> list;
        for (int i = 0; i < node->iTri; ++i) {
            const auto found = tdb.trackItems.find(node->trItemRef[i]);
            if (found == tdb.trackItems.end() || found->second == nullptr)
                continue;
            TRitem *item = found->second;
            if (item->type != "speedpostitem")
                continue;
            const float distance = item->getTrackPosition();
            float pose[7] = {0, 0, 0, 0, 0, 0, 0};
            const bool posed = tdb.getDrawPositionOnTrNode(pose, entry.first, distance);
            TrackSpeedPosts::Post read;
            if (!TrackSpeedPosts::read(*item, posed ? pose[3] : 0.0f, read))
                continue;
            // Warning posts never count (Open Rails' this_lim_speed).
            if (read.kind == TrackSpeedPosts::Post::Warning)
                continue;
            Post post;
            post.distance = distance;
            post.forward = posed ? read.forward : true;
            const float speed = read.kind == TrackSpeedPosts::Post::Resume ? ResumeSpeed : read.speed;
            post.passenger = read.passenger ? speed : -1.0f;
            post.freight = read.freight ? speed : -1.0f;
            list.push_back(post);
        }
        if (list.empty())
            continue;
        std::stable_sort(list.begin(), list.end(),
                         [](const Post &a, const Post &b) { return a.distance < b.distance; });
        posts.emplace(entry.first, std::move(list));
    }
}

float SpeedScan::at(int node, float distance, bool forward, bool freight) {
    const auto found = posts.find(node);
    if (found != posts.end()) {
        const std::vector<Post> &list = found->second;
        if (forward) {
            for (auto it = list.rbegin(); it != list.rend(); ++it) {
                const float speed = freight ? it->freight : it->passenger;
                if (it->distance < distance && it->forward && speed > 0.0f)
                    return speed;
            }
        } else {
            for (const Post &post : list) {
                const float speed = freight ? post.freight : post.passenger;
                if (post.distance > distance && !post.forward && speed > 0.0f)
                    return speed;
            }
        }
    }
    return entry(node, forward, freight);
}

float SpeedScan::last(int node, bool forward, bool freight) const {
    const auto found = posts.find(node);
    if (found == posts.end())
        return -Infinity;
    const std::vector<Post> &list = found->second;
    if (forward) {
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            const float speed = freight ? it->freight : it->passenger;
            if (it->forward && speed > 0.0f)
                return speed;
        }
    } else {
        for (const Post &post : list) {
            const float speed = freight ? post.freight : post.passenger;
            if (!post.forward && speed > 0.0f)
                return speed;
        }
    }
    return -Infinity;
}

bool SpeedScan::previous(int node, bool forward, int &previousNode, bool &previousForward) const {
    const auto self = tdb.trackNodes.find(node);
    if (self == tdb.trackNodes.end() || self->second == nullptr || self->second->typ != 1)
        return false;
    // The end trains moving this way come in by.
    const int end = forward ? 0 : 1;
    const int link = self->second->pins[end].link;
    const auto junctionAt = tdb.trackNodes.find(link);
    if (link <= 0 || junctionAt == tdb.trackNodes.end() || junctionAt->second == nullptr
            || junctionAt->second->typ != 2)
        return false;
    const TRnode *junction = junctionAt->second;
    const int pin = PathTrack::junctionPin(junction, node, end);
    if (pin < 0)
        return false;
    // Back through it: from its input the main route (default switch
    // position), from a branch its input.
    const int nextPin = pin < junction->inputPinCount ? PathTrack::junctionMainPin(&tdb, junction) : 0;
    if (nextPin < 0 || nextPin > 2)
        return false;
    const int nextNode = junction->pins[nextPin].link;
    const auto next = tdb.trackNodes.find(nextNode);
    if (nextNode <= 0 || next == tdb.trackNodes.end() || next->second == nullptr || next->second->typ != 1)
        return false;
    const int nextEnd = PathTrack::endOfJunctionPin(junction, link, nextPin, next->second);
    if (nextEnd < 0)
        return false;
    previousNode = nextNode;
    // Trains leave it at that end: its way when that is its end 1.
    previousForward = nextEnd == 1;
    return true;
}

float SpeedScan::entry(int node, bool forward, bool freight) {
    std::vector<long long> chain;
    std::unordered_set<int> seen;
    float value = -Infinity;
    int current = node;
    bool currentForward = forward;
    for (;;) {
        const long long key = (long long)current * 4 + (currentForward ? 2 : 0) + (freight ? 1 : 0);
        const auto known = entries.find(key);
        if (known != entries.end()) {
            value = known->second;
            break;
        }
        // Back where it was: none (Open Rails stops at a section it passed).
        if (!seen.insert(current).second)
            break;
        chain.push_back(key);
        int previousNode = 0;
        bool previousForward = true;
        if (!previous(current, currentForward, previousNode, previousForward))
            break;
        const float found = last(previousNode, previousForward, freight);
        if (found > 0.0f) {
            value = found;
            break;
        }
        current = previousNode;
        currentForward = previousForward;
    }
    for (long long key : chain)
        entries[key] = value;
    return value;
}

void computeAll(TDB &tdb, const Route &route) {
    // The loaded objects' decisions and the rebuilds asked stay.
    const std::set<unsigned long long> visualOff = tdb.cant.visualOff;
    const std::unordered_map<unsigned long long, float> pivots = tdb.cant.pivots;
    const std::set<Tables::Owner> rebuild = tdb.cant.rebuild;
    tdb.cant = Tables();
    tdb.cant.visualOff = visualOff;
    tdb.cant.pivots = pivots;
    tdb.cant.rebuild = rebuild;
    tdb.cant.route = route;
    SpeedScan scan(tdb);
    std::vector<int> ids;
    for (const auto &entry : tdb.trackNodes)
        if (entry.second != nullptr && entry.second->typ == 1 && entry.first > 0)
            ids.push_back(entry.first);
    // Open Rails' order: by node id.
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const TRnode *node = tdb.trackNodes[id];
        const std::vector<Speeds> speeds = nodeSpeeds(tdb, route, scan, id, node);
        computeNode(tdb, route, id, node, speeds, tdb.cant.nodes[id], tdb.cant.curves);
    }
    tdb.cant.indexOwners(tdb);
}

std::set<int> update(TDB &tdb, const Route &route, const std::set<int> &changed) {
    tdb.cant.route = route;
    std::set<int> computed;
    for (auto it = tdb.cant.nodes.begin(); it != tdb.cant.nodes.end();) {
        const auto node = tdb.trackNodes.find(it->first);
        if (node == tdb.trackNodes.end() || node->second == nullptr || node->second->typ != 1) {
            computed.insert(it->first);
            it = tdb.cant.nodes.erase(it);
        } else {
            ++it;
        }
    }
    SpeedScan scan(tdb);
    std::vector<int> ids;
    for (const auto &entry : tdb.trackNodes)
        if (entry.second != nullptr && entry.second->typ == 1 && entry.first > 0)
            ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const TRnode *node = tdb.trackNodes[id];
        const std::vector<Speeds> speeds = nodeSpeeds(tdb, route, scan, id, node);
        const auto known = tdb.cant.nodes.find(id);
        bool again = changed.count(id) > 0 || known == tdb.cant.nodes.end()
                || known->second.size() != size_t(std::max(0, node->iTrv));
        for (size_t i = 0; !again && i < speeds.size(); ++i)
            again = known->second[i].passengerSpeed != speeds[i].passenger
                    || known->second[i].freightSpeed != speeds[i].freight;
        if (!again)
            continue;
        // An edited node: every object on it rebuilt; one whose speeds
        // changed: those whose sections' cant did.
        const bool edited = changed.count(id) > 0 || known == tdb.cant.nodes.end()
                || known->second.size() != size_t(std::max(0, node->iTrv));
        const std::vector<Section> before = edited ? std::vector<Section>() : known->second;
        computeNode(tdb, route, id, node, speeds, tdb.cant.nodes[id], tdb.cant.curves);
        computed.insert(id);
        const std::vector<Section> &after = tdb.cant.nodes[id];
        for (int i = 0; i < node->iTrv; ++i) {
            if (!edited && size_t(i) < before.size() && before[size_t(i)].sameCant(after[size_t(i)]))
                continue;
            const TrackVectorSection &s = node->trVectorSection[i];
            tdb.cant.rebuild.insert({s.worldTileX, s.worldTileZ, s.worldObjectId});
        }
    }
    // Any edit may move sections between objects and nodes.
    tdb.cant.indexOwners(tdb);
    return computed;
}

void Tables::queueChanged(TDB &tdb, const Tables &before) {
    for (const auto &entry : owners) {
        const auto old = before.owners.find(entry.first);
        bool same = old != before.owners.end() && old->second.size() == entry.second.size();
        for (size_t k = 0; same && k < entry.second.size(); ++k) {
            const Section *a = section(entry.second[k].node, entry.second[k].index);
            const Section *b = before.section(old->second[k].node, old->second[k].index);
            same = a != nullptr && b != nullptr && a->sameCant(*b);
        }
        if (same || entry.second.empty())
            continue;
        const Owned &first = entry.second.front();
        const auto node = tdb.trackNodes.find(first.node);
        if (node == tdb.trackNodes.end() || node->second == nullptr || first.index >= node->second->iTrv)
            continue;
        const TrackVectorSection &s = node->second->trVectorSection[first.index];
        rebuild.insert({s.worldTileX, s.worldTileZ, s.worldObjectId});
    }
}

void dumpIfAsked(TDB &tdb) {
    static const QString path = qEnvironmentVariable("TSRE_CANT_DUMP");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "TSRE_CANT_DUMP: cannot write" << path;
        return;
    }
    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(5);
    std::vector<int> ids;
    for (const auto &entry : tdb.cant.nodes)
        ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    const auto speed = [](float value) { return std::isfinite(value) ? QString::number(value, 'f', 3) : QString("-"); };
    for (int id : ids) {
        const std::vector<Section> &sections = tdb.cant.nodes[id];
        const TRnode *node = tdb.trackNodes[id];
        for (size_t i = 0; i < sections.size(); ++i) {
            const Section &s = sections[i];
            out << "node " << id << " section " << int(i) << " index "
                << (node != nullptr && int(i) < node->iTrv ? int(node->trVectorSection[i].sectionIndex) : -1)
                << " pass " << speed(s.passengerSpeed) << " freight " << speed(s.freightSpeed)
                << " nominal " << s.nominal;
            if (s.points > 0) {
                out << " cant";
                for (int p = 0; p < s.points; ++p)
                    out << " " << s.position[p] << ":" << s.physical[p];
                out << " vis";
                for (int p = 0; p < s.points; ++p)
                    out << " " << s.position[p] << ":" << s.visual[p];
            }
            out << "\n";
        }
    }
    qInfo() << "TSRE_CANT_DUMP: wrote" << ids.size() << "nodes to" << path;
}

}
