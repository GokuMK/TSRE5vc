/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/OrtsTrackProfileRenderer.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>

namespace {

struct Found {
    int node = 0;
    int index = 0;
    bool reversed = false;
};

// An object's vector sections whose endpoint numbers are `start` and `end`
// (either way round), in database order.
std::vector<Found> sectionsBetween(TDB &tdb, int tileX, int tileZ, unsigned int uid, int start, int end) {
    std::vector<Found> found;
    const auto *owned = tdb.cant.ownedBy(tileX, tileZ, uid);
    if(owned == nullptr)
        return found;
    for(const TrackCant::Tables::Owned &o : *owned){
        const auto node = tdb.trackNodes.find(o.node);
        if(node == tdb.trackNodes.end() || node->second == nullptr || o.index >= node->second->iTrv)
            continue;
        const TrackVectorSection &s = node->second->trVectorSection[o.index];
        if(s.worldTileX != tileX || s.worldTileZ != tileZ || s.worldObjectId != uid)
            continue;
        if(int(s.startEndpointIndex) == start && int(s.endEndpointIndex) == end)
            found.push_back({o.node, o.index, false});
        else if(int(s.startEndpointIndex) == end && int(s.endEndpointIndex) == start)
            found.push_back({o.node, o.index, true});
    }
    return found;
}

// A path's spans over its sections: `found` its vector sections in
// database order (read backwards when reversed).
OrtsProfileRoll spansOf(TDB &tdb, const std::vector<Found> &found, const QVector<float> &lengths) {
    OrtsProfileRoll roll;
    if(found.empty() || int(found.size()) != lengths.size())
        return roll;
    const bool reversed = found.front().reversed;
    float from = 0.0f;
    bool any = false;
    for(int k = 0; k < lengths.size(); k++){
        const Found &f = found[size_t(reversed ? lengths.size() - 1 - k : k)];
        OrtsProfileRoll::Span span;
        span.from = from;
        span.length = lengths[k];
        span.reversed = f.reversed;
        if(const TrackCant::Section *cant = tdb.cant.section(f.node, f.index))
            span.cant = *cant;
        any = any || span.cant.points > 0;
        roll.spans.append(span);
        from += lengths[k];
    }
    if(!any)
        roll.spans.clear();
    return roll;
}

}

QVector<OrtsProfileRoll> OrtsProfileRoll::forObject(TDB &tdb, int tileX, int tileZ,
        unsigned int uid, const TrackShape &shape) {
    QVector<OrtsProfileRoll> rolls(std::max(0, shape.numpaths));
    if(tdb.tsection == nullptr)
        return rolls;
    // The paths' endpoint numbers as TDB::placeTrack gives them (and MSTS):
    // a path's start shares an earlier path's start number at the same
    // place, every end is new.
    std::unordered_map<int, int> numberAt;
    int next = 0;
    QVector<int> starts(shape.numpaths), ends(shape.numpaths);
    for(int p = 0; p < shape.numpaths; p++){
        const int place = (float)shape.path[p].pos[0] * 100000 + (float)shape.path[p].pos[1] * 1000
                + (float)shape.path[p].pos[2] * 10;
        const auto known = numberAt.find(place);
        if(known == numberAt.end()){
            starts[p] = next++;
            numberAt[place] = starts[p];
        } else {
            starts[p] = known->second;
        }
        ends[p] = next++;
    }
    for(int p = 0; p < shape.numpaths; p++){
        QVector<float> lengths;
        for(int k = 0; k < shape.path[p].n; k++){
            const auto section = tdb.tsection->sekcja.find(int(shape.path[p].sect[k]));
            lengths.append(section != tdb.tsection->sekcja.end() && section->second != nullptr
                           ? section->second->getDlugosc() : 0.0f);
        }
        rolls[p] = spansOf(tdb, sectionsBetween(tdb, tileX, tileZ, uid, starts[p], ends[p]), lengths);
    }
    return rolls;
}

OrtsProfileRoll OrtsProfileRoll::forDynTrack(TDB &tdb, int tileX, int tileZ,
        unsigned int uid, const QVector<TSection> &sections) {
    QVector<float> lengths;
    for(const TSection &section : sections)
        lengths.append(section.getDlugosc());
    return spansOf(tdb, sectionsBetween(tdb, tileX, tileZ, uid, 0, 1), lengths);
}

float OrtsProfileRoll::angle(float distance, int *side) const {
    if(side != nullptr)
        *side = 0;
    if(spans.isEmpty())
        return 0.0f;
    // The span holding it; the first or the last past the path's ends.
    int i = 0;
    while(i + 1 < spans.size() && distance > spans[i].from + spans[i].length)
        i++;
    const Span &span = spans[i];
    float fraction = span.length > 0 ? (distance - span.from) / span.length : 0.0f;
    fraction = std::max(0.0f, std::min(1.0f, fraction));
    // Open Rails' DetermineRotation: a reversed section read from its end,
    // negated.
    const float angle = span.reversed ? -span.cant.visualAt(1.0f - fraction)
                                      : span.cant.visualAt(fraction);
    if(side != nullptr && span.cant.points > 0){
        float low = span.cant.visual[0];
        float high = span.cant.visual[0];
        for(int p = 1; p < span.cant.points; p++){
            low = std::min(low, span.cant.visual[p]);
            high = std::max(high, span.cant.visual[p]);
        }
        // Open Rails' Direction: the sign of max + min, against the
        // section's way.
        const float sum = low + high;
        const int sign = sum > 0 ? 1 : sum < 0 ? -1 : 0;
        *side = sign * (span.reversed ? 1 : -1);
    }
    return angle;
}

bool OrtsProfileRoll::rolls(float from, float to) const {
    for(const Span &span : spans){
        if(span.from >= to || span.from + span.length <= from)
            continue;
        for(int p = 0; p < span.cant.points; p++)
            if(std::abs(span.cant.visual[p]) > 1e-7f)
                return true;
    }
    return false;
}

float OrtsProfileRoll::pivotOf(const OrtsTrackProfile &profile) {
    switch(profile.superElevationMethod){
        case OrtsTrackProfile::SuperElevationMethod::Outside:
            return profile.trackGauge / 2.0f;
        case OrtsTrackProfile::SuperElevationMethod::Inside:
            return -profile.trackGauge / 2.0f;
        case OrtsTrackProfile::SuperElevationMethod::Both:
        case OrtsTrackProfile::SuperElevationMethod::None:
        default:
            return 0.0f;
    }
}
