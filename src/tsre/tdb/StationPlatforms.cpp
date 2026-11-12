/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "StationPlatforms.h"

#include <QHash>
#include <algorithm>
#include <cmath>

#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>

namespace StationPlatforms {

namespace {

bool itemPlace(const TRitem &item, double out[3]) {
    if (item.trItemRData == nullptr)
        return false;
    out[0] = double(item.trItemRData[3]) * 2048.0 + double(item.trItemRData[0]);
    out[1] = double(item.trItemRData[1]);
    out[2] = double(item.trItemRData[4]) * 2048.0 + double(item.trItemRData[2]);
    return true;
}

}

std::vector<Platform> read(TDB &tdb) {
    // The node of each item, from the nodes' references.
    QHash<int, int> nodeOfItem;
    for (int id = 1; id <= tdb.iTRnodes; ++id) {
        const auto found = tdb.trackNodes.find(id);
        if (found == tdb.trackNodes.end() || found->second == nullptr)
            continue;
        const TRnode *node = found->second;
        if (node->typ != 1 || node->trItemRef == nullptr)
            continue;
        for (int i = 0; i < node->iTri; ++i)
            nodeOfItem.insert(node->trItemRef[i], id);
    }
    std::vector<Platform> platforms;
    for (const auto &pair : tdb.trackItems) {
        TRitem *item = pair.second;
        if (item == nullptr || item->platformTrItemData == nullptr
                || item->type.compare("platformitem", Qt::CaseInsensitive) != 0)
            continue;
        const int other = int(item->platformTrItemData[1]);
        // Each platform once, from its lower item.
        if (other <= int(item->trItemId))
            continue;
        const auto found = tdb.trackItems.find(other);
        if (found == tdb.trackItems.end() || found->second == nullptr)
            continue;
        TRitem *end = found->second;
        Platform platform;
        platform.start = int(item->trItemId);
        platform.end = other;
        platform.station = item->stationName.trimmed();
        platform.name = item->platformName.trimmed();
        if (!itemPlace(*item, platform.ends) || !itemPlace(*end, platform.ends + 3))
            continue;
        const int node = nodeOfItem.value(platform.start, -1);
        platform.node = node > 0 && nodeOfItem.value(platform.end, -1) == node ? node : -1;
        platform.from = std::min(double(item->getTrackPosition()), double(end->getTrackPosition()));
        platform.to = std::max(double(item->getTrackPosition()), double(end->getTrackPosition()));
        platforms.push_back(platform);
    }
    std::sort(platforms.begin(), platforms.end(), [](const Platform &a, const Platform &b) { return a.start < b.start; });
    return platforms;
}

void sample(TDB &tdb, Platform &platform, double step) {
    platform.sampled = true;
    platform.points.clear();
    if (platform.node > 0) {
        const int steps = std::max(1, int(std::ceil((platform.to - platform.from) / std::max(step, 0.1))));
        for (int i = 0; i <= steps; ++i) {
            float out[7];
            if (!tdb.getDrawPositionOnTrNode(out, platform.node,
                                             float(platform.from + (platform.to - platform.from) * i / steps)))
                continue;
            platform.points.push_back(double(out[5]) * 2048.0 + double(out[0]));
            platform.points.push_back(double(out[1]));
            platform.points.push_back(double(out[6]) * 2048.0 + double(out[2]));
        }
    }
    if (platform.points.size() < 6)
        platform.points.assign(platform.ends, platform.ends + 6);
}

}
